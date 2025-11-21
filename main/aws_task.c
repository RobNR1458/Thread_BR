#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "core_mqtt.h"
#include "network_transport.h"
#include "clock.h"
#include "shared_data.h"

// *** IMPORTANTE: Configura estos valores para tu cuenta AWS ***
#define AWS_IOT_ENDPOINT    "tu-endpoint-aqui-ats.iot.us-east-1.amazonaws.com"
#define AWS_IOT_THING_NAME  "esp32_thread_border_router"
#define MQTT_TOPIC          "thread/sensores"
#define MQTT_PORT           8883

static const char *TAG = "AWS_TASK";

// Certificados embebidos (definidos en CMakeLists.txt)
extern const uint8_t aws_root_ca_pem_start[] asm("_binary_aws_root_ca_pem_start");
extern const uint8_t aws_root_ca_pem_end[]   asm("_binary_aws_root_ca_pem_end");
extern const uint8_t device_cert_pem_start[] asm("_binary_device_cert_pem_start");
extern const uint8_t device_cert_pem_end[]   asm("_binary_device_cert_pem_end");
extern const uint8_t device_key_pem_start[]  asm("_binary_device_key_pem_start");
extern const uint8_t device_key_pem_end[]    asm("_binary_device_key_pem_end");

// MQTT context y buffers
static MQTTContext_t mqttContext;
static NetworkContext_t networkContext;
static uint8_t networkBuffer[2048];
static MQTTFixedBuffer_t mqttBuffer;

// Callback de eventos MQTT
static void mqtt_event_callback(MQTTContext_t *pMqttContext,
                                 MQTTPacketInfo_t *pPacketInfo,
                                 MQTTDeserializedInfo_t *pDeserializedInfo)
{
    uint16_t packetIdentifier = pDeserializedInfo->packetIdentifier;

    switch (pPacketInfo->type) {
        case MQTT_PACKET_TYPE_CONNACK:
            ESP_LOGI(TAG, "CONNACK received");
            break;
        case MQTT_PACKET_TYPE_PUBACK:
            ESP_LOGI(TAG, "PUBACK received for packet ID: %u", packetIdentifier);
            break;
        case MQTT_PACKET_TYPE_PINGRESP:
            ESP_LOGD(TAG, "PINGRESP received (keep-alive)");
            break;
        default:
            ESP_LOGD(TAG, "Other MQTT packet type: %u", pPacketInfo->type);
            break;
    }
}

// Función para inicializar el contexto de red TLS
static bool initialize_network_context(void)
{
    ESP_LOGI(TAG, "Initializing TLS network context...");

    // Configurar el contexto de red con certificados embebidos
    networkContext.pcServerRootCA = (const char *)aws_root_ca_pem_start;
    networkContext.pcServerRootCASize = aws_root_ca_pem_end - aws_root_ca_pem_start;
    networkContext.pcClientCert = (const char *)device_cert_pem_start;
    networkContext.pcClientCertSize = device_cert_pem_end - device_cert_pem_start;
    networkContext.pcClientKey = (const char *)device_key_pem_start;
    networkContext.pcClientKeySize = device_key_pem_end - device_key_pem_start;
    networkContext.pDestinationURL = AWS_IOT_ENDPOINT;
    networkContext.xPort = MQTT_PORT;
    networkContext.disableSni = false;  // SNI es requerido por AWS IoT
    networkContext.pAlpnProtos = NULL;   // Solo necesario para puerto 443

    // Crear semáforo para contexto TLS
    networkContext.xTlsContextSemaphore = xSemaphoreCreateMutex();
    if (networkContext.xTlsContextSemaphore == NULL) {
        ESP_LOGE(TAG, "Failed to create TLS context semaphore");
        return false;
    }

    ESP_LOGI(TAG, "Network context initialized");
    return true;
}

// Función para conectar TLS
static bool connect_tls(void)
{
    ESP_LOGI(TAG, "Connecting to AWS IoT endpoint: %s:%d", AWS_IOT_ENDPOINT, MQTT_PORT);

    TlsTransportStatus_t tlsStatus = xTlsConnect(&networkContext);

    if (tlsStatus != TLS_TRANSPORT_SUCCESS) {
        ESP_LOGE(TAG, "TLS connection failed with status: %d", tlsStatus);
        return false;
    }

    ESP_LOGI(TAG, "TLS connection established successfully");
    return true;
}

// Función para inicializar MQTT
static bool initialize_mqtt(void)
{
    ESP_LOGI(TAG, "Initializing MQTT context...");

    // Configurar el buffer de red para MQTT
    mqttBuffer.pBuffer = networkBuffer;
    mqttBuffer.size = sizeof(networkBuffer);

    // Inicializar contexto MQTT
    TransportInterface_t transport;
    transport.pNetworkContext = &networkContext;
    transport.send = TLS_FreeRTOS_send;
    transport.recv = TLS_FreeRTOS_recv;
    transport.writev = NULL;

    MQTTStatus_t mqttStatus = MQTT_Init(&mqttContext,
                                         &transport,
                                         Clock_GetTimeMs,
                                         mqtt_event_callback,
                                         &mqttBuffer);

    if (mqttStatus != MQTTSuccess) {
        ESP_LOGE(TAG, "MQTT_Init failed with status: %d", mqttStatus);
        return false;
    }

    ESP_LOGI(TAG, "MQTT context initialized successfully");
    return true;
}

// Función para conectar MQTT
static bool connect_mqtt(void)
{
    ESP_LOGI(TAG, "Connecting to AWS IoT Core via MQTT...");

    MQTTConnectInfo_t connectInfo;
    memset(&connectInfo, 0, sizeof(connectInfo));

    connectInfo.cleanSession = false;  // Sesión persistente
    connectInfo.pClientIdentifier = AWS_IOT_THING_NAME;
    connectInfo.clientIdentifierLength = strlen(AWS_IOT_THING_NAME);
    connectInfo.keepAliveSeconds = 60;  // Keep-alive cada 60 segundos

    // String de métricas (opcional pero recomendado)
    connectInfo.pUserName = "?SDK=ESP-IDF&Version=5.4.2&Platform=ESP32-S3&MQTTLib=coreMQTT";
    connectInfo.userNameLength = strlen(connectInfo.pUserName);

    bool sessionPresent = false;
    MQTTStatus_t mqttStatus = MQTT_Connect(&mqttContext,
                                            &connectInfo,
                                            NULL,  // No Last Will Testament
                                            3000,  // Timeout CONNACK: 3 segundos
                                            &sessionPresent);

    if (mqttStatus != MQTTSuccess) {
        ESP_LOGE(TAG, "MQTT_Connect failed with status: %d", mqttStatus);
        return false;
    }

    ESP_LOGI(TAG, "Connected to AWS IoT Core successfully!");
    ESP_LOGI(TAG, "Session present: %s", sessionPresent ? "YES" : "NO");
    return true;
}

// Tarea principal de AWS IoT
void aws_iot_task(void *param)
{
    ESP_LOGI(TAG, "AWS IoT Task started");

    // Esperar un poco para que la red esté lista
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Paso 1: Inicializar contexto de red
    if (!initialize_network_context()) {
        ESP_LOGE(TAG, "Failed to initialize network context. Exiting task.");
        vTaskDelete(NULL);
        return;
    }

    // Paso 2: Conectar TLS
    if (!connect_tls()) {
        ESP_LOGE(TAG, "Failed to establish TLS connection. Exiting task.");
        vTaskDelete(NULL);
        return;
    }

    // Paso 3: Inicializar MQTT
    if (!initialize_mqtt()) {
        ESP_LOGE(TAG, "Failed to initialize MQTT. Exiting task.");
        xTlsDisconnect(&networkContext);
        vTaskDelete(NULL);
        return;
    }

    // Paso 4: Conectar MQTT
    if (!connect_mqtt()) {
        ESP_LOGE(TAG, "Failed to connect MQTT. Exiting task.");
        xTlsDisconnect(&networkContext);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Connection established. Entering main loop (no publishing yet)...");

    // Bucle principal: Solo mantener conexión con keep-alive
    while (1) {
        // Procesar loop de MQTT para keep-alive y recibir paquetes
        MQTTStatus_t mqttStatus = MQTT_ProcessLoop(&mqttContext);

        if (mqttStatus != MQTTSuccess) {
            ESP_LOGW(TAG, "MQTT_ProcessLoop returned status: %d", mqttStatus);
        }

        // Esperar 1 segundo antes del siguiente ciclo
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // Cleanup (nunca debería llegar aquí)
    MQTT_Disconnect(&mqttContext);
    xTlsDisconnect(&networkContext);
    vTaskDelete(NULL);
}

// Función pública para iniciar el cliente AWS
void start_aws_client(void)
{
    xTaskCreate(aws_iot_task, "aws_iot_task", 8192, NULL, 5, NULL);
}
