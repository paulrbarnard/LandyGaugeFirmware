/*
 * Copyright (c) 2026 Paul Barnard (Toxic Celery)
 */

/**
 * @file wifi_ntp.h
 * @brief WiFi connection and NTP time synchronization
 */

#ifndef WIFI_NTP_H
#define WIFI_NTP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/**
 * @brief Initialize WiFi and connect to network
 * @return true if connection successful, false otherwise
 */
bool wifi_ntp_init(void);

/**
 * @brief Synchronize time from NTP server and update RTC
 * @return true if time sync successful, false otherwise
 */
bool wifi_ntp_sync_time(void);

/**
 * @brief Check if WiFi is connected
 * @return true if connected, false otherwise
 */
bool wifi_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_NTP_H */
