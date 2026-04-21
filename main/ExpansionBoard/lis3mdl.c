/*
 * Copyright (c) 2026 Paul Barnard (Toxic Celery)
 */

/**
 * @file lis3mdl.c
 * @brief LIS3MDL 3-axis magnetometer driver — heading & calibration
 *
 * ST LIS3MDL: ±4 gauss, 16-bit, I²C digital magnetic sensor.
 * Configured for continuous mode, high-performance XY/Z, 10 Hz ODR.
 *
 * Compass heading is computed from the horizontal X and Y axes.
 * The sensor must be mounted level (Z axis vertical) for correct heading.
 *
 * Calibration uses min/max tracking during a user-initiated 360° rotation.
 *   Hard-iron offsets  = midpoint of each axis range.
 *   Soft-iron scale    = half-spread of each axis (normalises the ellipse
 *                        to a circle before computing atan2).
 */

#include "lis3mdl.h"
#include "I2C_Driver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "LIS3MDL";

/* ── Sensitivity (LSB/Gauss) from datasheet Table 3 ────────────────────── */
#define SENSITIVITY_4G   6842.0f
#define SENSITIVITY_8G   3421.0f
#define SENSITIVITY_12G  2281.0f
#define SENSITIVITY_16G  1711.0f
#define GAUSS_TO_UT      100.0f          /* 1 Gauss = 100 µT */

/* ── Calibration thresholds ─────────────────────────────────────────────── */
/* Empirical full-360° spreads at ±4 G: X ≈ 5 300, Y ≈ 6 300 LSB.
   Require ~80 % of the weaker axis so user must do a near-complete turn. */
#define CAL_TARGET_SPREAD   5000   /* progress bar reaches 100 %            */
#define CAL_GOOD_SPREAD     3500   /* minimum for a usable result           */
#define CAL_MIN_SAMPLES       50   /* ≈5 s at 10 Hz                        */

/* ── Heading EMA filter ─────────────────────────────────────────────────── */
/* α = 0.5 → 50 % new sample, 50 % history (≈0.2 s time constant @ 10 Hz).
   Jumps > SNAP_THRESHOLD° bypass the filter entirely so a fast rotation
   isn’t sluggish.                                                          */
#define HEADING_EMA_ALPHA     0.5f
#define HEADING_SNAP_THRESH  30.0f

/* Declination / fixed offset (degrees).  Positive value means magnetic north
   is east of true north.  Subtract this from the magnetic heading to get a
   true-north heading.  Adjust for your location or to compensate for
   fixed installation offsets.                                               */
#define HEADING_DECLINATION  39.0f

/* ── Internal state ─────────────────────────────────────────────────────── */
static lis3mdl_fs_t current_fs = LIS3MDL_FS_4G;

/* Hard-iron offsets (midpoint of calibration min/max) */
static int16_t offset_x = 0;
static int16_t offset_y = 0;

/* Soft-iron scale factors (half of the calibration spread per axis).
   Dividing corrected readings by these produces unit-amplitude values.    */
static float scale_x = 1.0f;
static float scale_y = 1.0f;

/* Live calibration state */
static bool    calibrating      = false;
static int     cal_sample_count = 0;
static int16_t cal_x_min, cal_x_max;
static int16_t cal_y_min, cal_y_max;

/* EMA heading state (–1 = uninitialised) */
static float ema_heading = -1.0f;

/*******************************************************************************
 * Low-level register access
 ******************************************************************************/

static esp_err_t lis3mdl_write_reg(uint8_t reg, uint8_t data)
{
    esp_err_t ret = I2C_Write(LIS3MDL_I2C_ADDR, reg, &data, 1);
    if (ret != ESP_OK)
        ESP_LOGE(TAG, "Write 0x%02X failed: %s", reg, esp_err_to_name(ret));
    return ret;
}

static esp_err_t lis3mdl_read_reg(uint8_t reg, uint8_t *data)
{
    esp_err_t ret = I2C_Read(LIS3MDL_I2C_ADDR, reg, data, 1);
    if (ret != ESP_OK)
        ESP_LOGE(TAG, "Read 0x%02X failed: %s", reg, esp_err_to_name(ret));
    return ret;
}

static esp_err_t lis3mdl_read_regs(uint8_t reg, uint8_t *data, uint32_t len)
{
    /* LIS3MDL auto-increments when MSB of sub-address is set */
    esp_err_t ret = I2C_Read(LIS3MDL_I2C_ADDR, reg | 0x80, data, len);
    if (ret != ESP_OK)
        ESP_LOGE(TAG, "Read 0x%02X (%lu B) failed: %s",
                 reg, (unsigned long)len, esp_err_to_name(ret));
    return ret;
}

/*******************************************************************************
 * Helpers
 ******************************************************************************/

static float get_sensitivity(void)
{
    switch (current_fs) {
        case LIS3MDL_FS_4G:  return SENSITIVITY_4G;
        case LIS3MDL_FS_8G:  return SENSITIVITY_8G;
        case LIS3MDL_FS_12G: return SENSITIVITY_12G;
        case LIS3MDL_FS_16G: return SENSITIVITY_16G;
        default:             return SENSITIVITY_4G;
    }
}

/**
 * @brief Apply EMA smoothing to a heading value, handling the 360/0 wrap.
 *        Snaps immediately for large changes (fast rotation).
 */
static float smooth_heading(float raw_deg)
{
    if (ema_heading < 0.0f) {
        ema_heading = raw_deg;                   /* seed on first call */
    } else {
        float diff = raw_deg - ema_heading;
        if (diff >  180.0f) diff -= 360.0f;
        if (diff < -180.0f) diff += 360.0f;

        /* Snap immediately for large jumps (fast rotation) */
        if (fabsf(diff) > HEADING_SNAP_THRESH) {
            ema_heading = raw_deg;
        } else {
            ema_heading += HEADING_EMA_ALPHA * diff;
        }
        if (ema_heading <    0.0f) ema_heading += 360.0f;
        if (ema_heading >= 360.0f) ema_heading -= 360.0f;
    }
    return ema_heading;
}

/*******************************************************************************
 * Initialisation
 ******************************************************************************/

esp_err_t lis3mdl_init(void)
{
    esp_err_t ret;

    /* Verify chip presence */
    uint8_t who = 0;
    ret = lis3mdl_read_reg(LIS3MDL_REG_WHO_AM_I, &who);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Not found at 0x%02X", LIS3MDL_I2C_ADDR);
        return ret;
    }
    if (who != LIS3MDL_WHO_AM_I_VALUE) {
        ESP_LOGE(TAG, "Bad WHO_AM_I 0x%02X (expected 0x%02X)",
                 who, LIS3MDL_WHO_AM_I_VALUE);
        return ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(TAG, "Detected at 0x%02X", LIS3MDL_I2C_ADDR);

    /* Soft reset */
    ret = lis3mdl_write_reg(LIS3MDL_REG_CTRL_REG2, LIS3MDL_SOFT_RST);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(50));

    /* CTRL1: temp enable | high-performance XY | 80 Hz ODR */
    ret = lis3mdl_write_reg(LIS3MDL_REG_CTRL_REG1,
                            LIS3MDL_TEMP_EN | LIS3MDL_OM_HIGH | LIS3MDL_ODR_80HZ);
    if (ret != ESP_OK) return ret;

    /* CTRL2: ±4 G full-scale (best resolution for compass use) */
    current_fs = LIS3MDL_FS_4G;
    ret = lis3mdl_write_reg(LIS3MDL_REG_CTRL_REG2, (uint8_t)current_fs);
    if (ret != ESP_OK) return ret;

    /* CTRL3: continuous conversion */
    ret = lis3mdl_write_reg(LIS3MDL_REG_CTRL_REG3, (uint8_t)LIS3MDL_MD_CONTINUOUS);
    if (ret != ESP_OK) return ret;

    /* CTRL4: high-performance Z */
    ret = lis3mdl_write_reg(LIS3MDL_REG_CTRL_REG4, 0x08);
    if (ret != ESP_OK) return ret;

    /* CTRL5: block data update */
    ret = lis3mdl_write_reg(LIS3MDL_REG_CTRL_REG5, 0x40);
    if (ret != ESP_OK) return ret;

    /* Flush startup samples */
    vTaskDelay(pdMS_TO_TICKS(200));
    lis3mdl_raw_t discard;
    for (int i = 0; i < 3; i++) {
        lis3mdl_read_raw(&discard);
        vTaskDelay(pdMS_TO_TICKS(110));
    }

    ESP_LOGI(TAG, "Initialised: 80 Hz, +/-4 G, high-perf, continuous");
    return ESP_OK;
}

/*******************************************************************************
 * Raw / calibrated reads
 ******************************************************************************/

bool lis3mdl_data_ready(void)
{
    uint8_t st = 0;
    if (lis3mdl_read_reg(LIS3MDL_REG_STATUS, &st) != ESP_OK) return false;
    return (st & LIS3MDL_STATUS_ZYXDA) != 0;
}

esp_err_t lis3mdl_read_raw(lis3mdl_raw_t *raw)
{
    uint8_t buf[6] = {0};
    esp_err_t ret = lis3mdl_read_regs(LIS3MDL_REG_OUT_X_L, buf, 6);
    if (ret != ESP_OK) return ret;

    raw->x = (int16_t)((buf[1] << 8) | buf[0]);
    raw->y = (int16_t)((buf[3] << 8) | buf[2]);
    raw->z = (int16_t)((buf[5] << 8) | buf[4]);
    return ESP_OK;
}

esp_err_t lis3mdl_read_data(lis3mdl_data_t *data)
{
    lis3mdl_raw_t raw;
    esp_err_t ret = lis3mdl_read_raw(&raw);
    if (ret != ESP_OK) return ret;

    float sens = get_sensitivity();
    data->x = ((float)(raw.x - offset_x) / sens) * GAUSS_TO_UT;
    data->y = ((float)(raw.y - offset_y) / sens) * GAUSS_TO_UT;
    data->z = ((float)(raw.z)             / sens) * GAUSS_TO_UT;
    return ESP_OK;
}

esp_err_t lis3mdl_read_temperature(float *temp_c)
{
    uint8_t buf[2] = {0};
    esp_err_t ret = lis3mdl_read_regs(LIS3MDL_REG_TEMP_OUT_L, buf, 2);
    if (ret != ESP_OK) return ret;

    int16_t raw = (int16_t)((buf[1] << 8) | buf[0]);
    *temp_c = ((float)raw / 8.0f) + 25.0f;     /* 8 LSB/°C, 0 = 25 °C */
    return ESP_OK;
}

/*******************************************************************************
 * Heading
 ******************************************************************************/

/**
 * @brief Compute compass heading from horizontal magnetic field.
 *
 * Algorithm (standard hard/soft-iron compensated 2-D compass):
 *  1. Read raw X, Y.
 *  2. Subtract hard-iron offsets (centres the field circle at the origin).
 *  3. Divide by per-axis scale factors (turns an ellipse into a circle).
 *  4. heading = atan2(mx, my)   [east-component, north-component]
 *     – LIS3MDL face-up datasheet axes: X ≈ right, Y ≈ forward, Z = up.
 *     – atan2(east, north) naturally gives CW-from-north compass bearing.
 *  5. Normalise to [0, 360) and apply EMA smoothing.
 *
 * If calibration is in progress the min/max trackers are updated first.
 */
esp_err_t lis3mdl_get_heading(float *heading)
{
    /* Only read when the sensor has fresh data — avoids re-reading stale
       registers (BDU is enabled, so registers lock after a read until
       new data arrives).  Return the smoothed heading from last time.
       Exception: during calibration, always read so every poll collects
       a min/max sample.                                                  */
    if (!calibrating && !lis3mdl_data_ready()) {
        if (ema_heading >= 0.0f) {
            *heading = ema_heading;
            return ESP_OK;
        }
        /* No data yet at all — wait briefly for first sample */
    }

    lis3mdl_raw_t raw;
    esp_err_t ret = lis3mdl_read_raw(&raw);
    if (ret != ESP_OK) return ret;

    /* ── Update calibration min/max if active ───────────────────────────── */
    if (calibrating) {
        cal_sample_count++;
        if (cal_sample_count > 5) {              /* skip pipeline flush    */
            if (raw.x < cal_x_min) cal_x_min = raw.x;
            if (raw.x > cal_x_max) cal_x_max = raw.x;
            if (raw.y < cal_y_min) cal_y_min = raw.y;
            if (raw.y > cal_y_max) cal_y_max = raw.y;
        }
    }

    /* ── Hard-iron correction ───────────────────────────────────────────── */
    float mx = (float)(raw.x - offset_x);
    float my = (float)(raw.y - offset_y);

    /* ── Soft-iron normalisation ────────────────────────────────────────── */
    mx /= scale_x;
    my /= scale_y;

    /* ── Heading ────────────────────────────────────────────────────────── */
    /*  Empirically derived from raw cardinal readings with +X aimed:
     *    N: X=1546 Y=-2234  → mx≈0  my=large negative
     *    E: X=6850 Y=2551   → mx=large positive  my≈0
     *  East component = mx,  North component = -my.
     *  CW-from-north bearing = atan2(east, north) = atan2(mx, -my).         */
    float hdg = atan2f(mx, -my) * (180.0f / (float)M_PI);
    hdg -= HEADING_DECLINATION;               /* magnetic → true north      */
    if (hdg < 0.0f)   hdg += 360.0f;
    if (hdg >= 360.0f) hdg -= 360.0f;

    *heading = smooth_heading(hdg);
    return ESP_OK;
}

/*******************************************************************************
 * Calibration
 ******************************************************************************/

void lis3mdl_start_calibration(void)
{
    cal_x_min = INT16_MAX;  cal_x_max = INT16_MIN;
    cal_y_min = INT16_MAX;  cal_y_max = INT16_MIN;
    cal_sample_count = 0;
    calibrating = true;
    ema_heading = -1.0f;                         /* reset smoother */
    ESP_LOGW(TAG, "Calibration started — rotate slowly through 360 deg");
}

void lis3mdl_stop_calibration(void)
{
    calibrating = false;

    int sx = cal_x_max - cal_x_min;
    int sy = cal_y_max - cal_y_min;
    int weaker = (sx < sy) ? sx : sy;
    ESP_LOGW(TAG, "stop_cal: samples=%d  spread_x=%d  spread_y=%d  weaker=%d  min_req=%d  good_req=%d",
             cal_sample_count, sx, sy, weaker, CAL_MIN_SAMPLES, CAL_GOOD_SPREAD);

    if (cal_sample_count < CAL_MIN_SAMPLES) {
        ESP_LOGW(TAG, "Too few samples (%d/%d) — cancelled",
                 cal_sample_count, CAL_MIN_SAMPLES);
        return;
    }

    /* Hard-iron offset = midpoint (int32 avoids overflow) */
    offset_x = (int16_t)(((int32_t)cal_x_min + (int32_t)cal_x_max) / 2);
    offset_y = (int16_t)(((int32_t)cal_y_min + (int32_t)cal_y_max) / 2);

    /* Soft-iron scale = half-spread; clamp ≥ 1 to avoid div-by-zero */
    int spread_x = cal_x_max - cal_x_min;
    int spread_y = cal_y_max - cal_y_min;
    scale_x = (spread_x > 1) ? (float)spread_x / 2.0f : 1.0f;
    scale_y = (spread_y > 1) ? (float)spread_y / 2.0f : 1.0f;

    /* Reset smoother so it adapts immediately to new offsets */
    ema_heading = -1.0f;

    ESP_LOGW(TAG, "Calibration done (%d samples)", cal_sample_count);
    ESP_LOGW(TAG, "  X [%d .. %d] spread=%d  offset=%d  scale=%.0f",
             cal_x_min, cal_x_max, spread_x, offset_x, scale_x);
    ESP_LOGW(TAG, "  Y [%d .. %d] spread=%d  offset=%d  scale=%.0f",
             cal_y_min, cal_y_max, spread_y, offset_y, scale_y);
}

bool lis3mdl_is_calibrating(void)
{
    return calibrating;
}

int lis3mdl_get_cal_sample_count(void)
{
    return cal_sample_count;
}

float lis3mdl_get_cal_progress(void)
{
    if (!calibrating || cal_sample_count <= 5) return 0.0f;

    int sx = cal_x_max - cal_x_min;
    int sy = cal_y_max - cal_y_min;

    /* Progress = weaker axis / target (both axes must contribute) */
    float px = (float)sx / (float)CAL_TARGET_SPREAD;
    float py = (float)sy / (float)CAL_TARGET_SPREAD;
    float p  = (px < py) ? px : py;
    if (p > 1.0f) p = 1.0f;
    if (p < 0.0f) p = 0.0f;
    return p;
}

bool lis3mdl_cal_is_good(void)
{
    int sx = cal_x_max - cal_x_min;
    int sy = cal_y_max - cal_y_min;
    int weaker = (sx < sy) ? sx : sy;
    return (cal_sample_count >= CAL_MIN_SAMPLES) && (weaker >= CAL_GOOD_SPREAD);
}

void lis3mdl_set_calibration(const lis3mdl_calibration_t *cal)
{
    offset_x = cal->x_offset;
    offset_y = cal->y_offset;
    scale_x  = (cal->scale_x > 0.0f) ? cal->scale_x : 1.0f;
    scale_y  = (cal->scale_y > 0.0f) ? cal->scale_y : 1.0f;
    ema_heading = -1.0f;
    ESP_LOGI(TAG, "Calibration set: offset=(%d, %d) scale=(%.0f, %.0f)",
             offset_x, offset_y, scale_x, scale_y);
}

void lis3mdl_get_calibration(lis3mdl_calibration_t *cal)
{
    cal->x_offset = offset_x;
    cal->y_offset = offset_y;
    cal->z_offset = 0;
    cal->scale_x  = scale_x;
    cal->scale_y  = scale_y;
}
