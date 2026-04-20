/**
 * @file settings.c
 * @brief Persistent settings via NVS (non-volatile storage)
 */

#include "settings.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "lis3mdl.h"
#include "Boost/boost.h"
#include "TirePressure/tire_pressure.h"
#include "EGT/egt.h"
#include "Tilt/tilt.h"
#include "Incline/incline.h"
#include "BLE_TPMS/ble_tpms.h"
#include "PCF85063/PCF85063.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

static const char *TAG = "SETTINGS";

/* NVS namespace — max 15 chars */
#define NVS_NAMESPACE "gauge_settings"

/* Keys (max 15 chars each) */
#define KEY_CAL_OX     "cal_ox"       /* int16  compass offset X           */
#define KEY_CAL_OY     "cal_oy"       /* int16  compass offset Y           */
#define KEY_CAL_SX     "cal_sx"       /* int32  compass scale_x * 100      */
#define KEY_CAL_SY     "cal_sy"       /* int32  compass scale_y * 100      */
#define KEY_BOOST_BAR  "boost_bar"    /* uint8  1=bar 0=psi                */
#define KEY_TPMS_BAR   "tpms_bar"    /* uint8  DEPRECATED - old bool key   */
#define KEY_TPMS_MODE  "tpms_mode"   /* uint8  0=BAR°C 1=PSI°C 2=BAR°F 3=PSI°F */
#define KEY_EGT_CEL    "egt_celsius"  /* uint8  1=°C 0=°F                    */
#define KEY_TILT_OFF   "tilt_offset"  /* int32  tilt zero-offset * 100       */
#define KEY_INCL_OFF   "incl_offset"  /* int32  incline zero-offset * 100    */
#define KEY_INCL_MODE  "incl_mode"    /* uint8  0=deg 1=1-in-X 2=%slope     */

/* WiFi credential keys (string blobs) */
#define KEY_WIFI_H_SSID  "wifi_h_ssid"  /* string  home SSID (max 32)          */
#define KEY_WIFI_H_PASS  "wifi_h_pass"  /* string  home password (max 64)       */
#define KEY_WIFI_P_SSID  "wifi_p_ssid"  /* string  phone SSID (max 32)         */
#define KEY_WIFI_P_PASS  "wifi_p_pass"  /* string  phone password (max 64)      */

/* TPMS sensor MAC address keys (6-byte blobs) */
#define KEY_TPMS_FL    "tpms_fl"       /* blob(6) front-left MAC              */
#define KEY_TPMS_FR    "tpms_fr"       /* blob(6) front-right MAC             */
#define KEY_TPMS_RL    "tpms_rl"       /* blob(6) rear-left MAC               */
#define KEY_TPMS_RR    "tpms_rr"       /* blob(6) rear-right MAC              */
#define KEY_TZ_INDEX   "tz_index"      /* uint8  index into timezone table     */

/* Cached WiFi credentials (loaded from NVS, fallback to defaults) */
static char wifi_home_ssid[33]  = "";
static char wifi_home_pass[65]  = "";
static char wifi_phone_ssid[33] = "";
static char wifi_phone_pass[65] = "";

/* Cached TPMS MACs (loaded from NVS) */
static uint8_t tpms_macs[TPMS_POSITION_COUNT][6] = {0};
static bool    tpms_macs_valid[TPMS_POSITION_COUNT] = {false};

/* Cached timezone index */
static uint8_t tz_index = 12;  /* default: London GMT/BST (index 12 in tz_table) */

/*******************************************************************************
 * Helpers
 ******************************************************************************/

static nvs_handle_t open_nvs(nvs_open_mode_t mode)
{
    nvs_handle_t h = 0;
    esp_err_t err = nvs_open(NVS_NAMESPACE, mode, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return 0;
    }
    return h;
}

/*******************************************************************************
 * Load all settings
 ******************************************************************************/

esp_err_t settings_load(void)
{
    nvs_handle_t h = open_nvs(NVS_READONLY);
    if (!h) {
        ESP_LOGW(TAG, "No saved settings (first boot?)");
        return ESP_OK;          /* not an error — just no data yet */
    }

    /* ── Compass calibration ──────────────────────────────────────────── */
    int16_t ox = 0, oy = 0;
    int32_t sx100 = 0, sy100 = 0;
    bool have_cal = true;

    if (nvs_get_i16(h, KEY_CAL_OX, &ox) != ESP_OK) have_cal = false;
    if (nvs_get_i16(h, KEY_CAL_OY, &oy) != ESP_OK) have_cal = false;
    if (nvs_get_i32(h, KEY_CAL_SX, &sx100) != ESP_OK) have_cal = false;
    if (nvs_get_i32(h, KEY_CAL_SY, &sy100) != ESP_OK) have_cal = false;

    if (have_cal && sx100 > 0 && sy100 > 0) {
        lis3mdl_calibration_t cal = {
            .x_offset = ox,
            .y_offset = oy,
            .z_offset = 0,
            .scale_x  = (float)sx100 / 100.0f,
            .scale_y  = (float)sy100 / 100.0f,
        };
        lis3mdl_set_calibration(&cal);
        ESP_LOGI(TAG, "Compass cal restored: off=(%d,%d) scl=(%.1f,%.1f)",
                 ox, oy, cal.scale_x, cal.scale_y);
    } else {
        ESP_LOGW(TAG, "No compass calibration saved");
    }

    /* ── Boost units ──────────────────────────────────────────────────── */
    uint8_t val = 0;
    if (nvs_get_u8(h, KEY_BOOST_BAR, &val) == ESP_OK) {
        boost_set_units_bar(val != 0);
        ESP_LOGI(TAG, "Boost units restored: %s", val ? "BAR" : "PSI");
    }

    /* ── Tire pressure mode ──────────────────────────────────────────── */
    if (nvs_get_u8(h, KEY_TPMS_MODE, &val) == ESP_OK) {
        tire_pressure_set_mode(val);
        ESP_LOGI(TAG, "TPMS mode restored: %d", val);
    } else if (nvs_get_u8(h, KEY_TPMS_BAR, &val) == ESP_OK) {
        /* Legacy: migrate old bool to new mode (BAR°C or PSI°C) */
        tire_pressure_set_units_bar(val != 0);
        ESP_LOGI(TAG, "TPMS units restored (legacy): %s", val ? "BAR" : "PSI");
    }
    /* ── EGT units ──────────────────────────────────────────────────── */
    if (nvs_get_u8(h, KEY_EGT_CEL, &val) == ESP_OK) {
        egt_set_units_celsius(val != 0);
        ESP_LOGI(TAG, "EGT units restored: %s", val ? "°C" : "°F");
    }

    /* ── Tilt zero-offset ─────────────────────────────────────────────── */
    int32_t toff100 = 0;
    if (nvs_get_i32(h, KEY_TILT_OFF, &toff100) == ESP_OK) {
        tilt_set_offset((float)toff100 / 100.0f);
        ESP_LOGI(TAG, "Tilt offset restored: %.1f°", (float)toff100 / 100.0f);
    }

    /* ── Incline zero-offset ───────────────────────────────────────────── */
    int32_t ioff100 = 0;
    if (nvs_get_i32(h, KEY_INCL_OFF, &ioff100) == ESP_OK) {
        incline_set_offset((float)ioff100 / 100.0f);
        ESP_LOGI(TAG, "Incline offset restored: %.1f°", (float)ioff100 / 100.0f);
    }
    if (nvs_get_u8(h, KEY_INCL_MODE, &val) == ESP_OK) {
        incline_set_mode(val);
        ESP_LOGI(TAG, "Incline mode restored: %d", val);
    }

    /* ── WiFi credentials ─────────────────────────────────────────────── */
    size_t len = sizeof(wifi_home_ssid);
    if (nvs_get_str(h, KEY_WIFI_H_SSID, wifi_home_ssid, &len) == ESP_OK) {
        ESP_LOGI(TAG, "WiFi home SSID restored: %s", wifi_home_ssid);
    }
    len = sizeof(wifi_home_pass);
    if (nvs_get_str(h, KEY_WIFI_H_PASS, wifi_home_pass, &len) == ESP_OK) {
        ESP_LOGI(TAG, "WiFi home password restored");
    }
    len = sizeof(wifi_phone_ssid);
    if (nvs_get_str(h, KEY_WIFI_P_SSID, wifi_phone_ssid, &len) == ESP_OK) {
        ESP_LOGI(TAG, "WiFi phone SSID restored: %s", wifi_phone_ssid);
    }
    len = sizeof(wifi_phone_pass);
    if (nvs_get_str(h, KEY_WIFI_P_PASS, wifi_phone_pass, &len) == ESP_OK) {
        ESP_LOGI(TAG, "WiFi phone password restored");
    }

    /* ── TPMS sensor MAC addresses ────────────────────────────────────── */
    {
        const char *keys[TPMS_POSITION_COUNT] = {KEY_TPMS_FL, KEY_TPMS_FR, KEY_TPMS_RL, KEY_TPMS_RR};
        const char *names[TPMS_POSITION_COUNT] = {"FL", "FR", "RL", "RR"};
        for (int i = 0; i < TPMS_POSITION_COUNT; i++) {
            size_t blob_len = 6;
            if (nvs_get_blob(h, keys[i], tpms_macs[i], &blob_len) == ESP_OK && blob_len == 6) {
                tpms_macs_valid[i] = true;
                ESP_LOGI(TAG, "TPMS %s MAC restored: %02X:%02X:%02X:%02X:%02X:%02X",
                         names[i], tpms_macs[i][0], tpms_macs[i][1], tpms_macs[i][2],
                         tpms_macs[i][3], tpms_macs[i][4], tpms_macs[i][5]);
            }
        }
    }

    /* ── Timezone index ───────────────────────────────────────────────── */
    {
        uint8_t val = 0;
        if (nvs_get_u8(h, KEY_TZ_INDEX, &val) == ESP_OK) {
            tz_index = val;
            ESP_LOGI(TAG, "Timezone index restored: %d", tz_index);
        }
    }

    nvs_close(h);

    /* Apply timezone (must be after NVS load so tz_index is set) */
    settings_apply_timezone();

    return ESP_OK;
}

/*******************************************************************************
 * Save individual settings
 ******************************************************************************/

void settings_save_compass_cal(void)
{
    lis3mdl_calibration_t cal;
    lis3mdl_get_calibration(&cal);

    nvs_handle_t h = open_nvs(NVS_READWRITE);
    if (!h) return;

    /* Store scale as int32 * 100 to avoid float in NVS */
    int32_t sx100 = (int32_t)(cal.scale_x * 100.0f);
    int32_t sy100 = (int32_t)(cal.scale_y * 100.0f);

    nvs_set_i16(h, KEY_CAL_OX, cal.x_offset);
    nvs_set_i16(h, KEY_CAL_OY, cal.y_offset);
    nvs_set_i32(h, KEY_CAL_SX, sx100);
    nvs_set_i32(h, KEY_CAL_SY, sy100);
    nvs_commit(h);
    nvs_close(h);

    ESP_LOGI(TAG, "Compass cal saved: off=(%d,%d) scl=(%.1f,%.1f)",
             cal.x_offset, cal.y_offset, cal.scale_x, cal.scale_y);
}

void settings_save_boost_units(bool use_bar)
{
    nvs_handle_t h = open_nvs(NVS_READWRITE);
    if (!h) return;

    nvs_set_u8(h, KEY_BOOST_BAR, use_bar ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);

    ESP_LOGD(TAG, "Boost units saved: %s", use_bar ? "BAR" : "PSI");
}

void settings_save_tpms_units(bool use_bar)
{
    /* Legacy wrapper – saves as mode */
    settings_save_tpms_mode(use_bar ? 0 : 1);
}

void settings_save_tpms_mode(uint8_t mode)
{
    nvs_handle_t h = open_nvs(NVS_READWRITE);
    if (!h) return;

    nvs_set_u8(h, KEY_TPMS_MODE, mode);
    nvs_commit(h);
    nvs_close(h);

    ESP_LOGD(TAG, "TPMS mode saved: %d", mode);
}

void settings_save_egt_units(bool use_celsius)
{
    nvs_handle_t h = open_nvs(NVS_READWRITE);
    if (!h) return;

    nvs_set_u8(h, KEY_EGT_CEL, use_celsius ? 1 : 0);
    nvs_commit(h);
    nvs_close(h);

    ESP_LOGD(TAG, "EGT units saved: %s", use_celsius ? "°C" : "°F");
}

void settings_save_tilt_offset(float offset_deg)
{
    nvs_handle_t h = open_nvs(NVS_READWRITE);
    if (!h) return;

    int32_t toff100 = (int32_t)(offset_deg * 100.0f);
    nvs_set_i32(h, KEY_TILT_OFF, toff100);
    nvs_commit(h);
    nvs_close(h);

    ESP_LOGD(TAG, "Tilt offset saved: %.1f°", offset_deg);
}

void settings_save_incline_offset(float offset_deg)
{
    nvs_handle_t h = open_nvs(NVS_READWRITE);
    if (!h) return;

    int32_t ioff100 = (int32_t)(offset_deg * 100.0f);
    nvs_set_i32(h, KEY_INCL_OFF, ioff100);
    nvs_commit(h);
    nvs_close(h);

    ESP_LOGD(TAG, "Incline offset saved: %.1f°", offset_deg);
}

void settings_save_incline_mode(uint8_t mode)
{
    nvs_handle_t h = open_nvs(NVS_READWRITE);
    if (!h) return;

    nvs_set_u8(h, KEY_INCL_MODE, mode);
    nvs_commit(h);
    nvs_close(h);

    ESP_LOGD(TAG, "Incline mode saved: %d", mode);
}

/*******************************************************************************
 * WiFi credentials
 ******************************************************************************/

void settings_save_wifi_home(const char *ssid, const char *pass)
{
    nvs_handle_t h = open_nvs(NVS_READWRITE);
    if (!h) return;

    nvs_set_str(h, KEY_WIFI_H_SSID, ssid);
    nvs_set_str(h, KEY_WIFI_H_PASS, pass);
    nvs_commit(h);
    nvs_close(h);

    strncpy(wifi_home_ssid, ssid, sizeof(wifi_home_ssid) - 1);
    wifi_home_ssid[sizeof(wifi_home_ssid) - 1] = '\0';
    strncpy(wifi_home_pass, pass, sizeof(wifi_home_pass) - 1);
    wifi_home_pass[sizeof(wifi_home_pass) - 1] = '\0';

    ESP_LOGI(TAG, "WiFi home credentials saved: %s", ssid);
}

void settings_save_wifi_phone(const char *ssid, const char *pass)
{
    nvs_handle_t h = open_nvs(NVS_READWRITE);
    if (!h) return;

    nvs_set_str(h, KEY_WIFI_P_SSID, ssid);
    nvs_set_str(h, KEY_WIFI_P_PASS, pass);
    nvs_commit(h);
    nvs_close(h);

    strncpy(wifi_phone_ssid, ssid, sizeof(wifi_phone_ssid) - 1);
    wifi_phone_ssid[sizeof(wifi_phone_ssid) - 1] = '\0';
    strncpy(wifi_phone_pass, pass, sizeof(wifi_phone_pass) - 1);
    wifi_phone_pass[sizeof(wifi_phone_pass) - 1] = '\0';

    ESP_LOGI(TAG, "WiFi phone credentials saved: %s", ssid);
}

const char *settings_get_wifi_home_ssid(void) { return wifi_home_ssid; }
const char *settings_get_wifi_home_pass(void) { return wifi_home_pass; }
const char *settings_get_wifi_phone_ssid(void) { return wifi_phone_ssid; }
const char *settings_get_wifi_phone_pass(void) { return wifi_phone_pass; }

/*******************************************************************************
 * TPMS sensor MAC addresses
 ******************************************************************************/

void settings_save_tpms_mac(tpms_position_t pos, const uint8_t *mac)
{
    if (pos >= TPMS_POSITION_COUNT) return;

    const char *keys[TPMS_POSITION_COUNT] = {KEY_TPMS_FL, KEY_TPMS_FR, KEY_TPMS_RL, KEY_TPMS_RR};

    nvs_handle_t h = open_nvs(NVS_READWRITE);
    if (!h) return;

    nvs_set_blob(h, keys[pos], mac, 6);
    nvs_commit(h);
    nvs_close(h);

    memcpy(tpms_macs[pos], mac, 6);
    tpms_macs_valid[pos] = true;

    ESP_LOGI(TAG, "TPMS %s MAC saved: %02X:%02X:%02X:%02X:%02X:%02X",
             pos == 0 ? "FL" : pos == 1 ? "FR" : pos == 2 ? "RL" : "RR",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void settings_save_all_tpms_macs(void)
{
    nvs_handle_t h = open_nvs(NVS_READWRITE);
    if (!h) return;

    const char *keys[TPMS_POSITION_COUNT] = {KEY_TPMS_FL, KEY_TPMS_FR, KEY_TPMS_RL, KEY_TPMS_RR};
    for (int i = 0; i < TPMS_POSITION_COUNT; i++) {
        if (tpms_macs_valid[i]) {
            nvs_set_blob(h, keys[i], tpms_macs[i], 6);
        }
    }
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "All TPMS MACs saved");
}

bool settings_get_tpms_mac(tpms_position_t pos, uint8_t *mac_out)
{
    if (pos >= TPMS_POSITION_COUNT || !tpms_macs_valid[pos]) return false;
    memcpy(mac_out, tpms_macs[pos], 6);
    return true;
}

/*******************************************************************************
 * Timezone table — POSIX TZ strings with friendly names
 * DST rules are embedded in the string; localtime() handles transitions.
 ******************************************************************************/

typedef struct {
    const char *name;     /* Friendly display name */
    const char *posix_tz; /* POSIX TZ string for setenv("TZ", ...) */
    float       latitude; /* Representative latitude for sunrise/sunset calc */
} tz_entry_t;

static const tz_entry_t tz_table[] = {
    /* UTC-12 to UTC+14, common zones with DST where applicable */
    {"Baker Island (-12)",        "BAKT12",                                      0.2f},
    {"Hawaii (-10)",              "HST10",                                       21.3f},
    {"Alaska (-9/DST)",           "AKST9AKDT,M3.2.0,M11.1.0",                   61.2f},
    {"US Pacific (-8/DST)",       "PST8PDT,M3.2.0,M11.1.0",                     34.0f},
    {"US Mountain (-7/DST)",      "MST7MDT,M3.2.0,M11.1.0",                     39.7f},
    {"US Central (-6/DST)",       "CST6CDT,M3.2.0,M11.1.0",                     41.9f},
    {"US Eastern (-5/DST)",       "EST5EDT,M3.2.0,M11.1.0",                     40.7f},
    {"Atlantic (-4/DST)",         "AST4ADT,M3.2.0,M11.1.0",                     44.6f},
    {"Argentina (-3)",            "ART3",                                        -34.6f},
    {"Brazil (-3/DST)",           "BRT3BRST,M10.3.0/0,M2.3.0/0",                -23.5f},
    {"Mid-Atlantic (-2)",         "MAT2",                                        32.3f},
    {"Azores (-1/DST)",           "AZOT1AZOST,M3.5.0/0,M10.5.0/1",              38.7f},
    {"London (GMT/BST)",          "GMT0BST,M3.5.0/1,M10.5.0",                   51.5f},
    {"Paris (CET/CEST)",          "CET-1CEST,M3.5.0/2,M10.5.0/3",               48.9f},
    {"Athens (EET/EEST)",         "EET-2EEST,M3.5.0/3,M10.5.0/4",               38.0f},
    {"Moscow (+3)",               "MSK-3",                                       55.8f},
    {"Tehran (+3:30/DST)",        "<+0330>-3:30<+0430>,J79/24,J263/24",          35.7f},
    {"Dubai (+4)",                "GST-4",                                       25.2f},
    {"Kabul (+4:30)",             "<+0430>-4:30",                                34.5f},
    {"Pakistan (+5)",             "PKT-5",                                       24.9f},
    {"India (+5:30)",             "IST-5:30",                                    28.6f},
    {"Nepal (+5:45)",             "<+0545>-5:45",                                27.7f},
    {"Bangladesh (+6)",           "BDT-6",                                       23.8f},
    {"Thailand (+7)",             "ICT-7",                                       13.8f},
    {"China/HK (+8)",             "CST-8",                                       22.3f},
    {"Japan/Korea (+9)",          "JST-9",                                       35.7f},
    {"Australia AEST (+10/DST)",  "AEST-10AEDT,M10.1.0,M4.1.0/3",               -33.9f},
    {"Australia ACST (+9:30/DST)","ACST-9:30ACDT,M10.1.0,M4.1.0/3",             -34.9f},
    {"Australia AWST (+8)",       "AWST-8",                                      -31.9f},
    {"New Zealand (+12/DST)",     "NZST-12NZDT,M9.5.0,M4.1.0/3",                -36.8f},
    {"Tonga (+13)",               "TOT-13",                                      -21.2f},
    {"South Africa (SAST)",       "SAST-2",                                      -33.9f},
    {"E. Africa (+3)",            "EAT-3",                                       -1.3f},
    {"W. Africa (+1)",            "WAT-1",                                       6.5f},
};

#define TZ_TABLE_COUNT  (sizeof(tz_table) / sizeof(tz_table[0]))

void settings_apply_timezone(void)
{
    uint8_t idx = tz_index;
    if (idx >= TZ_TABLE_COUNT) idx = 12;  /* fallback: London */
    setenv("TZ", tz_table[idx].posix_tz, 1);
    tzset();
    ESP_LOGI(TAG, "Timezone set: %s (%s)", tz_table[idx].name, tz_table[idx].posix_tz);
}

void settings_save_timezone(uint8_t idx)
{
    if (idx >= TZ_TABLE_COUNT) return;

    uint8_t old_idx = tz_index;
    tz_index = idx;

    nvs_handle_t h = open_nvs(NVS_READWRITE);
    if (!h) return;

    nvs_set_u8(h, KEY_TZ_INDEX, idx);
    nvs_commit(h);
    nvs_close(h);

    /* ── Recalculate RTC: old-TZ local → UTC → new-TZ local ────────── */
    if (old_idx != idx) {
        /* 1. Read current RTC time (stored as old-TZ local) */
        datetime_t rtc;
        PCF85063_Read_Time(&rtc);

        /* 2. Convert to struct tm under old timezone → mktime → UTC epoch */
        setenv("TZ", tz_table[old_idx].posix_tz, 1);
        tzset();

        struct tm old_tm = {0};
        old_tm.tm_year = rtc.year - 1900;
        old_tm.tm_mon  = rtc.month - 1;
        old_tm.tm_mday = rtc.day;
        old_tm.tm_hour = rtc.hour;
        old_tm.tm_min  = rtc.minute;
        old_tm.tm_sec  = rtc.second;
        old_tm.tm_isdst = -1;  /* let mktime figure out DST */
        time_t utc_epoch = mktime(&old_tm);

        /* 3. Switch to new timezone and convert UTC → new local */
        setenv("TZ", tz_table[idx].posix_tz, 1);
        tzset();

        struct tm new_tm;
        localtime_r(&utc_epoch, &new_tm);

        /* 4. Write new local time to RTC */
        datetime_t new_rtc = {
            .year   = new_tm.tm_year + 1900,
            .month  = new_tm.tm_mon + 1,
            .day    = new_tm.tm_mday,
            .hour   = new_tm.tm_hour,
            .minute = new_tm.tm_min,
            .second = new_tm.tm_sec,
        };
        PCF85063_Set_All(new_rtc);

        ESP_LOGI(TAG, "RTC adjusted: %02d:%02d:%02d → %02d:%02d:%02d (%s → %s)",
                 rtc.hour, rtc.minute, rtc.second,
                 new_rtc.hour, new_rtc.minute, new_rtc.second,
                 tz_table[old_idx].name, tz_table[idx].name);
    } else {
        settings_apply_timezone();
    }

    ESP_LOGI(TAG, "Timezone saved: %d (%s)", idx, tz_table[idx].name);
}

uint8_t settings_get_timezone_index(void)
{
    return tz_index;
}

int settings_get_timezone_count(void)
{
    return (int)TZ_TABLE_COUNT;
}

const char *settings_get_timezone_name(int idx)
{
    if (idx < 0 || idx >= (int)TZ_TABLE_COUNT) return "?";
    return tz_table[idx].name;
}

bool settings_timezone_configured(void)
{
    /* True if explicitly saved (non-default or any NVS write) */
    nvs_handle_t h = open_nvs(NVS_READONLY);
    if (!h) return false;
    uint8_t val;
    bool found = (nvs_get_u8(h, KEY_TZ_INDEX, &val) == ESP_OK);
    nvs_close(h);
    return found;
}

/*******************************************************************************
 * Solar twilight calculation — dawn/dusk based on timezone latitude
 ******************************************************************************/

float settings_get_latitude(void)
{
    uint8_t idx = tz_index;
    if (idx >= TZ_TABLE_COUNT) idx = 12;
    return tz_table[idx].latitude;
}

bool settings_is_dark(uint8_t month, uint8_t day, uint8_t hour, uint8_t minute)
{
    float lat = settings_get_latitude();

    /* Day of year (approximate, ignoring leap years — good enough) */
    static const uint16_t month_days[] = {0,31,59,90,120,151,181,212,243,273,304,334};
    if (month < 1 || month > 12) return false;
    int doy = month_days[month - 1] + day;

    /* Solar declination using Fourier approximation (Spencer, 1971) */
    float gamma = 2.0f * (float)M_PI * (doy - 1) / 365.0f;
    float decl  = 0.006918f - 0.399912f * cosf(gamma)
                + 0.070257f * sinf(gamma)
                - 0.006758f * cosf(2.0f * gamma)
                + 0.000907f * sinf(2.0f * gamma)
                - 0.002697f * cosf(3.0f * gamma)
                + 0.001480f * sinf(3.0f * gamma);

    float lat_rad = lat * (float)M_PI / 180.0f;

    /* Civil twilight: sun 6° below horizon (zenith = 96°) */
    float cos_zenith = cosf(96.0f * (float)M_PI / 180.0f);
    float cos_ha = (cos_zenith - sinf(lat_rad) * sinf(decl))
                 / (cosf(lat_rad) * cosf(decl));

    /* Polar extremes: midnight sun or polar night */
    if (cos_ha < -1.0f) return false;   /* never gets dark */
    if (cos_ha >  1.0f) return true;    /* never gets light */

    float ha_hours = acosf(cos_ha) * 12.0f / (float)M_PI;
    float dawn = 12.0f - ha_hours;
    float dusk = 12.0f + ha_hours;

    /* The RTC stores local time, but dawn/dusk are in UTC (solar noon = 12:00 UTC).
     * Convert dawn/dusk from UTC to local time using the system timezone offset. */
    time_t t = time(NULL);
    struct tm local_tm, utc_tm;
    localtime_r(&t, &local_tm);
    gmtime_r(&t, &utc_tm);
    float utc_offset = (float)(local_tm.tm_hour - utc_tm.tm_hour)
                     + (float)(local_tm.tm_min - utc_tm.tm_min) / 60.0f;
    /* Handle day boundary (e.g. local 00:30, UTC 23:30 → offset should be +1) */
    if (utc_offset < -12.0f) utc_offset += 24.0f;
    if (utc_offset >  14.0f) utc_offset -= 24.0f;

    dawn += utc_offset;
    dusk += utc_offset;

    float now = (float)hour + (float)minute / 60.0f;
    return (now < dawn || now >= dusk);
}
