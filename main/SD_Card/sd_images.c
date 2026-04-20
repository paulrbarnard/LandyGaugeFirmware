/**
 * @file sd_images.c
 * @brief Load custom vehicle images from SD card with compiled-in fallbacks
 */

#include "sd_images.h"
#include "SD_MMC.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "SD_IMG";

/* ── Compiled-in fallback images ─────────────────────────────────── */
LV_IMG_DECLARE(rear_110_235);
LV_IMG_DECLARE(rear_dark_110_235);
LV_IMG_DECLARE(side_110_292w);
LV_IMG_DECLARE(side_dark_110_292w);
LV_IMG_DECLARE(roof_110_150w);
LV_IMG_DECLARE(roof_dark_110_150w);
LV_IMG_DECLARE(logo_img);
LV_IMG_DECLARE(logo_dark_img);

/* ── SD card image slots ─────────────────────────────────────────── */
typedef struct {
    const char       *filename;     /* SD card path */
    const lv_img_dsc_t *fallback;   /* compiled-in default */
    lv_img_dsc_t      loaded;       /* descriptor for SD image */
    uint8_t           *data;        /* pixel data in PSRAM */
    bool               valid;       /* true if loaded from SD */
} sd_image_slot_t;

enum {
    SLOT_REAR_DAY = 0,
    SLOT_REAR_NIGHT,
    SLOT_SIDE_DAY,
    SLOT_SIDE_NIGHT,
    SLOT_ROOF_DAY,
    SLOT_ROOF_NIGHT,
    SLOT_LOGO_DAY,
    SLOT_LOGO_NIGHT,
    SLOT_COUNT
};

static sd_image_slot_t slots[SLOT_COUNT] = {
    [SLOT_REAR_DAY]   = { "/sdcard/rear.bin",     &rear_110_235,       {}, NULL, false },
    [SLOT_REAR_NIGHT] = { "/sdcard/reardark.bin",  &rear_dark_110_235,  {}, NULL, false },
    [SLOT_SIDE_DAY]   = { "/sdcard/side.bin",      &side_110_292w,      {}, NULL, false },
    [SLOT_SIDE_NIGHT] = { "/sdcard/sidedark.bin",  &side_dark_110_292w, {}, NULL, false },
    [SLOT_ROOF_DAY]   = { "/sdcard/roof.bin",      &roof_110_150w,      {}, NULL, false },
    [SLOT_ROOF_NIGHT] = { "/sdcard/roofdark.bin",  &roof_dark_110_150w, {}, NULL, false },
    [SLOT_LOGO_DAY]   = { "/sdcard/logo.bin",      &logo_img,           {}, NULL, false },
    [SLOT_LOGO_NIGHT] = { "/sdcard/logodark.bin",  &logo_dark_img,      {}, NULL, false },
};

/*
 * .bin file format (LVGL v8 binary image):
 *   Offset 0:  lv_img_header_t (4 bytes, packed bitfield)
 *   Offset 4:  raw pixel data  (w * h * 3 bytes for RGB565+alpha)
 *
 * lv_img_header_t layout (32 bits):
 *   bits [0:4]   = cf (color format, 5 = LV_IMG_CF_TRUE_COLOR_ALPHA)
 *   bit  [5]     = always_zero
 *   bits [6:7]   = reserved
 *   bits [8:18]  = w (11 bits)
 *   bits [19:29] = h (11 bits)
 *   bits [30:31] = reserved
 */

static bool load_slot(sd_image_slot_t *slot)
{
    FILE *f = fopen(slot->filename, "rb");
    if (!f) return false;

    /* Read the 4-byte LVGL image header */
    uint32_t raw_header = 0;
    if (fread(&raw_header, 1, 4, f) != 4) {
        ESP_LOGW(TAG, "%s: too short for header", slot->filename);
        fclose(f);
        return false;
    }

    /* Parse header fields */
    uint8_t  cf = raw_header & 0x1F;
    uint16_t w  = (raw_header >> 8) & 0x7FF;
    uint16_t h  = (raw_header >> 19) & 0x7FF;

    if (cf != LV_IMG_CF_TRUE_COLOR_ALPHA || w == 0 || h == 0 || w > 400 || h > 400) {
        ESP_LOGW(TAG, "%s: invalid header (cf=%d, %dx%d)", slot->filename, cf, w, h);
        fclose(f);
        return false;
    }

    /* Calculate expected data size: w * h * 3 bytes (RGB565 swapped + alpha) */
    uint32_t pixel_count = (uint32_t)w * h;
    uint32_t data_size = pixel_count * LV_IMG_PX_SIZE_ALPHA_BYTE;

    /* Allocate in PSRAM */
    uint8_t *buf = heap_caps_malloc(data_size, MALLOC_CAP_SPIRAM);
    if (!buf) {
        ESP_LOGW(TAG, "%s: PSRAM alloc failed (%lu bytes)", slot->filename, (unsigned long)data_size);
        fclose(f);
        return false;
    }

    /* Read pixel data */
    size_t read = fread(buf, 1, data_size, f);
    fclose(f);

    if (read != data_size) {
        ESP_LOGW(TAG, "%s: data truncated (%u of %lu bytes)",
                 slot->filename, (unsigned)read, (unsigned long)data_size);
        heap_caps_free(buf);
        return false;
    }

    /* Build the descriptor */
    slot->loaded.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    slot->loaded.header.always_zero = 0;
    slot->loaded.header.reserved = 0;
    slot->loaded.header.w = w;
    slot->loaded.header.h = h;
    slot->loaded.data_size = data_size;
    slot->loaded.data = buf;
    slot->data = buf;
    slot->valid = true;

    ESP_LOGI(TAG, "Loaded %s (%dx%d, %lu bytes)", slot->filename, w, h, (unsigned long)data_size);
    return true;
}

void sd_images_init(void)
{
    if (SDCard_Size == 0) {
        ESP_LOGI(TAG, "No SD card — using built-in images");
        return;
    }

    int loaded = 0;
    for (int i = 0; i < SLOT_COUNT; i++) {
        if (load_slot(&slots[i])) loaded++;
    }

    ESP_LOGI(TAG, "Custom images: %d of %d loaded from SD card", loaded, SLOT_COUNT);
}

const lv_img_dsc_t *sd_images_get_rear(bool night_mode)
{
    sd_image_slot_t *s = &slots[night_mode ? SLOT_REAR_NIGHT : SLOT_REAR_DAY];
    return s->valid ? &s->loaded : s->fallback;
}

const lv_img_dsc_t *sd_images_get_side(bool night_mode)
{
    sd_image_slot_t *s = &slots[night_mode ? SLOT_SIDE_NIGHT : SLOT_SIDE_DAY];
    return s->valid ? &s->loaded : s->fallback;
}

const lv_img_dsc_t *sd_images_get_roof(bool night_mode)
{
    sd_image_slot_t *s = &slots[night_mode ? SLOT_ROOF_NIGHT : SLOT_ROOF_DAY];
    return s->valid ? &s->loaded : s->fallback;
}

const lv_img_dsc_t *sd_images_get_logo(bool night_mode)
{
    sd_image_slot_t *s = &slots[night_mode ? SLOT_LOGO_NIGHT : SLOT_LOGO_DAY];
    return s->valid ? &s->loaded : s->fallback;
}
