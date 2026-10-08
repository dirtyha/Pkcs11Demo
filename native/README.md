## Compiling Azure IoT C SDK

First, install the build dependencies:
```bash
sudo apt update
sudo apt install -y \
  git \
  cmake \
  build-essential \
  libssl-dev \
  libcurl4-openssl-dev \
  uuid-dev
```

Then clone the Azure IoT C SDK with its submodules:
```bash
cd ~/Work

git clone --recursive https://github.com/Azure/azure-iot-sdk-c.git
cd azure-iot-sdk-c
```

Create a build directory:
```bash
mkdir -p cmake
cd cmake
```

Configure it for MQTT:
```bash
cmake .. \
  -Duse_mqtt=ON \
  -Duse_amqp=OFF \
  -Duse_http=OFF \
  -Dskip_samples=ON
```

Then build:
```bash
cmake --build . -j$(nproc)
```

## Build pkcs11iot connectivity libary

```bash
cd native
mkdir -p build
cd build

cmake ..
cmake --build .
```

The result should be:
```bash
ls -l libpkcs11iot.so
```

You are now ready to create C# app that calls libpkcs11iot.so, for example by using DllImports:
```C#
[DllImport(
        "pkcs11iot",
        CallingConvention = CallingConvention.Cdecl)]
    public static extern int iot_connect(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string hubName,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string deviceId,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string certificatePath,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string pkcs11Uri);
```






