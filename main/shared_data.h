#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Estructura de los datos que vienen de los sensores
typedef struct {
    char device_id[16];
    float temperature;
    float pressure;
    float humidity;
    float gas_concentration;
} sensor_data_t;

// Referencia externa a la cola, la cual se define en Thread_BR.c
extern QueueHandle_t g_aws_queue;