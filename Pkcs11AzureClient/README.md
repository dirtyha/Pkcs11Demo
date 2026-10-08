## Install az CLI
The cleanest way is Microsoft’s official APT repository.
```text
sudo apt update
sudo apt install -y ca-certificates curl apt-transport-https lsb-release gnupg
```

Add Microsoft’s signing key and repository:
```text
curl -sL https://packages.microsoft.com/keys/microsoft.asc \
  | gpg --dearmor \
  | sudo tee /etc/apt/keyrings/microsoft.gpg > /dev/null
```

Then add the Azure CLI repository:

```text
AZ_DIST=$(lsb_release -cs)

echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/microsoft.gpg] https://packages.microsoft.com/repos/azure-cli/ ${AZ_DIST} main" \
  | sudo tee /etc/apt/sources.list.d/azure-cli.list
```

Install Azure CLI:
```text
sudo apt update
sudo apt install -y azure-cli
```

Verify:
```text
az version
```

## Azure Resources

First check that you're logged in and see which subscription is active:
```text
az login
```

if you have several subscriptions:
```text
az account list --output table

az account set --subscription "<subscription-name-or-id>"
```

Then install/update the Azure IoT CLI extension:
```text
az extension add --upgrade --name azure-iot
```

Create a resource group:
```text
az group create \
    --name rg-pkcs11-demo \
    --location northeurope
```

Register Microsoft.Devices resource provider:
```text
az provider register --namespace Microsoft.Devices
```

Create an IoT Hub:
```text
az iot hub create \
    --resource-group rg-pkcs11-demo \
    --name <YOUR-UNIQUE-HUB-NAME> \
    --sku F1 \
    --partition-count 2
```

For example, you might try IoT Hub name like:
```text
pkcs11-demo
```

