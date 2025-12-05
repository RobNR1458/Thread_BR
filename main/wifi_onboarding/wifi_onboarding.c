#include "wifi_onboarding.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "lwip/inet.h"
#include "lwip/dns.h"
#include "dns_server.h"
#include "esp_ot_wifi_cmd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "wifi_onboarding";

// NVS keys for WiFi credentials
#define NVS_NAMESPACE "wifi_config"
#define NVS_KEY_SSID "ssid"
#define NVS_KEY_PASSWORD "password"
#define NVS_KEY_CONFIGURED "configured"

// AP configuration
#define AP_SSID "Thread_Border_Router"
#define AP_PASSWORD "practicum2"
#define AP_MAX_CONNECTIONS 4
#define AP_CHANNEL 6

// Static variables
static httpd_handle_t server = NULL;
static bool provisioning_done = false;
static bool s_wifi_connected = false;  // Track WiFi STA connection status

// Forward declarations
static esp_err_t root_handler(httpd_req_t *req);
static esp_err_t redirect_handler(httpd_req_t *req);
static esp_err_t scan_handler(httpd_req_t *req);
static esp_err_t provision_handler(httpd_req_t *req);
static esp_err_t status_handler(httpd_req_t *req);
static void restart_task(void *pvParameter);

// External HTML content (will be defined in separate file)
extern const char portal_html_start[] asm("_binary_portal_html_start");
extern const char portal_html_end[] asm("_binary_portal_html_end");

/**
 * Check if WiFi credentials exist in NVS
 */
bool wifi_onboarding_has_credentials(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return false;
    }

    uint8_t configured = 0;
    err = nvs_get_u8(nvs_handle, NVS_KEY_CONFIGURED, &configured);
    nvs_close(nvs_handle);

    return (err == ESP_OK && configured == 1);
}

/**
 * Save WiFi credentials to NVS
 */
static esp_err_t save_wifi_credentials(const char *ssid, const char *password)
{
    nvs_handle_t nvs_handle;
    esp_err_t err;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_str(nvs_handle, NVS_KEY_SSID, ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save SSID: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_set_str(nvs_handle, NVS_KEY_PASSWORD, password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save password: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_set_u8(nvs_handle, NVS_KEY_CONFIGURED, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set configured flag: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "WiFi credentials saved successfully");
    }

    return err;
}

/**
 * Read WiFi credentials from NVS
 */
static esp_err_t read_wifi_credentials(char *ssid, size_t ssid_len, char *password, size_t pass_len)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_str(nvs_handle, NVS_KEY_SSID, ssid, &ssid_len);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }

    err = nvs_get_str(nvs_handle, NVS_KEY_PASSWORD, password, &pass_len);
    nvs_close(nvs_handle);

    return err;
}

/**
 * Clear WiFi credentials from NVS
 */
esp_err_t wifi_onboarding_clear_credentials(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    nvs_erase_key(nvs_handle, NVS_KEY_SSID);
    nvs_erase_key(nvs_handle, NVS_KEY_PASSWORD);
    nvs_erase_key(nvs_handle, NVS_KEY_CONFIGURED);
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);

    ESP_LOGI(TAG, "WiFi credentials cleared");
    return err;
}

/**
 * HTTP handler for root path (serves portal HTML)
 */
static esp_err_t root_handler(httpd_req_t *req)
{
    const size_t html_len = portal_html_end - portal_html_start;

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate");
    httpd_resp_send(req, portal_html_start, html_len);

    return ESP_OK;
}

/**
 * HTTP handler for captive portal redirect
 * Handles connectivity check URLs from Android/iOS
 */
static esp_err_t redirect_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Captive portal redirect from: %s", req->uri);

    // Send 302 redirect to root
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);

    return ESP_OK;
}

/**
 * HTTP handler for WiFi scan
 */
static esp_err_t scan_handler(httpd_req_t *req)
{
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false
    };

    ESP_LOGI(TAG, "Starting WiFi scan...");
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Scan failed");
        return ESP_FAIL;
    }

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);

    if (ap_count == 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"networks\":[]}");
        return ESP_OK;
    }

    // Limit to 20 networks to avoid buffer overflow
    if (ap_count > 20) {
        ap_count = 20;
    }

    wifi_ap_record_t *ap_records = malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (ap_records == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }

    esp_wifi_scan_get_ap_records(&ap_count, ap_records);

    // Build JSON response
    char *json = malloc(4096);  // Allocate buffer for JSON
    if (json == NULL) {
        free(ap_records);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Memory allocation failed");
        return ESP_FAIL;
    }

    int offset = sprintf(json, "{\"networks\":[");

    for (int i = 0; i < ap_count; i++) {
        if (i > 0) {
            offset += sprintf(json + offset, ",");
        }
        offset += sprintf(json + offset,
            "{\"ssid\":\"%s\",\"rssi\":%d,\"auth\":%d}",
            ap_records[i].ssid,
            ap_records[i].rssi,
            ap_records[i].authmode
        );
    }

    offset += sprintf(json + offset, "]}");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json);

    free(json);
    free(ap_records);

    ESP_LOGI(TAG, "Scan complete, found %d networks", ap_count);
    return ESP_OK;
}

/**
 * HTTP handler for WiFi provisioning
 */
static esp_err_t provision_handler(httpd_req_t *req)
{
    char content[512];
    int ret = httpd_req_recv(req, content, sizeof(content) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
        return ESP_FAIL;
    }
    content[ret] = '\0';

    // Parse JSON manually (simple approach)
    char ssid[33] = {0};
    char password[64] = {0};

    // Extract SSID
    char *ssid_start = strstr(content, "\"ssid\":\"");
    if (ssid_start) {
        ssid_start += 8;  // Skip past "ssid":"
        char *ssid_end = strchr(ssid_start, '"');
        if (ssid_end) {
            size_t ssid_len = ssid_end - ssid_start;
            if (ssid_len < sizeof(ssid)) {
                strncpy(ssid, ssid_start, ssid_len);
                ssid[ssid_len] = '\0';
            }
        }
    }

    // Extract password
    char *pass_start = strstr(content, "\"password\":\"");
    if (pass_start) {
        pass_start += 12;  // Skip past "password":"
        char *pass_end = strchr(pass_start, '"');
        if (pass_end) {
            size_t pass_len = pass_end - pass_start;
            if (pass_len < sizeof(password)) {
                strncpy(password, pass_start, pass_len);
                password[pass_len] = '\0';
            }
        }
    }

    ESP_LOGI(TAG, "Received credentials - SSID: %s", ssid);

    if (strlen(ssid) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "SSID required");
        return ESP_FAIL;
    }

    // Try to connect to WiFi
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));

    // Save credentials first
    esp_err_t err = save_wifi_credentials(ssid, password);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save credentials");
        return ESP_FAIL;
    }

    // Mark provisioning as done
    provisioning_done = true;

    // Send success response
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"success\",\"message\":\"Credentials saved. Device will restart.\"}");

    ESP_LOGI(TAG, "Provisioning successful, will restart in 3 seconds...");

    // Create task to restart ESP32 after delay
    xTaskCreate(restart_task, "restart", 2048, NULL, 5, NULL);

    return ESP_OK;
}

/**
 * Task to restart ESP32 after delay
 */
static void restart_task(void *pvParameter)
{
    ESP_LOGI(TAG, "Restarting in 3 seconds...");
    vTaskDelay(pdMS_TO_TICKS(3000));
    ESP_LOGI(TAG, "Restarting now!");
    esp_restart();
}

/**
 * HTTP handler for status check
 */
static esp_err_t status_handler(httpd_req_t *req)
{
    char response[128];
    sprintf(response, "{\"provisioned\":%s}", provisioning_done ? "true" : "false");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response);

    return ESP_OK;
}

/**
 * Start HTTP server for captive portal
 */
static esp_err_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;
    config.max_uri_handlers = 16;  // Increased for captive portal URLs
    config.stack_size = 8192;

    ESP_LOGI(TAG, "Starting HTTP server on port 80");

    if (httpd_start(&server, &config) == ESP_OK) {
        // Register URI handlers
        httpd_uri_t root_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = root_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &root_uri);

        httpd_uri_t scan_uri = {
            .uri = "/scan",
            .method = HTTP_GET,
            .handler = scan_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &scan_uri);

        httpd_uri_t provision_uri = {
            .uri = "/provision",
            .method = HTTP_POST,
            .handler = provision_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &provision_uri);

        httpd_uri_t status_uri = {
            .uri = "/status",
            .method = HTTP_GET,
            .handler = status_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &status_uri);

        // Captive portal detection URLs
        // Android connectivity checks
        httpd_uri_t generate_204_uri = {
            .uri = "/generate_204",
            .method = HTTP_GET,
            .handler = redirect_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &generate_204_uri);

        httpd_uri_t gen_204_uri = {
            .uri = "/gen_204",
            .method = HTTP_GET,
            .handler = redirect_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &gen_204_uri);

        // iOS/Apple connectivity checks
        httpd_uri_t hotspot_detect_uri = {
            .uri = "/hotspot-detect.html",
            .method = HTTP_GET,
            .handler = redirect_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &hotspot_detect_uri);

        // Windows connectivity check
        httpd_uri_t ncsi_uri = {
            .uri = "/ncsi.txt",
            .method = HTTP_GET,
            .handler = redirect_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &ncsi_uri);

        // Generic fallback for any other path
        httpd_uri_t catch_all_uri = {
            .uri = "/*",
            .method = HTTP_GET,
            .handler = redirect_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &catch_all_uri);

        ESP_LOGI(TAG, "HTTP server started successfully");
        ESP_LOGI(TAG, "Captive portal handlers registered");
        return ESP_OK;
    }

    ESP_LOGE(TAG, "Failed to start HTTP server");
    return ESP_FAIL;
}

/**
 * WiFi event handler
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "Station connected, MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5]);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "Station disconnected, MAC: %02x:%02x:%02x:%02x:%02x:%02x",
                 event->mac[0], event->mac[1], event->mac[2],
                 event->mac[3], event->mac[4], event->mac[5]);
    }
}

/**
 * Start WiFi onboarding in AP mode
 */
esp_err_t wifi_onboarding_start(void)
{
    ESP_LOGI(TAG, "Starting WiFi onboarding in AP mode...");

    // Initialize WiFi in AP mode
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handler
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    // Use fixed SSID
    wifi_config_t ap_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .channel = AP_CHANNEL,
            .password = AP_PASSWORD,
            .max_connection = AP_MAX_CONNECTIONS,
            .authmode = WIFI_AUTH_WPA2_PSK,  // Change to WIFI_AUTH_OPEN for no password
            .pmf_cfg = {
                .required = false,
            },
        },
    };

    // If you want open AP, uncomment this:
    // ap_config.ap.authmode = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));  // AP + STA for scanning
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi AP started: SSID=%s, Password=%s", AP_SSID, AP_PASSWORD);

    // Start DNS server for captive portal
    dns_server_start();

    // Start HTTP server
    start_webserver();

    ESP_LOGI(TAG, "Captive portal ready at http://192.168.4.1");

    return ESP_OK;
}

/**
 * Stop WiFi onboarding
 */
esp_err_t wifi_onboarding_stop(void)
{
    ESP_LOGI(TAG, "Stopping WiFi onboarding...");

    if (server) {
        httpd_stop(server);
        server = NULL;
    }

    dns_server_stop();

    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);

    return ESP_OK;
}

// Event handler for WiFi station connection
static void wifi_sta_event_handler(void *arg, esp_event_base_t event_base,
                                     int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi station started, connecting...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_connected = false;  // Mark as disconnected
        ESP_LOGW(TAG, "WiFi disconnected, retrying...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        s_wifi_connected = true;  // Mark as connected
        ESP_LOGI(TAG, "WiFi connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

/**
 * Connect to WiFi using stored credentials
 */
esp_err_t wifi_onboarding_connect(void)
{
    char ssid[33] = {0};
    char password[64] = {0};

    esp_err_t err = read_wifi_credentials(ssid, sizeof(ssid), password, sizeof(password));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read WiFi credentials: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Connecting to WiFi: %s", ssid);

    // Initialize WiFi in STA mode
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init WiFi: %s", esp_err_to_name(err));
        return err;
    }

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                &wifi_sta_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                &wifi_sta_event_handler, NULL));

    // Configure WiFi
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi connection initiated");
    return ESP_OK;
}

/**
 * Check if WiFi is currently connected (has obtained IP)
 */
bool wifi_onboarding_is_connected(void)
{
    return s_wifi_connected;
}
