#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Estructura de los datos que vienen de tus sensores
typedef struct {
    char device_id[16];
    float temperature;
    float pressure;
    float humidity;
    float gas_concentration;
} sensor_data_t;

// Referencia externa a la cola (definida en app_main.c)
extern QueueHandle_t g_aws_queue;