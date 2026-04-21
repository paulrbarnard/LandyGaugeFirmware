/*
 * Copyright (c) 2026 Paul Barnard (Toxic Celery)
 */

/**
 * @file settings_screen.h
 * @brief Hidden settings screen — WiFi credentials + TPMS sensor pairing
 *
 * Entry: double-tap centre while on the Clock gauge.
 * Navigation: left/right to navigate, long-press to select/confirm.
 * Works with touch, physical buttons, and expansion board select.
 */

#ifndef SETTINGS_SCREEN_H
#define SETTINGS_SCREEN_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Create the settings screen UI on the active LVGL screen */
void settings_screen_init(void);

/** Destroy all settings screen objects */
void settings_screen_cleanup(void);

/** Set night mode colours */
void settings_screen_set_night_mode(bool night);

/**
 * @brief Handle navigation input from main.c
 * @param dir  -1 = prev/left/up, +1 = next/right/down
 */
void settings_screen_navigate(int dir);

/** Handle select/confirm action (long-press or select button) */
void settings_screen_select(void);

/** Periodic update (call from main LVGL loop, ~10ms) */
void settings_screen_update(void);

/** Change roller character in text-edit mode: dir -1=prev, +1=next */
void settings_screen_char_change(int dir);

/** Delete character before cursor in text-edit mode */
void settings_screen_backspace(void);

/** @return true if screen is in text-editing mode (suppresses gauge nav) */
bool settings_screen_editing(void);

/** @return true if user selected "Exit" from main menu */
bool settings_screen_wants_exit(void);

#ifdef __cplusplus
}
#endif

#endif // SETTINGS_SCREEN_H
