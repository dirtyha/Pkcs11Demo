#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <p11-kit/pkcs11.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/bio.h>

#include "iothub_device_client_ll.h"
#include "iothub_message.h"
#include "iothubtransportmqtt.h"
#include "iothub_client_options.h"
#include "azure_c_shared_utility/threadapi.h"
#include "azure_c_shared_utility/shared_util_options.h"

#include "pkcs11iot.h"

static IOTHUB_DEVICE_CLIENT_LL_HANDLE g_client = NULL;
static int g_authenticated = 0;

static int g_send_done = 0;
static IOTHUB_CLIENT_CONFIRMATION_RESULT g_send_result;

static char *load_certificate_from_pkcs11(
    const char *module_path,
    const char *token_label,
    unsigned char certificate_id)
{
    void *module = NULL;
    CK_FUNCTION_LIST_PTR p11 = NULL;
    CK_RV rv;

    CK_SLOT_ID_PTR slots = NULL;
    CK_ULONG slot_count = 0;
    CK_SLOT_ID selected_slot = 0;
    int slot_found = 0;

    CK_SESSION_HANDLE session = CK_INVALID_HANDLE;
    CK_OBJECT_HANDLE cert_object = CK_INVALID_HANDLE;

    unsigned char *der = NULL;
    char *pem = NULL;

    module = dlopen(module_path, RTLD_NOW);

    if (!module)
    {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        goto cleanup;
    }

    CK_C_GetFunctionList get_function_list =
        (CK_C_GetFunctionList)dlsym(module, "C_GetFunctionList");

    if (!get_function_list)
    {
        fprintf(stderr, "C_GetFunctionList not found\n");
        goto cleanup;
    }

    rv = get_function_list(&p11);

    if (rv != CKR_OK)
    {
        fprintf(stderr, "C_GetFunctionList failed: 0x%lx\n", rv);
        goto cleanup;
    }

    rv = p11->C_Initialize(NULL);

    /*
     * CKR_CRYPTOKI_ALREADY_INITIALIZED is fine if the OpenSSL
     * PKCS#11 engine already initialized this module.
     */
    if (rv != CKR_OK &&
        rv != CKR_CRYPTOKI_ALREADY_INITIALIZED)
    {
        fprintf(stderr, "C_Initialize failed: 0x%lx\n", rv);
        goto cleanup;
    }

    rv = p11->C_GetSlotList(CK_TRUE, NULL, &slot_count);

    if (rv != CKR_OK || slot_count == 0)
    {
        fprintf(stderr, "No PKCS#11 token slots found\n");
        goto cleanup;
    }

    slots = malloc(slot_count * sizeof(CK_SLOT_ID));

    if (!slots)
        goto cleanup;

    rv = p11->C_GetSlotList(CK_TRUE, slots, &slot_count);

    if (rv != CKR_OK)
        goto cleanup;

    for (CK_ULONG i = 0; i < slot_count; ++i)
    {
        CK_TOKEN_INFO info;

        rv = p11->C_GetTokenInfo(slots[i], &info);

        if (rv != CKR_OK)
            continue;

        char label[33];
        memcpy(label, info.label, 32);
        label[32] = '\0';

        /*
         * PKCS#11 token labels are padded with spaces.
         */
        for (int j = 31; j >= 0; --j)
        {
            if (label[j] == ' ')
                label[j] = '\0';
            else
                break;
        }

        if (strcmp(label, token_label) == 0)
        {
            selected_slot = slots[i];
            slot_found = 1;
            break;
        }
    }

    if (!slot_found)
    {
        fprintf(stderr, "Token '%s' not found\n", token_label);
        goto cleanup;
    }

    rv = p11->C_OpenSession(
        selected_slot,
        CKF_SERIAL_SESSION,
        NULL,
        NULL,
        &session);

    if (rv != CKR_OK)
    {
        fprintf(stderr, "C_OpenSession failed: 0x%lx\n", rv);
        goto cleanup;
    }

    CK_OBJECT_CLASS cert_class = CKO_CERTIFICATE;

    CK_ATTRIBUTE search[] =
        {
            {CKA_CLASS,
             &cert_class,
             sizeof(cert_class)},
            {CKA_ID,
             &certificate_id,
             sizeof(certificate_id)}};

    rv = p11->C_FindObjectsInit(
        session,
        search,
        sizeof(search) / sizeof(search[0]));

    if (rv != CKR_OK)
    {
        fprintf(stderr, "C_FindObjectsInit failed: 0x%lx\n", rv);
        goto cleanup;
    }

    CK_ULONG found = 0;

    rv = p11->C_FindObjects(
        session,
        &cert_object,
        1,
        &found);

    p11->C_FindObjectsFinal(session);

    if (rv != CKR_OK || found != 1)
    {
        fprintf(stderr,
                "Certificate object ID=%02x not found\n",
                certificate_id);

        goto cleanup;
    }

    CK_ATTRIBUTE value =
        {
            CKA_VALUE,
            NULL,
            0};

    rv = p11->C_GetAttributeValue(
        session,
        cert_object,
        &value,
        1);

    if (rv != CKR_OK)
    {
        fprintf(stderr,
                "Unable to read certificate size: 0x%lx\n",
                rv);

        goto cleanup;
    }

    der = malloc(value.ulValueLen);

    if (!der)
        goto cleanup;

    value.pValue = der;

    rv = p11->C_GetAttributeValue(
        session,
        cert_object,
        &value,
        1);

    if (rv != CKR_OK)
    {
        fprintf(stderr,
                "Unable to read certificate: 0x%lx\n",
                rv);

        goto cleanup;
    }

    const unsigned char *der_ptr = der;

    X509 *x509 = d2i_X509(
        NULL,
        &der_ptr,
        value.ulValueLen);

    if (!x509)
    {
        fprintf(stderr,
                "Unable to parse X.509 certificate\n");

        goto cleanup;
    }

    BIO *bio = BIO_new(BIO_s_mem());

    if (!bio)
    {
        X509_free(x509);
        goto cleanup;
    }

    if (!PEM_write_bio_X509(bio, x509))
    {
        fprintf(stderr,
                "Unable to convert certificate to PEM\n");

        BIO_free(bio);
        X509_free(x509);

        goto cleanup;
    }

    BUF_MEM *buffer = NULL;
    BIO_get_mem_ptr(bio, &buffer);

    pem = malloc(buffer->length + 1);

    if (pem)
    {
        memcpy(pem, buffer->data, buffer->length);
        pem[buffer->length] = '\0';
    }

    BIO_free(bio);
    X509_free(x509);

cleanup:

    free(der);
    free(slots);

    if (session != CK_INVALID_HANDLE && p11)
        p11->C_CloseSession(session);

    /*
     * Don't call C_Finalize here.
     *
     * The same PKCS#11 module can subsequently be used by the
     * OpenSSL engine. Finalizing it here can interfere with that.
     */

    if (module)
        dlclose(module);

    return pem;
}

static void connection_status_callback(
    IOTHUB_CLIENT_CONNECTION_STATUS result,
    IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason,
    void *user_context)
{
    (void)user_context;

    printf(
        "IoT connection status: %s, reason=%d\n",
        result == IOTHUB_CLIENT_CONNECTION_AUTHENTICATED
            ? "AUTHENTICATED"
            : "UNAUTHENTICATED",
        (int)reason);

    g_authenticated =
        result == IOTHUB_CLIENT_CONNECTION_AUTHENTICATED;
}

static void send_confirmation_callback(
    IOTHUB_CLIENT_CONFIRMATION_RESULT result,
    void *user_context)
{
    (void)user_context;

    g_send_result = result;
    g_send_done = 1;
}

int iot_connect(
    const char *hub_name,
    const char *device_id,
    const char *pkcs11_module,
    const char *token_label,
    unsigned char certificate_id,
    const char *private_key_uri)
{
    if (g_client != NULL)
    {
        fprintf(stderr, "IoT client already exists\n");
        return 1;
    }

    char *certificate =
        load_certificate_from_pkcs11(
            pkcs11_module,
            token_label,
            certificate_id);

    if (!certificate)
    {
        fprintf(stderr,
                "Unable to load certificate from PKCS#11\n");

        return 2;
    }

    IOTHUB_CLIENT_CONFIG config;
    memset(&config, 0, sizeof(config));

    config.iotHubName = hub_name;
    config.iotHubSuffix = "azure-devices.net";
    config.deviceId = device_id;
    config.protocol = MQTT_Protocol;

    g_client = IoTHubDeviceClient_LL_Create(&config);

    if (!g_client)
    {
        fprintf(stderr, "IoTHubDeviceClient_LL_Create failed\n");
        free(certificate);
        return 3;
    }

    const char *engine = "pkcs11";

    if (IoTHubDeviceClient_LL_SetOption(
            g_client,
            OPTION_OPENSSL_ENGINE,
            engine) != IOTHUB_CLIENT_OK)
    {
        fprintf(stderr, "Failed to configure OpenSSL engine\n");
        goto fail;
    }

    OPTION_OPENSSL_KEY_TYPE key_type = KEY_TYPE_ENGINE;

    if (IoTHubDeviceClient_LL_SetOption(
            g_client,
            OPTION_OPENSSL_PRIVATE_KEY_TYPE,
            &key_type) != IOTHUB_CLIENT_OK)
    {
        fprintf(stderr, "Failed to configure private key type\n");
        goto fail;
    }

    if (IoTHubDeviceClient_LL_SetOption(
            g_client,
            OPTION_X509_CERT,
            certificate) != IOTHUB_CLIENT_OK)
    {
        fprintf(stderr, "Failed to configure X.509 certificate\n");
        goto fail;
    }

    if (IoTHubDeviceClient_LL_SetOption(
            g_client,
            OPTION_X509_PRIVATE_KEY,
            private_key_uri) != IOTHUB_CLIENT_OK)
    {
        fprintf(stderr, "Failed to configure PKCS#11 private key\n");
        goto fail;
    }

    free(certificate);
    certificate = NULL;

    IoTHubDeviceClient_LL_SetConnectionStatusCallback(
        g_client,
        connection_status_callback,
        NULL);

    g_authenticated = 0;

    /*
     * Give the MQTT/TLS connection up to about 15 seconds.
     */
    for (int i = 0; i < 150 && !g_authenticated; i++)
    {
        IoTHubDeviceClient_LL_DoWork(g_client);
        ThreadAPI_Sleep(100);
    }

    if (!g_authenticated)
    {
        fprintf(stderr, "IoT Hub authentication timed out\n");
        iot_disconnect();
        return 4;
    }

    return 0;

fail:

    free(certificate);

    IoTHubDeviceClient_LL_Destroy(g_client);
    g_client = NULL;

    return 5;
}

int iot_send(const char *json_payload)
{
    if (!g_client || !g_authenticated)
    {
        fprintf(stderr, "IoT client is not connected\n");
        return 1;
    }

    IOTHUB_MESSAGE_HANDLE message =
        IoTHubMessage_CreateFromString(json_payload);

    if (!message)
    {
        fprintf(stderr, "IoTHubMessage_CreateFromString failed\n");
        return 2;
    }

    if (IoTHubMessage_SetContentTypeSystemProperty(
            message,
            "application/json") != IOTHUB_MESSAGE_OK)
    {
        fprintf(stderr, "Unable to set content type\n");
        IoTHubMessage_Destroy(message);
        return 3;
    }

    if (IoTHubMessage_SetContentEncodingSystemProperty(
            message,
            "utf-8") != IOTHUB_MESSAGE_OK)
    {
        fprintf(stderr, "Unable to set encoding\n");
        IoTHubMessage_Destroy(message);
        return 4;
    }

    g_send_done = 0;

    if (IoTHubDeviceClient_LL_SendEventAsync(
            g_client,
            message,
            send_confirmation_callback,
            NULL) != IOTHUB_CLIENT_OK)
    {
        fprintf(stderr, "SendEventAsync failed\n");
        IoTHubMessage_Destroy(message);
        return 5;
    }

    /*
     * The SDK owns everything needed after SendEventAsync;
     * our application can destroy its message handle.
     */
    IoTHubMessage_Destroy(message);

    for (int i = 0; i < 100 && !g_send_done; i++)
    {
        IoTHubDeviceClient_LL_DoWork(g_client);
        ThreadAPI_Sleep(100);
    }

    if (!g_send_done)
    {
        fprintf(stderr, "Message confirmation timed out\n");
        return 6;
    }

    if (g_send_result != IOTHUB_CLIENT_CONFIRMATION_OK)
    {
        fprintf(
            stderr,
            "Message delivery failed: %d\n",
            (int)g_send_result);

        return 7;
    }

    return 0;
}

void iot_disconnect(void)
{
    if (g_client)
    {
        IoTHubDeviceClient_LL_Destroy(g_client);
        g_client = NULL;
    }

    g_authenticated = 0;
}