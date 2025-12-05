#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_openthread.h"
#include "esp_openthread_border_router.h"
#include "esp_openthread_types.h"
#include "esp_ot_config.h"
#include "esp_ot_ota_commands.h"
#include "esp_ot_wifi_cmd.h"
#include "esp_spiffs.h"
#include "esp_vfs_eventfd.h"
#include "mdns.h"
#include "nvs_flash.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_partition.h"
#include "freertos/queue.h"
#include "shared_data.h"
#include "wifi_onboarding/wifi_onboarding.h"
#include "border_router_launch.h"
#include "wifi_reset_cmd.h"
#include "wifi_connectivity_watchdog.h"

#define TAG "esp_ot_br"

// Global AWS queue definition
QueueHandle_t g_aws_queue = NULL;

// Declaración de funciones externas
extern void start_aws_client(void);
extern void start_thread_coap_server(void);


static esp_err_t init_spiffs(void)
{
#if CONFIG_AUTO_UPDATE_RCP
    esp_vfs_spiffs_conf_t rcp_fw_conf = {.base_path = "/" CONFIG_RCP_PARTITION_NAME,
                                         .partition_label = CONFIG_RCP_PARTITION_NAME,
                                         .max_files = 10,
                                         .format_if_mount_failed = false};
    ESP_RETURN_ON_ERROR(esp_vfs_spiffs_register(&rcp_fw_conf), TAG, "Failed to mount rcp firmware storage");
#endif
    return ESP_OK;
}

void app_main(void)
{
    // Used eventfds:
    // * netif
    // * task queue
    // * border router
    // * discovery delegate (WiFi)
    // * additional WiFi events
    size_t max_eventfd = 6;
    esp_vfs_eventfd_config_t eventfd_config = {
        .max_fds = max_eventfd,
    };

    ESP_ERROR_CHECK(esp_vfs_eventfd_register(&eventfd_config));
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(init_spiffs());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create AWS queue for sensor data before starting tasks
    g_aws_queue = xQueueCreate(10, sizeof(sensor_data_t));
    if (g_aws_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create AWS queue");
        abort();
    }
    ESP_LOGI(TAG, "AWS queue created successfully (capacity: 10)");

    // ========== WiFi Onboarding Logic ==========
    if (!wifi_onboarding_has_credentials()) {
        ESP_LOGW(TAG, "No WiFi credentials found - Starting AP mode for configuration");
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "====================================================");
        ESP_LOGI(TAG, "  FIRST TIME SETUP - WiFi Configuration Required");
        ESP_LOGI(TAG, "====================================================");
        ESP_LOGI(TAG, "1. Connect your phone/laptop to WiFi: Thread_Border_Router");
        ESP_LOGI(TAG, "2. Password: practicum2");
        ESP_LOGI(TAG, "3. Portal will open automatically (or go to http://192.168.4.1)");
        ESP_LOGI(TAG, "4. Select your WiFi network and enter password");
        ESP_LOGI(TAG, "5. Device will restart and connect to your WiFi");
        ESP_LOGI(TAG, "====================================================");
        ESP_LOGI(TAG, "");

        // Start WiFi onboarding (AP mode + captive portal)
        ESP_ERROR_CHECK(wifi_onboarding_start());

        // Wait in loop until credentials are configured
        // The device will restart after successful configuration
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // WiFi credentials exist - proceed with normal operation
    ESP_LOGI(TAG, "WiFi credentials found - connecting...");
    ESP_ERROR_CHECK(wifi_onboarding_connect());

    // Initialize mDNS after WiFi connection
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set("esp-ot-br"));

    // Start CoAP server for receiving Thread sensor data
    ESP_LOGI(TAG, "Starting Thread CoAP server...");
    start_thread_coap_server();

    // Start AWS IoT client for cloud publishing
    ESP_LOGI(TAG, "Starting AWS IoT client...");
    start_aws_client();

    // Start WiFi connectivity watchdog (auto-reset WiFi if no internet for 2 minutes)
    ESP_LOGI(TAG, "Starting WiFi connectivity watchdog...");
    start_wifi_connectivity_watchdog();

    // Launch OpenThread Border Router
    esp_openthread_platform_config_t platform_config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    esp_rcp_update_config_t rcp_update_config = ESP_OPENTHREAD_RCP_UPDATE_CONFIG();

    launch_openthread_border_router(&platform_config, &rcp_update_config);
}