/*
 * Copyright (c) 2026 Paul Barnard (Toxic Celery)
 */

/**
 * @file user_input.h
 * @brief Unified input abstraction for all user interaction methods
 *
 * Combines physical buttons (next/prev/combo-select) and touch screen
 * into a single event stream.
 *
 * Events produced:
 *   INPUT_NEXT           Next / scroll down  (auto-repeats on button hold)
 *   INPUT_PREV           Prev / scroll up    (auto-repeats on button hold)
 *   INPUT_SELECT         Single select tap
 *   INPUT_SELECT_DOUBLE  Double-tap select   (NORMAL mode only)
 *   INPUT_SELECT_LONG    Long-press select   (>1 s hold)
 *
 * Input sources:
 *   1. GPIO buttons: GPIO0/43 = next, GPIO44 = prev, combo = select
 *   2. Touch screen: fed via user_input_feed_*() from LVGL handler
 *
 * Button auto-repeat : 400 ms initial delay, then every 150 ms.
 * Combo tolerance    : 150 ms window for pressing both buttons.
 * Double-tap window  : 500 ms between taps.
 * Long-press threshold: 1000 ms hold.
 */

#ifndef USER_INPUT_H
#define USER_INPUT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    INPUT_NONE = 0,
    INPUT_NEXT,
    INPUT_PREV,
    INPUT_SELECT,
    INPUT_SELECT_DOUBLE,
    INPUT_SELECT_LONG,
} input_event_t;

typedef enum {
    INPUT_MODE_NORMAL,   /**< Double-tap active; single tap emits after timeout */
    INPUT_MODE_DIRECT,   /**< Tap fires INPUT_SELECT immediately; no double-tap */
} input_mode_t;

/** Initialise the unified input system (call once) */
void user_input_init(void);

/** Set input mode — clears any pending double-tap */
void user_input_set_mode(input_mode_t mode);

/** Get current input mode */
input_mode_t user_input_get_mode(void);

/**
 * @brief Poll for the next input event.
 *
 * Handles button polling, expansion-board select, auto-repeat timers,
 * and double-tap timeouts internally.
 *
 * Drain with:  while ((evt = user_input_poll()) != INPUT_NONE) { ... }
 *
 * @return Next event, or INPUT_NONE.
 */
input_event_t user_input_poll(void);

/** Feed a NEXT event from the touch handler */
void user_input_feed_next(void);

/** Feed a PREV event from the touch handler */
void user_input_feed_prev(void);

/** Feed a select tap from the touch handler (centre short-click) */
void user_input_feed_select_tap(void);

/** Feed a select long-press from the touch handler (centre long-press) */
void user_input_feed_select_long(void);

/** Reset pending state (double-tap, queue) — call on gauge switch */
void user_input_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* USER_INPUT_H */
