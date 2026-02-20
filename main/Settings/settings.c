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
    nvs_close(h);
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
