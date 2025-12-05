#ifndef WIFI_ONBOARDING_H
#define WIFI_ONBOARDING_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check if WiFi credentials are stored in NVS
 *
 * @return true if credentials exist, false otherwise
 */
bool wifi_onboarding_has_credentials(void);

/**
 * @brief Start WiFi onboarding in Access Point mode
 *
 * Creates a WiFi AP called "Thread-BR-Setup-XXXX" and starts
 * a captive portal web server for WiFi configuration.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_onboarding_start(void);

/**
 * @brief Stop WiFi onboarding and shutdown AP mode
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_onboarding_stop(void);

/**
 * @brief Connect to WiFi using stored credentials
 *
 * Reads credentials from NVS and connects to WiFi network
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_onboarding_connect(void);

/**
 * @brief Clear stored WiFi credentials from NVS
 *
 * Useful for factory reset or re-configuration
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t wifi_onboarding_clear_credentials(void);

/**
 * @brief Check if WiFi is currently connected
 *
 * Returns true once IP address has been obtained from DHCP
 *
 * @return true if connected with IP, false otherwise
 */
bool wifi_onboarding_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_ONBOARDING_H
