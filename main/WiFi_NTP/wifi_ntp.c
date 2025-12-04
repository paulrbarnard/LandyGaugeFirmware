/**
 * @file wifi_ntp.c
 * @brief WiFi connection and NTP time synchronization implementation
 */

#include "wifi_ntp.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "PCF85063.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "WIFI_NTP";

// WiFi credentials
#define WIFI_SSID "Barnard Home Network"
#define WIFI_PASS "0D03908CE5"

// Event group bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
#define MAX_RETRY 5

static bool wifi_connected = false;

static void event_handler(void* arg, esp_event_base_t event_base,
                         int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry connecting to WiFi... (%d/%d)", s_retry_num, MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Failed to connect to WiFi");
        }
        wifi_connected = false;
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        wifi_connected = true;
    }
}

bool wifi_ntp_init(void)
{
    s_wifi_event_group = xEventGroupCreate();

    // WiFi already initialized and started by Wireless_Init()
    // Register our event handlers
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    // Configure WiFi with our credentials
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    ESP_LOGI(TAG, "WiFi configuration set. Connecting to %s...", WIFI_SSID);
    
    // Connect to the AP
    esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi connect failed: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "WiFi initialization finished. Connecting to %s...", WIFI_SSID);

    // Wait for connection result
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                          WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                          pdFALSE,
                                          pdFALSE,
                                          pdMS_TO_TICKS(15000)); // 15 second timeout

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to WiFi: %s", WIFI_SSID);
        return true;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to WiFi: %s", WIFI_SSID);
        return false;
    } else {
        ESP_LOGE(TAG, "WiFi connection timeout");
        return false;
    }
}

static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "NTP time synchronized");
}

bool wifi_ntp_sync_time(void)
{
    if (!wifi_connected) {
        ESP_LOGE(TAG, "WiFi not connected, cannot sync time");
        return false;
    }

    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();

    // Wait for time to be synchronized (max 10 seconds)
    int retry = 0;
    const int retry_count = 50; // 50 * 200ms = 10 seconds
    time_t now = 0;
    struct tm timeinfo = { 0 };
    
    while (timeinfo.tm_year < (2016 - 1900) && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(pdMS_TO_TICKS(200));
        time(&now);
        localtime_r(&now, &timeinfo);
    }

    if (timeinfo.tm_year < (2016 - 1900)) {
        ESP_LOGE(TAG, "Failed to synchronize time from NTP (timeout)");
        esp_sntp_stop();
        return false;
    }

    ESP_LOGI(TAG, "Time synchronized from NTP");

    // Get current time
    time(&now);
    localtime_r(&now, &timeinfo);

    // Set timezone to UK (GMT/BST with automatic daylight saving time)
    // BST starts last Sunday in March at 01:00 GMT (clocks forward to 02:00)
    // BST ends last Sunday in October at 02:00 BST (clocks back to 01:00)
    setenv("TZ", "GMT0BST,M3.5.0/1,M10.5.0", 1);
    tzset();
    time(&now);
    localtime_r(&now, &timeinfo);

    ESP_LOGI(TAG, "Current time: %04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    // Update RTC with NTP time
    datetime_t ntp_time;
    ntp_time.year = timeinfo.tm_year + 1900;
    ntp_time.month = timeinfo.tm_mon + 1;
    ntp_time.day = timeinfo.tm_mday;
    ntp_time.hour = timeinfo.tm_hour;
    ntp_time.minute = timeinfo.tm_min;
    ntp_time.second = timeinfo.tm_sec;
    
    PCF85063_Set_All(ntp_time);

    ESP_LOGI(TAG, "RTC updated with NTP time");
    
    return true;
}

bool wifi_is_connected(void)
{
    return wifi_connected;
}
