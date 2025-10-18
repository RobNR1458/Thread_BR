#include <stdio.h>
#include "driver/ledc.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void example_ledc_init(void){
     // Sección de inicialización del temporizador LEDC
    const ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .duty_resolution  = LEDC_TIMER_13_BIT,
        .timer_num        = LEDC_TIMER_0,
        .freq_hz          = 5000,  // Frequency in Hertz. Set frequency at 5 kHz
        .clk_cfg          = LEDC_AUTO_CLK // 80 MHz source clock
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Sección de inicialización del canal LEDC
    const ledc_channel_config_t ledc_channel = {
    .speed_mode     = LEDC_LOW_SPEED_MODE,
    .channel        = LEDC_CHANNEL_0,
    .timer_sel      = LEDC_TIMER_0,
    .intr_type      = LEDC_INTR_DISABLE,
    .gpio_num       = 8, // GPIO pin number
    .duty           = 0, // Set duty to 0%
    .hpoint         = 0
};
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

}
   

void app_main(void)
{

    // Inicializar el LEDC
    example_ledc_init();

    // Bucle principal
    while (1) {
        // Aumentar el brillo del LED
        for (int duty = 0; duty <= 8191; duty += 128) {
            ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty));
            ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        printf("Brillo máximo alcanzado!\n");
        // Disminuir el brillo del LED
        for (int duty = 8191; duty >= 0; duty -= 128) {
            ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty));
            ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        printf("Brillo mínimo alcanzado!\n");
    }
}
