#include "dns_server.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "dns_server";

#define DNS_SERVER_PORT 53
#define DNS_MAX_PACKET_SIZE 512
#define AP_IP_ADDRESS "192.168.4.1"

static int dns_socket = -1;
static TaskHandle_t dns_task_handle = NULL;
static bool dns_running = false;

/**
 * DNS header structure
 */
typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__((packed)) dns_header_t;

/**
 * DNS task - handles all DNS queries
 */
static void dns_server_task(void *pvParameters)
{
    char rx_buffer[DNS_MAX_PACKET_SIZE];
    char tx_buffer[DNS_MAX_PACKET_SIZE];
    struct sockaddr_in server_addr;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    // Create UDP socket
    dns_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (dns_socket < 0) {
        ESP_LOGE(TAG, "Unable to create socket");
        vTaskDelete(NULL);
        return;
    }

    // Bind to DNS port
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(DNS_SERVER_PORT);

    if (bind(dns_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Socket bind failed");
        close(dns_socket);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "DNS server listening on port %d", DNS_SERVER_PORT);

    while (dns_running) {
        // Receive DNS query
        int len = recvfrom(dns_socket, rx_buffer, sizeof(rx_buffer) - 1, 0,
                          (struct sockaddr *)&client_addr, &client_addr_len);

        if (len < 0) {
            if (dns_running) {
                ESP_LOGE(TAG, "recvfrom failed");
            }
            break;
        }

        if (len < sizeof(dns_header_t)) {
            continue;  // Invalid packet
        }

        // Parse DNS header
        dns_header_t *dns_header = (dns_header_t *)rx_buffer;

        // Only respond to queries (QR bit = 0)
        if ((ntohs(dns_header->flags) & 0x8000) != 0) {
            continue;
        }

        // Build DNS response
        memcpy(tx_buffer, rx_buffer, len);
        dns_header_t *response_header = (dns_header_t *)tx_buffer;

        // Set response flags: QR=1 (response), AA=1 (authoritative)
        response_header->flags = htons(0x8400);
        response_header->ancount = htons(1);  // One answer

        int response_len = len;

        // Add answer record at the end
        // We need to add:
        // - Name pointer (0xC00C points to question name)
        // - Type (A record = 0x0001)
        // - Class (IN = 0x0001)
        // - TTL (60 seconds = 0x0000003C)
        // - Data length (4 bytes for IPv4 = 0x0004)
        // - IP address (192.168.4.1)

        uint8_t *answer = (uint8_t *)(tx_buffer + response_len);

        // Name pointer (compress to question section)
        answer[0] = 0xC0;
        answer[1] = 0x0C;

        // Type: A record
        answer[2] = 0x00;
        answer[3] = 0x01;

        // Class: IN
        answer[4] = 0x00;
        answer[5] = 0x01;

        // TTL: 60 seconds
        answer[6] = 0x00;
        answer[7] = 0x00;
        answer[8] = 0x00;
        answer[9] = 0x3C;

        // Data length: 4 bytes
        answer[10] = 0x00;
        answer[11] = 0x04;

        // IP address: 192.168.4.1
        answer[12] = 192;
        answer[13] = 168;
        answer[14] = 4;
        answer[15] = 1;

        response_len += 16;

        // Send DNS response
        sendto(dns_socket, tx_buffer, response_len, 0,
               (struct sockaddr *)&client_addr, client_addr_len);

        ESP_LOGD(TAG, "DNS query responded with %s", AP_IP_ADDRESS);
    }

    close(dns_socket);
    dns_socket = -1;
    vTaskDelete(NULL);
}

/**
 * Start DNS server
 */
esp_err_t dns_server_start(void)
{
    if (dns_running) {
        ESP_LOGW(TAG, "DNS server already running");
        return ESP_OK;
    }

    dns_running = true;

    BaseType_t ret = xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 5, &dns_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create DNS server task");
        dns_running = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "DNS server started");
    return ESP_OK;
}

/**
 * Stop DNS server
 */
esp_err_t dns_server_stop(void)
{
    if (!dns_running) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping DNS server...");
    dns_running = false;

    if (dns_socket >= 0) {
        shutdown(dns_socket, SHUT_RDWR);
        close(dns_socket);
        dns_socket = -1;
    }

    if (dns_task_handle) {
        vTaskDelay(pdMS_TO_TICKS(100));  // Give task time to exit
        dns_task_handle = NULL;
    }

    ESP_LOGI(TAG, "DNS server stopped");
    return ESP_OK;
}
