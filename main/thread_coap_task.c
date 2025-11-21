#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_openthread.h"
#include "openthread/coap.h"
#include "shared_data.h"

static const char *TAG = "THREAD_COAP";

// Esta función se ejecuta cada vez que llega un mensaje CoAP
static void coap_handler(void *aContext, otMessage *aMessage, const otMessageInfo *aMessageInfo)
{
    sensor_data_t received_data;
    uint16_t length = otMessageGetLength(aMessage) - otMessageGetOffset(aMessage);

    // 1. Leer el mensaje
    if (otMessageRead(aMessage, otMessageGetOffset(aMessage), &received_data, sizeof(received_data)) != length) {
        ESP_LOGE(TAG, "Error leyendo mensaje CoAP");
        return;
    }

    ESP_LOGI(TAG, "Recibido de Thread: ID=%s, Temp=%.2f", received_data.device_id, received_data.temperature);

    // 2. Enviar a la cola de AWS
    if (g_aws_queue != NULL) {
        if (xQueueSend(g_aws_queue, &received_data, 0) != pdTRUE) {
            ESP_LOGW(TAG, "Cola AWS llena, descartando dato");
        }
    }

    // 3. Responder con ACK (Opcional pero recomendado)
    if (aMessageInfo != NULL) {
         // Aquí iría la lógica simple de respuesta (omitiendo para brevedad)
         (void)aContext; // Suprimir advertencia de variable no usada
    }
}

// Función pública para iniciar el servidor
void start_thread_coap_server(void)
{
    otInstance *instance = esp_openthread_get_instance();
    static otCoapResource s_resource;
    
    memset(&s_resource, 0, sizeof(s_resource));
    s_resource.mUriPath = "sensor/data";
    s_resource.mHandler = coap_handler;
    s_resource.mContext = instance;

    otCoapAddResource(instance, &s_resource);
    ESP_LOGI(TAG, "Recurso CoAP 'sensor/data' iniciado correctamente");
}