using System.Runtime.InteropServices;

internal static class NativeIoT
{
    [DllImport(
        "pkcs11iot",
        CallingConvention = CallingConvention.Cdecl)]
    public static extern int iot_connect(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string hubName,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string deviceId,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string pkcs11Module,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string tokenLabel,
        byte certificateId,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string privateKeyUri);

    [DllImport(
        "pkcs11iot",
        CallingConvention = CallingConvention.Cdecl)]
    public static extern int iot_send(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string jsonPayload);

    [DllImport(
        "pkcs11iot",
        CallingConvention = CallingConvention.Cdecl)]
    public static extern void iot_disconnect();
}

internal class Program
{
    private static void Main()
    {
        const string hubName =
            "pkcs11-demo";

        const string deviceId =
            "pkcs11-device-01";

        const string pkcs11Module =
            "/usr/lib/x86_64-linux-gnu/softhsm/libsofthsm2.so";

        const string tokenLabel =
            "Pkcs#11Demo";

        const byte certificateId = 0x01;

        var pin = Environment.GetEnvironmentVariable("SOFTHSM_PIN");

        if (string.IsNullOrWhiteSpace(pin))
        {
            Console.Error.WriteLine(
                "SOFTHSM_PIN environment variable is not set.");

            return;
        }

        var privateKeyUri =
            $"pkcs11:token=Pkcs#11Demo;" +
            $"object=pkcs11-device-01;" +
            $"type=private;" +
            $"id=%01?" +
            $"pin-value={pin}";

        Console.WriteLine("Connecting...");

        var result = NativeIoT.iot_connect(
            hubName,
            deviceId,
            pkcs11Module,
            tokenLabel,
            certificateId,
            privateKeyUri);

        if (result != 0)
        {
            Console.Error.WriteLine(
                $"Connection failed, native result={result}");

            return;
        }

        Console.WriteLine("Connected.");

        const string payload =
            """
            {
              "message": "Hello from C# using PKCS#11",
              "source": "SoftHSM"
            }
            """;

        result = NativeIoT.iot_send(payload);

        if (result == 0)
        {
            Console.WriteLine("Message sent.");
        }
        else
        {
            Console.Error.WriteLine(
                $"Message send failed, native result={result}");
        }

        NativeIoT.iot_disconnect();

        Console.WriteLine("Disconnected.");
    }
}