#ifndef PKCS11IOT_H
#define PKCS11IOT_H

#ifdef __cplusplus
extern "C" {
#endif

int iot_connect(
    const char* hub_name,
    const char* device_id,
    const char* pkcs11_module,
    const char* token_label,
    unsigned char certificate_id,
    const char* private_key_uri);

int iot_send(const char* json_payload);

void iot_disconnect(void);

#ifdef __cplusplus
}
#endif

#endif