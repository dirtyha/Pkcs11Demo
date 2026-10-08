## Overview
The sequence here should be:
```text
1. Create demo Root CA
2. Upload Root CA public certificate to IoT Hub
3. Verify the CA in IoT Hub
4. Generate device key pair inside SoftHSM
5. Create device CSR from that key
6. Sign CSR with the demo CA
7. Create IoT Hub device identity using X.509 CA authentication
8. Connect from C#
```

## Create demo root CA
First create the demo CA private key:
```text
cd certs
openssl genrsa \
  -out demo-root-ca.key.pem \
  4096
```

Create its self-signed CA certificate:
```text
openssl req \
  -x509 \
  -new \
  -sha256 \
  -key demo-root-ca.key.pem \
  -days 3650 \
  -out demo-root-ca.cert.pem \
  -subj "/CN=PKCS11 Demo Root CA"
```

Check it:
```text
openssl x509 \
  -in demo-root-ca.cert.pem \
  -noout \
  -subject \
  -issuer \
  -fingerprint \
  -sha256
```

You should see roughly:
```text
subject=CN = PKCS11 Demo Root CA
issuer=CN = PKCS11 Demo Root CA
SHA256 Fingerprint=...
```

Now upload the public CA certificate to IoT Hub:
```text
az iot hub certificate create \
  --hub-name pkcs11-demo \
  --name pkcs11-demo-root-ca \
  --path demo-root-ca.cert.pem
```

Then inspect it:
```text
az iot hub certificate show \
  --hub-name pkcs11-demo \
  --name pkcs11-demo-root-ca
```

At this stage the CA may still appear as unverified. Azure supports a proof-of-possession verification flow for uploaded IoT Hub CA certificates.

Now we need to prove to IoT Hub that we actually possess the private key corresponding to pkcs11-demo-root-ca. Azure does that with a verification code and a short-lived certificate signed by our CA.

You need to pass the certificate resource’s current ETag. Azure uses it for optimistic concurrency control.

First fetch it:
```text
az iot hub certificate show \
  --hub-name pkcs11-demo \
  --name pkcs11-demo-root-ca \
  --query etag \
  --output tsv
```

You should get something resembling:
```text
AAAAAAAAAAE=
```

Next generate the verification code and pass ETag:
```text
az iot hub certificate generate-verification-code \
  --hub-name pkcs11-demo \
  --name pkcs11-demo-root-ca \
  --etag 'AAAAAAAAAAE='
```

You should get JSON containing something like:
```text
{
  "properties": {
    "verificationCode": "ABCD1234..."
  }
}
```

Copy that verification code. Then create a temporary verification certificate whose Common Name equals that exact code. Microsoft’s documented proof-of-possession flow requires that certificate to be signed by the CA private key we are verifying.

From your certs directory:
```text
openssl genrsa \
  -out verification.key.pem \
  2048
```

Create a CSR. Replace <VERIFICATION_CODE> with the value Azure returned:
```text
openssl req \
  -new \
  -key verification.key.pem \
  -out verification.csr.pem \
  -subj "/CN=<VERIFICATION_CODE>"
```

Now sign that CSR using our demo root CA:
```text
openssl x509 \
  -req \
  -in verification.csr.pem \
  -CA demo-root-ca.cert.pem \
  -CAkey demo-root-ca.key.pem \
  -CAcreateserial \
  -out verification.cert.pem \
  -days 1 \
  -sha256
```

Check that the subject and issuer look right:
```text
openssl x509 \
  -in verification.cert.pem \
  -noout \
  -subject \
  -issuer
```

Expected structure:
```text
subject=CN = <VERIFICATION_CODE>
issuer=CN = PKCS11 Demo Root CA
```

Then submit that verification certificate back to Azure:
```text
az iot hub certificate verify \
  --hub-name pkcs11-demo \
  --name pkcs11-demo-root-ca \
  --path verification.cert.pem
```

Finally confirm:
```text
az iot hub certificate show \
  --hub-name pkcs11-demo \
  --name pkcs11-demo-root-ca \
  --query "{name:name,verified:properties.isVerified}" \
  --output table
```

We want:
```text
Name                     Verified
-----------------------  --------
pkcs11-demo-root-ca  True
```

## Create device identity

Now create the IoT Hub device identity configured for CA-signed X.509 authentication:
```text
az iot hub device-identity create \
  --hub-name pkcs11-demo \
  --device-id pkcs11-device-01 \
  --auth-method x509_ca
```

After that, generate the device key pair inside SoftHSM. Since your existing token is <TOKEN_NAME>, first find its slot:

```text
softhsm2-util --show-slots
```

Then generate an RSA key pair with pkcs11-tool:
```text
pkcs11-tool \
  --module /usr/lib/x86_64-linux-gnu/softhsm/libsofthsm2.so \
  --login \
  --token-label <TOKEN_NAME> \
  --keypairgen \
  --key-type rsa:2048 \
  --label pkcs11-device-01 \
  --id 01
```

It will ask for the token's user PIN. Then verify that the key exists:
```text
pkcs11-tool \
  --module /usr/lib/x86_64-linux-gnu/softhsm/libsofthsm2.so \
  --login \
  --token-label Pkcs#11Demo \
  --list-objects
```

You should see two objects with ID 01:
```text
Public Key Object
  label:      pkcs11-device-01
  ID:         01

Private Key Object
  label:      pkcs11-device-01
  ID:         01
```

## Create X.509 CSR

Next we need to create an X.509 CSR whose signature is produced by that SoftHSM private key. That requires connecting OpenSSL to PKCS#11 rather than using a normal PEM private key.

First install the OpenSSL PKCS#11 engine:
```text
sudo apt update
sudo apt install -y libengine-pkcs11-openssl
```

Then confirm that OpenSSL can see the engine:
```text
openssl engine pkcs11 -t
```

Expected result:
```text
(pkcs11) pkcs11 engine
     [ available ]
```

Now point the engine explicitly at SoftHSM:
```bash
export PKCS11_MODULE_PATH=/usr/lib/x86_64-linux-gnu/softhsm/libsofthsm2.so
```

Then create the CSR. The private key is referenced using a PKCS#11 URI:
```bash
openssl req \
  -engine pkcs11 \
  -keyform engine \
  -key "pkcs11:token=Pkcs#11Demo;object=pkcs11-device-01;type=private" \
  -new \
  -sha256 \
  -subj "/CN=pkcs11-device-01" \
  -out pkcs11-device-01.csr.pem
```

OpenSSL should ask for the SoftHSM user PIN. The key stays inside SoftHSM; the engine asks the token to perform the RSA signing operation needed to create the CSR.

Verify the CSR:
```bash
openssl req \
  -in pkcs11-device-01.csr.pem \
  -noout \
  -subject \
  -text
```

Then sign the device certificate with your demo CA:
```bash
openssl x509 \
  -req \
  -in pkcs11-device-01.csr.pem \
  -CA demo-root-ca.cert.pem \
  -CAkey demo-root-ca.key.pem \
  -CAcreateserial \
  -out pkcs11-device-01.cert.pem \
  -days 365 \
  -sha256
```

Verify the resulting certificate chain:
```bash
openssl verify \
  -CAfile demo-root-ca.cert.pem \
  pkcs11-device-01.cert.pem
```

Expected:
```text
pkcs11-device-01.cert.pem: OK
```

And inspect it:
```bash
openssl x509 \
  -in pkcs11-device-01.cert.pem \
  -noout \
  -subject \
  -issuer \
  -serial \
  -fingerprint \
  -sha256
```

You should now have:
```text
SoftHSM
└── private key
    label = pkcs11-device-01
    id    = 01

Filesystem
├── pkcs11-device-01.csr.pem
└── pkcs11-device-01.cert.pem

Demo CA
├── demo-root-ca.cert.pem
└── demo-root-ca.key.pem
```

## Storing device X.509 certificate

SoftHSM can store the X.509 certificate object alongside the matching private/public key objects. Then the token contains the full device identity material:

```text
SoftHSM token: Pkcs#11Demo
└── pkcs11-device-01
    ├── Private Key Object   ID=01
    ├── Public Key Object    ID=01
    └── Certificate Object   ID=01
```

The certificate itself is not secret, so keeping it in the filesystem is not a security problem. But storing it in the token is useful because applications can retrieve both the certificate and the private-key handle through the same PKCS#11 interface.

After creating:
```text
pkcs11-device-01.cert.pem
```

convert it to DER, because PKCS#11 certificate objects are typically imported in DER form:
```bash
openssl x509 \
  -in pkcs11-device-01.cert.pem \
  -outform DER \
  -out pkcs11-device-01.cert.der
```

Then import it into SoftHSM:
```bash
pkcs11-tool \
  --module /usr/lib/x86_64-linux-gnu/softhsm/libsofthsm2.so \
  --login \
  --token-label Pkcs#11Demo \
  --write-object pkcs11-device-01.cert.der \
  --type cert \
  --label pkcs11-device-01 \
  --id 01
```

Then verify:
```bash
pkcs11-tool \
  --module /usr/lib/x86_64-linux-gnu/softhsm/libsofthsm2.so \
  --login \
  --token-label Pkcs#11Demo \
  --list-objects
```

You should now see three objects sharing ID 01.

The .NET TLS stack generally expects the certificate and private key to come from the platform crypto system; arbitrary external signing implementations are not directly pluggable into SslStream. By contrast, the Azure IoT C SDK explicitly supports OpenSSL ENGINE-backed private keys, including PKCS#11.


