cmake_minimum_required(VERSION 3.16)

project(pkcs11_iot_test C)

set(CMAKE_C_STANDARD 99)

set(AZURE_IOT_SDK_DIR "$ENV{HOME}/Work/azure-iot-sdk-c")

include_directories(
    ${AZURE_IOT_SDK_DIR}/iothub_client/inc
    ${AZURE_IOT_SDK_DIR}/c-utility/inc
    ${AZURE_IOT_SDK_DIR}/umqtt/inc
    ${AZURE_IOT_SDK_DIR}/cmake
    ${AZURE_IOT_SDK_DIR}/deps/azure-macro-utils-c/inc
    ${AZURE_IOT_SDK_DIR}/deps/umock-c/inc
    /usr/include/p11-kit-1
)

link_directories(
    ${AZURE_IOT_SDK_DIR}/cmake/iothub_client
    ${AZURE_IOT_SDK_DIR}/cmake/c-utility
    ${AZURE_IOT_SDK_DIR}/cmake/umqtt
    ${AZURE_IOT_SDK_DIR}/cmake/provisioning_client
    ${AZURE_IOT_SDK_DIR}/cmake/provisioning_client/deps/utpm
    ${AZURE_IOT_SDK_DIR}/cmake/deps/parson
    ${AZURE_IOT_SDK_DIR}/cmake/deps/c-logging/v2
)

add_library(pkcs11iot SHARED
    pkcs11iot.c
)

target_link_libraries(pkcs11iot
    iothub_client_mqtt_transport
    iothub_client
    umqtt
    prov_auth_client
    hsm_security_client
    utpm
    aziotsharedutil
    parson
    c_logging_v2
    c_logging_v2_core
    ssl
    crypto
    pthread
    dl
    uuid
    m
)