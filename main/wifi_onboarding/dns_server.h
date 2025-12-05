#ifndef DNS_SERVER_H
#define DNS_SERVER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start DNS server for captive portal
 *
 * Redirects all DNS queries to 192.168.4.1 (AP IP)
 *
 * @return ESP_OK on success
 */
esp_err_t dns_server_start(void);

/**
 * @brief Stop DNS server
 *
 * @return ESP_OK on success
 */
esp_err_t dns_server_stop(void);

#ifdef __cplusplus
}
#endif

#endif // DNS_SERVER_H
