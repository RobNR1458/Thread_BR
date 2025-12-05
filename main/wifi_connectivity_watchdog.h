#ifndef WIFI_CONNECTIVITY_WATCHDOG_H
#define WIFI_CONNECTIVITY_WATCHDOG_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start WiFi connectivity watchdog
 *
 * Monitors internet connectivity by pinging 8.8.8.8 every 30 seconds.
 * If no internet connectivity is detected for 2 minutes (configurable),
 * automatically clears WiFi credentials and restarts the device in AP mode.
 *
 * This prevents the device from being stuck with invalid WiFi credentials
 * when moved to a different location.
 */
void start_wifi_connectivity_watchdog(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_CONNECTIVITY_WATCHDOG_H
