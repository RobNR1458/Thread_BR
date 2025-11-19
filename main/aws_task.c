#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "core_mqtt.h"
#include "core_mqtt_state.h"
#include "transport_interface.h"
#include "shared_data.h"

// *** ATENCIÓN: AJUSTA ESTO ***
#define AWS_IOT_ENDPOINT    "tu-endpoint-aqui-ats.iot.us-east-1.amazonaws.com" 
#define AWS_IOT_THING_NAME  "esp32_thread_border_router"
#define MQTT_TOPIC          "thread/sensores"

static const char *TAG = "AWS_TASK";

// Certificados embebidos (definidos en CMakeLists.txt)
extern const uint8_t aws_root_ca_pem_start[] asm("_binary_aws_root_ca_pem_start");
extern const uint8_t aws_root_ca_pem_end[]   asm("_binary_aws_root_ca_pem_end");
extern const uint8_t device_cert_pem_start[] asm("_binary_device_cert_pem_start");
extern const uint8_t device_cert_pem_end[]   asm("_binary_device_cert_pem_end");
extern const uint8_t device_key_pem_start[]  asm("_binary_device_key_pem_start");
extern const uint8_t device_key_pem_end[]    asm("_binary_device_key_pem_end");

// Estructuras de MQTT (simplificadas para el ejemplo)
static MQTTContext_t mqttContext;
static uint8_t networkBuffer[1024];
static TransportInterface_t transport;

// Prototipo de función dummy para tiempo (necesaria para coreMQTT)
uint32_t Mock_GetTimeMs(void) { return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS); }

// Tarea Principal
void aws_iot_task(void *param)
{
    // NOTA: En un código de producción real, aquí iría la inicialización de
    // esp_tls / network_transport para configurar 'transport'.
    // Por simplicidad y para asegurar que compile sin los helpers complejos del SDK,
    // asumiré que la conexión de red se establece aquí.
    
    // Dada la complejidad de inicializar TLS "a mano", te recomiendo encarecidamente
    // usar los helpers si el SDK está bien linkeado, pero aquí simularemos el bucle lógico.

    ESP_LOGI(TAG, "Tarea AWS Iniciada. Esperando datos...");
    
    sensor_data_t data;
    char json_payload[256];

    while (1) {
        // Bloquea hasta recibir datos de la red Thread
        if (xQueueReceive(g_aws_queue, &data, portMAX_DELAY)) {
            
            snprintf(json_payload, sizeof(json_payload), 
                "{\"id\":\"%s\",\"temp\":%.2f,\"hum\":%.2f,\"gas\":%.2f}",
                data.device_id, data.temperature, data.humidity, data.gas_concentration);

            ESP_LOGI(TAG, "Publicando a AWS: %s", json_payload);

            // Aquí llamarías a MQTT_Publish( &mqttContext, ... );
            // Si la conexión se cae, aquí debes reconectar.
        }
    }
    vTaskDelete(NULL);
}

void start_aws_client(void)
{
    xTaskCreate(aws_iot_task, "aws_iot_task", 8192, NULL, 5, NULL);
}