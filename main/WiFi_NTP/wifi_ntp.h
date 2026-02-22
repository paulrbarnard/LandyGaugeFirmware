/**
 * @file wifi_ntp.h
 * @brief WiFi connection and NTP time synchronization
 *
 * WiFi is started on ignition-on and stopped automatically after a
 * successful NTP sync or after a timeout (default 2 minutes).  A 24-hour
 * cooldown stored in NVS prevents unnecessary syncs on short drives.
 */

#ifndef WIFI_NTP_H
#define WIFI_NTP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/**
 * @brief One-time setup: register WiFi event handlers and configure credentials.
 *
 * Call once during boot (after Wireless_Init has created the netif/event loop
 * and esp_wifi_init/set_mode/start).  Does NOT start a connection — WiFi is
 * left in STA-stopped state ready for wifi_ntp_start().
 *
 * @return true on success
 */
bool wifi_ntp_init(void);

/**
 * @brief Start WiFi → NTP sync cycle (non-blocking).
 *
 * Launches a background FreeRTOS task that:
 *   1. Starts WiFi STA and connects to the configured AP
 *   2. Syncs time via SNTP and updates the RTC
 *   3. Stops WiFi to save power
 *   4. Self-deletes
 *
 * If the last successful sync was less than WIFI_NTP_COOLDOWN_HOURS ago
 * (read from NVS), the request is silently skipped.
 *
 * Safe to call repeatedly — ignored if a sync is already in progress.
 */
void wifi_ntp_start(void);

/**
 * @brief Force-start WiFi → NTP sync, bypassing the 24-hour cooldown.
 *
 * Use when the user explicitly requests a time sync (e.g. long-press on clock).
 * Still ignored if a sync is already in progress.
 */
void wifi_ntp_force_start(void);

/**
 * @brief Force-stop any in-progress WiFi/NTP activity and shut down WiFi.
 *
 * Called when ignition turns OFF while a sync is still running.
 */
void wifi_ntp_stop(void);

/**
 * @brief Check if WiFi is currently connected.
 */
bool wifi_is_connected(void);

/**
 * @brief Check if a sync task is currently running.
 */
bool wifi_ntp_is_active(void);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_NTP_H */
