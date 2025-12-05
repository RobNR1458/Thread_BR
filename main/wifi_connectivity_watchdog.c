#include "wifi_connectivity_watchdog.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_ping.h"
#include "ping/ping_sock.h"
#include "wifi_onboarding/wifi_onboarding.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "wifi_watchdog";

// Configuration
#define WATCHDOG_CHECK_INTERVAL_MS   30000  // Check every 30 seconds
#define WATCHDOG_TIMEOUT_MS          120000 // Reset after 2 minutes (120 seconds) without connectivity
#define PING_TARGET_IP               "8.8.8.8"  // Google DNS
#define PING_TIMEOUT_MS              5000

static bool s_connectivity_ok = false;
static uint32_t s_no_connectivity_time_ms = 0;

// Ping callback
static void on_ping_success(esp_ping_handle_t hdl, void *args)
{
    s_connectivity_ok = true;
    ESP_LOGI(TAG, "Internet connectivity verified (ping successful)");
}

static void on_ping_timeout(esp_ping_handle_t hdl, void *args)
{
    s_connectivity_ok = false;
    ESP_LOGW(TAG, "Ping timeout - no internet connectivity");
}

static void on_ping_end(esp_ping_handle_t hdl, void *args)
{
    esp_ping_delete_session(hdl);
}

// Check internet connectivity by pinging Google DNS
static bool check_internet_connectivity(void)
{
    // Reset flag
    s_connectivity_ok = false;

    // Check if WiFi is connected first
    if (!wifi_onboarding_is_connected()) {
        ESP_LOGW(TAG, "WiFi not connected, skipping ping");
        return false;
    }

    // Configure ping
    esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
    ping_config.target_addr.u_addr.ip4.addr = ipaddr_addr(PING_TARGET_IP);
    ping_config.target_addr.type = IPADDR_TYPE_V4;
    ping_config.count = 1;  // Single ping
    ping_config.interval_ms = 1000;
    ping_config.timeout_ms = PING_TIMEOUT_MS;

    // Set callbacks
    esp_ping_callbacks_t cbs = {
        .on_ping_success = on_ping_success,
        .on_ping_timeout = on_ping_timeout,
        .on_ping_end = on_ping_end,
        .cb_args = NULL
    };

    esp_ping_handle_t ping;
    esp_err_t err = esp_ping_new_session(&ping_config, &cbs, &ping);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ping session: %s", esp_err_to_name(err));
        return false;
    }

    // Start ping
    err = esp_ping_start(ping);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start ping: %s", esp_err_to_name(err));
        esp_ping_delete_session(ping);
        return false;
    }

    // Wait for ping to complete (timeout + small margin)
    vTaskDelay(pdMS_TO_TICKS(PING_TIMEOUT_MS + 1000));

    return s_connectivity_ok;
}

// WiFi connectivity watchdog task
static void wifi_watchdog_task(void *param)
{
    ESP_LOGI(TAG, "WiFi connectivity watchdog started");
    ESP_LOGI(TAG, "Will reset WiFi credentials after %d seconds without connectivity",
             WATCHDOG_TIMEOUT_MS / 1000);

    // Wait a bit for WiFi to attempt connection
    ESP_LOGI(TAG, "Giving WiFi 30 seconds to establish initial connection...");
    vTaskDelay(pdMS_TO_TICKS(30000));

    // Check if WiFi connected during that time
    if (!wifi_onboarding_is_connected()) {
        ESP_LOGW(TAG, "WiFi did not connect in 30 seconds, starting watchdog monitoring");
    } else {
        ESP_LOGI(TAG, "WiFi connected successfully, monitoring internet connectivity");
        // Give some time for network to stabilize
        vTaskDelay(pdMS_TO_TICKS(10000));
    }

    s_no_connectivity_time_ms = 0;
    uint32_t check_count = 0;

    while (1) {
        check_count++;

        // Check WiFi connection status first
        bool wifi_connected = wifi_onboarding_is_connected();
        bool has_internet = false;

        if (wifi_connected) {
            // WiFi is connected, check internet connectivity
            has_internet = check_internet_connectivity();
        } else {
            // WiFi is not connected at all
            ESP_LOGW(TAG, "WiFi not connected (attempting to reconnect...)");
        }

        if (has_internet) {
            // Reset counter on successful connectivity
            if (s_no_connectivity_time_ms > 0) {
                ESP_LOGI(TAG, "Internet connectivity restored!");
            }
            s_no_connectivity_time_ms = 0;
        } else {
            // Increment no-connectivity time (either WiFi disconnected OR no internet)
            s_no_connectivity_time_ms += WATCHDOG_CHECK_INTERVAL_MS;

            uint32_t seconds_without_connectivity = s_no_connectivity_time_ms / 1000;

            if (wifi_connected) {
                ESP_LOGW(TAG, "WiFi connected but no internet for %lu seconds (timeout at %d seconds)",
                         (unsigned long)seconds_without_connectivity, WATCHDOG_TIMEOUT_MS / 1000);
            } else {
                ESP_LOGW(TAG, "WiFi disconnected for %lu seconds (timeout at %d seconds)",
                         (unsigned long)seconds_without_connectivity, WATCHDOG_TIMEOUT_MS / 1000);
            }

            // Check if timeout reached
            if (s_no_connectivity_time_ms >= WATCHDOG_TIMEOUT_MS) {
                ESP_LOGE(TAG, "===================================================");
                ESP_LOGE(TAG, "  WiFi CONNECTIVITY TIMEOUT!");
                ESP_LOGE(TAG, "===================================================");
                ESP_LOGE(TAG, "No internet for %d seconds", WATCHDOG_TIMEOUT_MS / 1000);
                ESP_LOGE(TAG, "Clearing WiFi credentials and restarting...");
                ESP_LOGE(TAG, "Device will enter AP mode for reconfiguration");
                ESP_LOGE(TAG, "===================================================");

                // Clear WiFi credentials
                esp_err_t err = wifi_onboarding_clear_credentials();
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to clear credentials: %s", esp_err_to_name(err));
                }

                // Wait a moment for logs to flush
                vTaskDelay(pdMS_TO_TICKS(2000));

                // Restart device
                esp_restart();
            }
        }

        // Log summary every 10 checks (5 minutes if check interval is 30s)
        if (check_count % 10 == 0) {
            if (s_no_connectivity_time_ms == 0) {
                ESP_LOGI(TAG, "WiFi connectivity: OK (checked %lu times)",
                         (unsigned long)check_count);
            }
        }

        // Wait before next check
        vTaskDelay(pdMS_TO_TICKS(WATCHDOG_CHECK_INTERVAL_MS));
    }

    vTaskDelete(NULL);
}

// Start the WiFi connectivity watchdog
void start_wifi_connectivity_watchdog(void)
{
    xTaskCreate(wifi_watchdog_task, "wifi_watchdog", 4096, NULL, 3, NULL);
    ESP_LOGI(TAG, "WiFi connectivity watchdog task created");
}
