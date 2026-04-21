/*
 * Copyright (c) 2026 Paul Barnard (Toxic Celery)
 */

/**
 * @file wifi_ntp.c
 * @brief WiFi connection and NTP time synchronization implementation
 *
 * WiFi is started on ignition-on via wifi_ntp_start() which spawns a
 * background task.  The task connects, syncs NTP, updates the RTC, then
 * stops WiFi to free internal DMA RAM (large LVGL display buffers leave
 * little headroom for WiFi).  A 24-hour NVS cooldown prevents redundant
 * syncs on short drives.
 */

#include "wifi_ntp.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "PCF85063.h"
#include "ble_tpms.h"
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "settings.h"

static const char *TAG = "WIFI_NTP";

// Default WiFi credentials — used when NVS has no saved credentials
#define WIFI_SSID_HOME_DEFAULT    "Barnard Home Network"
#define WIFI_SSID_PHONE_DEFAULT   "Paul\xe2\x80\x99s iPhone"   // Unicode RIGHT SINGLE QUOTATION MARK U+2019
#define WIFI_PASS_DEFAULT         "0D03908CE5"

// Timing
#define WIFI_CONNECT_TIMEOUT_MS  15000   // 15 s to associate + get IP
#define NTP_SYNC_TIMEOUT_MS      15000   // 15 s for SNTP response
#define WIFI_OVERALL_TIMEOUT_MS 120000   // 2 min overall then give up

// NVS cooldown — skip sync if last success was < this many hours ago
#define WIFI_NTP_COOLDOWN_HOURS  24
#define NVS_NAMESPACE            "wifi_ntp"
#define NVS_KEY_LAST_SYNC        "last_sync"   // epoch seconds (uint32)

// Event group bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group = NULL;
static int  s_retry_num   = 0;
static bool wifi_connected = false;
static bool s_task_running = false;       // true while sync task exists
static bool s_abort        = false;       // set by wifi_ntp_stop()
static bool s_handlers_registered = false;
static bool s_use_phone_ssid = false;     // false = home network, true = iPhone
static volatile bool s_ntp_synced = false; // set by SNTP notification callback
#define MAX_RETRY 5

/* ─── event handler ──────────────────────────────────────────────── */
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_connected = false;
        if (s_retry_num < MAX_RETRY && !s_abort) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGD(TAG, "Retry WiFi connect (%d/%d)", s_retry_num, MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            if (s_abort) {
                ESP_LOGW(TAG, "WiFi connect aborted");
            } else {
                ESP_LOGE(TAG, "WiFi connect failed after %d retries", MAX_RETRY);
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&ev->ip_info.ip));
        s_retry_num = 0;
        wifi_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/* ─── NVS cooldown helpers ───────────────────────────────────────── */
static uint32_t nvs_get_last_sync(void)
{
    nvs_handle_t h;
    uint32_t val = 0;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u32(h, NVS_KEY_LAST_SYNC, &val);
        nvs_close(h);
    }
    return val;
}

static void nvs_set_last_sync(uint32_t epoch)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, NVS_KEY_LAST_SYNC, epoch);
        nvs_commit(h);
        nvs_close(h);
    }
}

static bool cooldown_active(void)
{
    uint32_t last = nvs_get_last_sync();
    if (last == 0) return false;  // never synced

    time_t now;
    time(&now);
    if (now < 1600000000) return false;  // system clock not set yet

    uint32_t age_h = ((uint32_t)now - last) / 3600;
    if (age_h < WIFI_NTP_COOLDOWN_HOURS) {
        static bool logged_once = false;
        if (!logged_once) {
            ESP_LOGI(TAG, "NTP cooldown active — last sync %lu h ago (<%d h)",
                     (unsigned long)age_h, WIFI_NTP_COOLDOWN_HOURS);
            logged_once = true;
        }
        return true;
    }
    return false;
}

/* ─── SNTP time sync notification callback ───────────────────────── */
static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "SNTP time synced (epoch %ld)", (long)tv->tv_sec);
    s_ntp_synced = true;
}

/* ─── WiFi start / stop helpers ──────────────────────────────────── */
static void wifi_shutdown(void)
{
    esp_sntp_stop();
    esp_wifi_disconnect();
    esp_wifi_stop();
    wifi_connected = false;
    ESP_LOGI(TAG, "WiFi stopped");
}

/* ─── WiFi credential switcher ───────────────────────────────────── */
static void wifi_set_credentials(bool use_phone)
{
    /* Use NVS credentials if available, otherwise fall back to compiled defaults */
    const char *ssid;
    const char *pass;

    if (use_phone) {
        ssid = settings_get_wifi_phone_ssid();
        pass = settings_get_wifi_phone_pass();
        if (ssid[0] == '\0') ssid = WIFI_SSID_PHONE_DEFAULT;
        if (pass[0] == '\0') pass = WIFI_PASS_DEFAULT;
    } else {
        ssid = settings_get_wifi_home_ssid();
        pass = settings_get_wifi_home_pass();
        if (ssid[0] == '\0') ssid = WIFI_SSID_HOME_DEFAULT;
        if (pass[0] == '\0') pass = WIFI_PASS_DEFAULT;
    }

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    /* Apple devices use Unicode RIGHT SINGLE QUOTATION MARK (U+2019,
       UTF-8: E2 80 99) in auto-generated names like "Paul's iPhone".
       Our simple text editor only has the ASCII apostrophe (0x27),
       so substitute only when the SSID looks like an Apple device name. */
    char fixed_ssid[33];
    bool apple_ssid = (strcasestr(ssid, "iPhone") || strcasestr(ssid, "iPad") ||
                       strcasestr(ssid, "iPod")   || strcasestr(ssid, "MacBook") ||
                       strcasestr(ssid, "iMac")   || strcasestr(ssid, "Apple"));
    if (apple_ssid && strchr(ssid, '\'')) {
        const char *s = ssid;
        char *d = fixed_ssid;
        char *end = fixed_ssid + sizeof(fixed_ssid) - 1;
        while (*s && d < end) {
            if (*s == '\'' && (end - d) >= 3) {
                *d++ = '\xe2';
                *d++ = '\x80';
                *d++ = '\x99';
                s++;
            } else {
                *d++ = *s++;
            }
        }
        *d = '\0';
        ssid = fixed_ssid;
        ESP_LOGI(TAG, "Apple device detected — converted apostrophe to Unicode");
    }

    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, pass, sizeof(wifi_config.sta.password));
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    ESP_LOGI(TAG, "WiFi credentials set for: %s", ssid);
}

/* ─── sync background task ───────────────────────────────────────── */
static void wifi_ntp_task(void *arg)
{
    const char *net_name = s_use_phone_ssid ? "phone" : "home";
    ESP_LOGI(TAG, "NTP sync task started (network: %s)", net_name);

    // Stop BLE scanning — shared radio, BLE must yield for WiFi
    ESP_LOGI(TAG, "Stopping BLE scan for WiFi coexistence");
    ble_tpms_stop_scan();
    vTaskDelay(pdMS_TO_TICKS(200));  // Let BLE radio quiesce

    // Apply the correct SSID before starting WiFi
    wifi_set_credentials(s_use_phone_ssid);

    // Reset state
    s_retry_num = 0;
    s_abort = false;
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    // 1. Start WiFi STA
    esp_err_t err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(err));
        goto done;
    }

    // 2. Wait for connection
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));
    if (s_abort) goto shutdown;
    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "WiFi connect timeout/fail");
        goto shutdown;
    }

    ESP_LOGI(TAG, "WiFi connected — starting NTP sync");

    // 3. SNTP sync — use notification callback for reliable detection
    s_ntp_synced = false;
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_setservername(2, "216.239.35.0");   // time.google.com IP fallback (DNS may fail on hotspot)
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP started (pool.ntp.org + time.google.com + IP fallback)");

    int retry = 0;
    const int max_retry = NTP_SYNC_TIMEOUT_MS / 250;
    while (!s_ntp_synced && ++retry < max_retry && !s_abort) {
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    if (s_abort) goto shutdown;

    if (!s_ntp_synced) {
        ESP_LOGE(TAG, "NTP sync timeout after %d ms", NTP_SYNC_TIMEOUT_MS);
        goto shutdown;
    }

    // 4. Apply timezone and update RTC
    settings_apply_timezone();
    time_t now = 0;
    struct tm ti = {0};
    time(&now);
    localtime_r(&now, &ti);

    ESP_LOGI(TAG, "NTP time: %04d-%02d-%02d %02d:%02d:%02d",
             ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
             ti.tm_hour, ti.tm_min, ti.tm_sec);

    datetime_t ntp_time = {
        .year   = ti.tm_year + 1900,
        .month  = ti.tm_mon + 1,
        .day    = ti.tm_mday,
        .hour   = ti.tm_hour,
        .minute = ti.tm_min,
        .second = ti.tm_sec,
    };
    PCF85063_Set_All(ntp_time);
    ESP_LOGI(TAG, "RTC updated from NTP");

    // 5. Record successful sync epoch in NVS
    nvs_set_last_sync((uint32_t)now);

shutdown:
    wifi_shutdown();
done:
    // Resume BLE scanning now that WiFi is off
    ESP_LOGI(TAG, "Resuming BLE scan");
    ble_tpms_start_scan();
    s_task_running = false;
    ESP_LOGI(TAG, "NTP sync task finished");
    vTaskDelete(NULL);
}

/* ─── public API ─────────────────────────────────────────────────── */

bool wifi_ntp_init(void)
{
    if (!s_wifi_event_group) {
        s_wifi_event_group = xEventGroupCreate();
    }

    // One-time WiFi subsystem initialisation (netif, event loop, STA mode)
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    if (!s_handlers_registered) {
        // Register event handlers (persistent — survive wifi stop/start cycles)
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(
            IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));
        s_handlers_registered = true;
    }

    // Set default credentials (home network for auto-sync)
    wifi_set_credentials(false);

    // Clear cooldown so a sync is attempted on every fresh boot
    nvs_set_last_sync(0);

    ESP_LOGI(TAG, "WiFi NTP initialised (WiFi idle, awaiting sync trigger)");
    return true;
}

void wifi_ntp_start(void)
{
    if (s_task_running) {
        return;  // silently skip — main loop calls this every 10ms
    }
    if (cooldown_active()) {
        return;  // already logged inside cooldown_active()
    }

    // Periodic auto-sync uses the home network
    s_use_phone_ssid = false;
    s_task_running = true;
    BaseType_t ret = xTaskCreatePinnedToCore(wifi_ntp_task, "wifi_ntp", 6144, NULL, 3, NULL, 0);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create NTP task (heap=%lu)", (unsigned long)esp_get_free_heap_size());
        s_task_running = false;
    }
}

void wifi_ntp_force_start(void)
{
    if (s_task_running) {
        ESP_LOGW(TAG, "Sync already in progress — ignoring");
        return;
    }
    // Bypass cooldown — user explicitly requested a sync via iPhone hotspot
    ESP_LOGI(TAG, "Force NTP sync requested (iPhone hotspot, bypassing cooldown)");
    s_use_phone_ssid = true;
    s_task_running = true;
    BaseType_t ret = xTaskCreatePinnedToCore(wifi_ntp_task, "wifi_ntp", 6144, NULL, 3, NULL, 0);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create NTP task (heap=%lu)", (unsigned long)esp_get_free_heap_size());
        s_task_running = false;
    }
}

void wifi_ntp_stop(void)
{
    if (!s_task_running) {
        // Nothing running — but make sure WiFi is off anyway
        esp_wifi_disconnect();
        esp_wifi_stop();
        wifi_connected = false;
        return;
    }
    ESP_LOGW(TAG, "Aborting NTP sync task");
    s_abort = true;
    // Signal the event group so the task unblocks quickly
    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    // Task will self-delete; it checks s_abort and calls wifi_shutdown()
}

bool wifi_is_connected(void)
{
    return wifi_connected;
}

bool wifi_ntp_is_active(void)
{
    return s_task_running;
}
