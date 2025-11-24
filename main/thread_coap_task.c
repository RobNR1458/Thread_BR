#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "openthread/coap.h"
#include "openthread/ip6.h"
#include "openthread/thread.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "shared_data.h"

static const char *TAG = "THREAD_COAP";

// Esta función se ejecuta cada vez que llega un mensaje CoAP
static void coap_handler(void *aContext, otMessage *aMessage, const otMessageInfo *aMessageInfo)
{
    sensor_data_t received_data;
    uint16_t length = otMessageGetLength(aMessage) - otMessageGetOffset(aMessage);

    ESP_LOGI(TAG, ">>> HANDLER COAP EJECUTADO! Mensaje recibido con %d bytes <<<", length);

    // Verificar que el tamaño del mensaje sea correcto
    if (length < sizeof(sensor_data_t)) {
        ESP_LOGE(TAG, "Payload muy pequeño (%d bytes, esperado %d)", length, sizeof(sensor_data_t));
        return;
    }

    // 1. Leer el mensaje
    if (otMessageRead(aMessage, otMessageGetOffset(aMessage), &received_data, sizeof(received_data)) != length) {
        ESP_LOGE(TAG, "Error leyendo mensaje CoAP");
        return;
    }

    ESP_LOGI(TAG, ">>> Recibido de Thread: ID=%s, Temp=%.2f, Hum=%.2f, Press=%.2f, Gas=%.2f",
             received_data.device_id, received_data.temperature,
             received_data.humidity, received_data.pressure, received_data.gas_concentration);

    // 2. Enviar a la cola de AWS
    if (g_aws_queue != NULL) {
        if (xQueueSend(g_aws_queue, &received_data, 0) != pdTRUE) {
            ESP_LOGW(TAG, "Cola AWS llena, descartando dato");
        } else {
            ESP_LOGI(TAG, "Dato enviado a cola AWS exitosamente");
        }
    } else {
        ESP_LOGE(TAG, "ERROR: g_aws_queue es NULL - la cola no fue inicializada");
    }

    // 3. Responder con ACK (Opcional pero recomendado)
    if (aMessageInfo != NULL) {
         // Aquí iría la lógica simple de respuesta (omitiendo para brevedad)
         (void)aContext; // Suprimir advertencia de variable no usada
    }
}

// Tarea que espera a que OpenThread esté operativo y registra el servidor CoAP
static void coap_server_task(void *pvParameters)
{
    otInstance *instance = NULL;
    otDeviceRole role = OT_DEVICE_ROLE_DISABLED;

    ESP_LOGI(TAG, "Tarea CoAP iniciada, esperando a que OpenThread esté listo...");

    // Paso 1: Esperar hasta que OpenThread esté completamente inicializado
    while (instance == NULL) {
        vTaskDelay(pdMS_TO_TICKS(500));
        instance = esp_openthread_get_instance();
    }

    ESP_LOGI(TAG, "Instancia de OpenThread obtenida");

    // Paso 2: Esperar hasta que OpenThread tenga un rol activo (child, router o leader)
    ESP_LOGI(TAG, "Esperando a que OpenThread tenga rol activo...");
    while (role == OT_DEVICE_ROLE_DISABLED || role == OT_DEVICE_ROLE_DETACHED) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_openthread_lock_acquire(portMAX_DELAY);
        role = otThreadGetDeviceRole(instance);
        esp_openthread_lock_release();

        if (role == OT_DEVICE_ROLE_DISABLED || role == OT_DEVICE_ROLE_DETACHED) {
            ESP_LOGI(TAG, "Rol actual: %d (esperando rol activo...)", role);
        }
    }

    ESP_LOGI(TAG, "OpenThread ahora tiene rol activo: %d", role);

    // Paso 3: Iniciar el servidor CoAP
    esp_openthread_lock_acquire(portMAX_DELAY);

    otError error = otCoapStart(instance, OT_DEFAULT_COAP_PORT);
    if (error != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "ERROR: No se pudo iniciar el stack CoAP (error %d)", error);
        esp_openthread_lock_release();
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "Stack CoAP iniciado en puerto %d", OT_DEFAULT_COAP_PORT);

    // Paso 4: Registrar el recurso 'sensordata'
    static otCoapResource s_resource;

    memset(&s_resource, 0, sizeof(s_resource));
    s_resource.mUriPath = "sensordata";
    s_resource.mHandler = coap_handler;
    s_resource.mContext = instance;

    otCoapAddResource(instance, &s_resource);

    ESP_LOGI(TAG, "Recurso CoAP 'sensordata' registrado correctamente");

    // Paso 5: Mostrar las direcciones IPv6 Thread donde está escuchando
    const otNetifAddress *addr = otIp6GetUnicastAddresses(instance);
    ESP_LOGI(TAG, "CoAP server escuchando en puerto %d en las siguientes direcciones:", OT_DEFAULT_COAP_PORT);

    while (addr != NULL) {
        const otIp6Address *ip6 = &addr->mAddress;

        // Formatear dirección IPv6
        char addr_str[40];
        snprintf(addr_str, sizeof(addr_str),
                 "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
                 ip6->mFields.m8[0], ip6->mFields.m8[1],
                 ip6->mFields.m8[2], ip6->mFields.m8[3],
                 ip6->mFields.m8[4], ip6->mFields.m8[5],
                 ip6->mFields.m8[6], ip6->mFields.m8[7],
                 ip6->mFields.m8[8], ip6->mFields.m8[9],
                 ip6->mFields.m8[10], ip6->mFields.m8[11],
                 ip6->mFields.m8[12], ip6->mFields.m8[13],
                 ip6->mFields.m8[14], ip6->mFields.m8[15]);

        ESP_LOGI(TAG, "  coap://[%s]:5683/sensordata", addr_str);

        addr = addr->mNext;
    }

    esp_openthread_lock_release();

    ESP_LOGI(TAG, "=== Servidor CoAP completamente inicializado y LISTO para recibir mensajes ===");

    // La tarea termina, pero el servidor CoAP sigue corriendo en el stack de OpenThread
    vTaskDelete(NULL);
}

// Función pública para iniciar el servidor (crea una tarea)
void start_thread_coap_server(void)
{
    xTaskCreate(coap_server_task, "coap_server", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "Tarea del servidor CoAP creada");
}