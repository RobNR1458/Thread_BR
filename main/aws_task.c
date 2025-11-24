#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_random.h"
#include "core_mqtt.h"
#include "network_transport.h"
#include "clock.h"
#include "backoff_algorithm.h"
#include "shared_data.h"

// *** IMPORTANTE: Configura estos valores para tu cuenta AWS ***
//
// 1. AWS_IOT_ENDPOINT: Obtener de AWS IoT Console > Settings > Device data endpoint
//    Ejemplo: "a1b2c3d4e5f6g7-ats.iot.us-east-1.amazonaws.com"
//
// 2. AWS_IOT_THING_NAME: Nombre del Thing creado en AWS IoT Console > Manage > Things
//    Debe coincidir exactamente con el nombre en AWS
//
// 3. MQTT_TOPIC: Topic donde se publicarán los datos de sensores
//    Asegúrate de que tu Thing tenga permisos (Policy) para publicar en este topic
//
// 4. Certificados: Deben estar en certs/ y embeberse via CMakeLists.txt:
//    - aws-root-ca.pem (Amazon Root CA 1)
//    - device.crt (Certificado del dispositivo)
//    - device.key (Clave privada del dispositivo)
//
#define AWS_IOT_ENDPOINT    "a216nupm45ewkv-ats.iot.us-east-2.amazonaws.com"
#define AWS_IOT_THING_NAME  "esp32_thread_border_router"
#define MQTT_TOPIC          "thread/sensores"
#define MQTT_PORT           8883

static const char *TAG = "AWS_TASK";

// Certificados embebidos (definidos en CMakeLists.txt)
extern const uint8_t aws_root_ca_pem_start[] asm("_binary_aws_root_ca_pem_start");
extern const uint8_t aws_root_ca_pem_end[]   asm("_binary_aws_root_ca_pem_end");
extern const uint8_t device_cert_pem_start[] asm("_binary_device_crt_start");
extern const uint8_t device_cert_pem_end[]   asm("_binary_device_crt_end");
extern const uint8_t device_key_pem_start[]  asm("_binary_device_key_start");
extern const uint8_t device_key_pem_end[]    asm("_binary_device_key_end");

// MQTT context y buffers
static MQTTContext_t mqttContext;
static NetworkContext_t networkContext;
static uint8_t networkBuffer[2048];
static MQTTFixedBuffer_t mqttBuffer;

// Buffers para QoS1/QoS2 (requeridos para publish con acknowledgement)
#define OUTGOING_PUBLISH_RECORD_COUNT 10
#define INCOMING_PUBLISH_RECORD_COUNT 10
static MQTTPubAckInfo_t outgoingPublishRecords[OUTGOING_PUBLISH_RECORD_COUNT];
static MQTTPubAckInfo_t incomingPublishRecords[INCOMING_PUBLISH_RECORD_COUNT];

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
    networkContext.pcHostname = AWS_IOT_ENDPOINT;
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
    transport.send = espTlsTransportSend;
    transport.recv = espTlsTransportRecv;
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

    // Inicializar soporte para QoS1/QoS2 (requerido para MQTT_Publish con QoS > 0)
    mqttStatus = MQTT_InitStatefulQoS(&mqttContext,
                                       outgoingPublishRecords,
                                       OUTGOING_PUBLISH_RECORD_COUNT,
                                       incomingPublishRecords,
                                       INCOMING_PUBLISH_RECORD_COUNT);

    if (mqttStatus != MQTTSuccess) {
        ESP_LOGE(TAG, "MQTT_InitStatefulQoS failed with status: %d", mqttStatus);
        return false;
    }

    ESP_LOGI(TAG, "MQTT context initialized successfully with QoS1/QoS2 support");
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

// Función para conectar con reintentos
static bool connect_with_backoff(void)
{
    BackoffAlgorithmContext_t backoffContext;
    BackoffAlgorithmStatus_t backoffStatus;
    uint16_t nextRetryBackoff = 0;
    bool connected = false;
    int attempt = 0;
    const int maxRetries = 5;

    // Inicializar backoff: 1 segundo base, 32 segundos máximo
    BackoffAlgorithm_InitializeParams(&backoffContext, 1000, 32000, maxRetries);

    for (attempt = 0; attempt < maxRetries && !connected; attempt++) {
        ESP_LOGI(TAG, "Connection attempt %d of %d", attempt + 1, maxRetries);

        // Paso 1: Conectar TLS
        if (!connect_tls()) {
            ESP_LOGW(TAG, "TLS connection failed on attempt %d", attempt + 1);
            goto retry;
        }

        // Paso 2: Inicializar MQTT
        if (!initialize_mqtt()) {
            ESP_LOGW(TAG, "MQTT initialization failed on attempt %d", attempt + 1);
            xTlsDisconnect(&networkContext);
            goto retry;
        }

        // Paso 3: Conectar MQTT
        if (!connect_mqtt()) {
            ESP_LOGW(TAG, "MQTT connection failed on attempt %d", attempt + 1);
            xTlsDisconnect(&networkContext);
            goto retry;
        }

        // ¡Conexión exitosa!
        connected = true;
        ESP_LOGI(TAG, "Successfully connected on attempt %d", attempt + 1);
        break;

retry:
        // Calcular delay de backoff exponencial
        // Generar un valor aleatorio para el jitter (0-1000ms)
        uint32_t randomValue = esp_random() % 1000;
        backoffStatus = BackoffAlgorithm_GetNextBackoff(&backoffContext, randomValue, &nextRetryBackoff);

        if (backoffStatus == BackoffAlgorithmSuccess) {
            ESP_LOGW(TAG, "Retrying in %u ms...", nextRetryBackoff);
            vTaskDelay(pdMS_TO_TICKS(nextRetryBackoff));
        } else if (backoffStatus == BackoffAlgorithmRetriesExhausted) {
            ESP_LOGE(TAG, "All retry attempts exhausted");
            break;
        }
    }

    return connected;
}

// Tarea principal de AWS IoT
void aws_iot_task(void *param)
{
    ESP_LOGI(TAG, "AWS IoT Task started");

    // Esperar a que WiFi obtenga IP (típicamente toma ~6-8 segundos)
    ESP_LOGI(TAG, "Waiting for network to be ready...");
    vTaskDelay(pdMS_TO_TICKS(10000));

    // Inicializar contexto de red (solo una vez)
    if (!initialize_network_context()) {
        ESP_LOGE(TAG, "Failed to initialize network context. Exiting task.");
        vTaskDelete(NULL);
        return;
    }

    // Conectar con backoff exponencial
    if (!connect_with_backoff()) {
        ESP_LOGE(TAG, "Failed to connect after all retries. Exiting task.");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Connection established. Entering main loop...");

    sensor_data_t sensor_data;
    char json_payload[256];

    // Bucle principal: Publicar datos desde la cola
    uint32_t loop_count = 0;
    while (1) {
        // Intentar recibir datos de la cola (espera máximo 1 segundo)
        if (xQueueReceive(g_aws_queue, &sensor_data, pdMS_TO_TICKS(1000)) == pdTRUE) {
            ESP_LOGI(TAG, "Dato recibido de la cola");
            // Formatear JSON con los datos del sensor
            int len = snprintf(json_payload, sizeof(json_payload),
                "{\"id\":\"%s\",\"temp\":%.2f,\"hum\":%.2f,\"press\":%.2f,\"gas\":%.2f}",
                sensor_data.device_id,
                sensor_data.temperature,
                sensor_data.humidity,
                sensor_data.pressure,
                sensor_data.gas_concentration);

            if (len > 0 && len < sizeof(json_payload)) {
                ESP_LOGI(TAG, "Publishing: %s", json_payload);

                // Configurar información de publicación
                MQTTPublishInfo_t publishInfo;
                memset(&publishInfo, 0, sizeof(publishInfo));
                publishInfo.qos = MQTTQoS1;  // QoS 1: at least once
                publishInfo.retain = false;
                publishInfo.dup = false;
                publishInfo.pTopicName = MQTT_TOPIC;
                publishInfo.topicNameLength = strlen(MQTT_TOPIC);
                publishInfo.pPayload = json_payload;
                publishInfo.payloadLength = len;

                // Obtener un packet ID único
                uint16_t packetId = MQTT_GetPacketId(&mqttContext);

                // Publicar
                MQTTStatus_t mqttStatus = MQTT_Publish(&mqttContext, &publishInfo, packetId);

                if (mqttStatus != MQTTSuccess) {
                    ESP_LOGE(TAG, "MQTT_Publish failed with status: %d", mqttStatus);
                } else {
                    ESP_LOGI(TAG, "Published successfully with packet ID: %u", packetId);
                }
            } else {
                ESP_LOGW(TAG, "JSON payload too large or formatting error");
            }
        } else {
            // Solo loguear cada 30 segundos para no saturar logs
            loop_count++;
            if (loop_count % 30 == 0) {
                ESP_LOGI(TAG, "Esperando datos en la cola... (loop %lu)", (unsigned long)loop_count);
            }
        }

        // Procesar loop de MQTT para keep-alive y ACKs (especialmente PUBACK para QoS1)
        MQTTStatus_t mqttStatus = MQTT_ProcessLoop(&mqttContext);

        if (mqttStatus != MQTTSuccess && mqttStatus != MQTTNeedMoreBytes) {
            ESP_LOGW(TAG, "MQTT_ProcessLoop returned status: %d", mqttStatus);

            // Si hay un error crítico, intentar reconectar
            if (mqttStatus == MQTTSendFailed || mqttStatus == MQTTRecvFailed ||
                mqttStatus == MQTTBadResponse || mqttStatus == MQTTKeepAliveTimeout) {

                ESP_LOGE(TAG, "Connection lost! Attempting to reconnect...");

                // Desconectar limpiamente
                MQTT_Disconnect(&mqttContext);
                xTlsDisconnect(&networkContext);

                // Intentar reconectar con backoff
                if (connect_with_backoff()) {
                    ESP_LOGI(TAG, "Reconnected successfully!");
                } else {
                    ESP_LOGE(TAG, "Reconnection failed after all retries. Exiting task.");
                    vTaskDelete(NULL);
                    return;
                }
            }
        }
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
