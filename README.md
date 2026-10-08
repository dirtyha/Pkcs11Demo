# Pkcs11Demo

Connect a device to **Azure IoT Hub** using an X.509 identity whose private key never leaves a **PKCS#11 token** (SoftHSM).

A C# console app calls a small native C library (`libpkcs11iot.so`) that wraps the Azure IoT C SDK. The library reads the device certificate from the token through PKCS#11 and lets OpenSSL's `pkcs11` engine perform the TLS signing operations with the token-held private key.

```text
C# app (Pkcs11AzureClient)
   │  P/Invoke: iot_connect / iot_send / iot_disconnect
   ▼
libpkcs11iot.so (native/)
   │  Azure IoT C SDK (MQTT) + OpenSSL pkcs11 engine
   ▼
SoftHSM token "Pkcs#11Demo"  ──►  device key + certificate (ID 01)
   │
   ▼
Azure IoT Hub (X.509 CA authentication)
```

## Repository layout

| Path | Contents |
| --- | --- |
| [`Pkcs11AzureClient/`](Pkcs11AzureClient/) | C# (.NET 10) client that connects and sends a telemetry message. Its README covers installing the Azure CLI and creating the IoT Hub. |
| [`native/`](native/) | C library wrapping the Azure IoT C SDK. Its README covers building the SDK and the library. |
| [`certs/`](certs/) | Walkthrough for the demo root CA, CA verification in IoT Hub, generating the device key in SoftHSM, signing the CSR and storing the certificate in the token. Generated certificates and keys are kept out of git. |

## Prerequisites

- Linux (tested on Ubuntu 22.04, x86_64)
- .NET 10 SDK
- SoftHSM 2, `pkcs11-tool` (OpenSC) and the OpenSSL `pkcs11` engine (libp11)
- CMake, a C compiler, `libssl-dev`, `libcurl4-openssl-dev`, `uuid-dev`
- Azure CLI with the `azure-iot` extension and an Azure subscription

## Getting started

1. **Create Azure resources:** follow [`Pkcs11AzureClient/README.md`](Pkcs11AzureClient/README.md) to install the Azure CLI and create a resource group and IoT Hub.
2. **Set up the PKI and device identity:** follow [`certs/README.md`](certs/README.md) to create the root CA, verify it in IoT Hub, generate the device key pair inside SoftHSM, issue the device certificate and import it into the token.
3. **Build the native library:** follow [`native/README.md`](native/README.md). The CMake file expects the Azure IoT C SDK in `~/Work/azure-iot-sdk-c`.
4. **Run the client:**

   ```bash
   export SOFTHSM_PIN=<your token user PIN>
   export LD_LIBRARY_PATH=$PWD/native/build:$LD_LIBRARY_PATH
   dotnet run --project Pkcs11AzureClient
   ```

   Expected output:

   ```text
   Connecting...
   Connected.
   Message sent.
   Disconnected.
   ```

## Configuration

The hub name, device ID, SoftHSM module path and token label are constants at the top of `Main` in [`Pkcs11AzureClient/Program.cs`](Pkcs11AzureClient/Program.cs). The defaults are:

| Setting | Value |
| --- | --- |
| IoT Hub | `pkcs11-demo` |
| Device ID | `pkcs11-device-01` |
| PKCS#11 module | `/usr/lib/x86_64-linux-gnu/softhsm/libsofthsm2.so` |
| Token label | `Pkcs#11Demo` |
| Key/certificate object ID | `01` |

The token PIN is read from the `SOFTHSM_PIN` environment variable and is never stored in the code.

## Note

This is a learning demo. The root CA is self-signed and its private key lives on disk, and SoftHSM is a software token. A production device would use a real hardware security module or secure element and a properly managed PKI.
