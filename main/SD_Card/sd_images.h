/**
 * @file sd_images.h
 * @brief Load custom vehicle images from SD card with compiled-in fallbacks
 *
 * At init, checks for .bin image files on the SD card (/sdcard/).
 * If found, loads them into PSRAM and uses them instead of the compiled-in
 * defaults. Falls back to compiled-in images if files are missing.
 *
 * Expected SD card files (8.3 names, LVGL binary format):
 *   rear.bin     — Rear-view vehicle image (Tilt gauge, day mode)
 *   reardark.bin — Rear-view vehicle image (Tilt gauge, night mode)
 *   side.bin     — Side-view vehicle image (Incline gauge, day mode)
 *   sidedark.bin — Side-view vehicle image (Incline gauge, night mode)
 *   roof.bin     — Roof-view vehicle image (TPMS gauge, day mode)
 *   roofdark.bin — Roof-view vehicle image (TPMS gauge, night mode)
 *   logo.bin     — Clock face logo image (day mode)
 *   logodark.bin — Clock face logo image (night mode)
 *
 * Generate .bin files with:  python3 convert_image.py --bin
 */

#pragma once

#include "lvgl.h"

/**
 * @brief Load custom images from SD card (call after SD_Init and before gauge init)
 *
 * Scans for .bin files and loads any found into PSRAM.
 * Safe to call even if no SD card is present — all images fall back to defaults.
 */
void sd_images_init(void);

/** @brief Get the rear-view image for the given display mode */
const lv_img_dsc_t *sd_images_get_rear(bool night_mode);

/** @brief Get the side-view image for the given display mode */
const lv_img_dsc_t *sd_images_get_side(bool night_mode);

/** @brief Get the roof-view image for the given display mode */
const lv_img_dsc_t *sd_images_get_roof(bool night_mode);

/** @brief Get the clock face logo image for the given display mode */
const lv_img_dsc_t *sd_images_get_logo(bool night_mode);
