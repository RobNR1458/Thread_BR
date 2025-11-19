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
#include "border_router_launch.h"
#include "esp_br_web.h"
#include "esp_partition.h"

#define TAG "esp_ot_br"


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
    size_t max_eventfd = 3;
    esp_vfs_eventfd_config_t eventfd_config = {
        .max_fds = max_eventfd,
    };

    esp_openthread_platform_config_t platform_config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    esp_rcp_update_config_t rcp_update_config = ESP_OPENTHREAD_RCP_UPDATE_CONFIG();
    ESP_ERROR_CHECK(esp_vfs_eventfd_register(&eventfd_config));
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(init_spiffs());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set("esp-ot-br"));
    
    launch_openthread_border_router(&platform_config, &rcp_update_config);
}