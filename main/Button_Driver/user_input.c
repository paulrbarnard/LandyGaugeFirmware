/**
 * @file user_input.c
 * @brief Unified input handler — merges buttons, expansion board & touch
 *
 * State machines:
 *   1. Button combo / auto-repeat  (physical GPIO buttons)
 *   2. Expansion-board select      (edge + long-press tracking)
 *   3. Select double-tap detector  (shared by all sources)
 *
 * Touch events are injected via user_input_feed_*() from the LVGL handler.
 * Everything else is polled directly from hardware.
 */

#include "user_input.h"
#include "button_input.h"
#include "expansion_board.h"
#include "warning_beep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "INPUT";

/* ══════════════════════════════════════════════════════════════════
 *  Timing constants
 * ══════════════════════════════════════════════════════════════════ */
#define DOUBLE_TAP_WINDOW_MS    400
#define LONG_PRESS_MS          1000
#define COMBO_TOLERANCE_MS      150
#define REPEAT_INITIAL_MS       400
#define REPEAT_INTERVAL_MS      150

/* ══════════════════════════════════════════════════════════════════
 *  Event queue
 * ══════════════════════════════════════════════════════════════════ */
#define QUEUE_SIZE 8
static input_event_t queue[QUEUE_SIZE];
static int q_head = 0, q_tail = 0, q_count = 0;

static inline void enqueue(input_event_t evt)
{
    if (q_count >= QUEUE_SIZE) return;   /* drop if full */
    queue[q_head] = evt;
    q_head = (q_head + 1) % QUEUE_SIZE;
    q_count++;
}

static inline input_event_t dequeue(void)
{
    if (q_count <= 0) return INPUT_NONE;
    input_event_t evt = queue[q_tail];
    q_tail = (q_tail + 1) % QUEUE_SIZE;
    q_count--;
    return evt;
}

/* ══════════════════════════════════════════════════════════════════
 *  Mode
 * ══════════════════════════════════════════════════════════════════ */
static input_mode_t current_mode = INPUT_MODE_NORMAL;

/* ══════════════════════════════════════════════════════════════════
 *  Helpers
 * ══════════════════════════════════════════════════════════════════ */
static inline uint32_t now_ms(void)
{
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

/* ══════════════════════════════════════════════════════════════════
 *  Select double-tap state machine
 *
 *  SEL_IDLE    → tap → SEL_PENDING  (record timestamp)
 *  SEL_PENDING → tap within 500 ms → emit SELECT_DOUBLE, → IDLE
 *  SEL_PENDING → 500 ms timeout    → emit SELECT, → IDLE
 *  Any state   → long-press        → cancel pending, emit SELECT_LONG
 * ══════════════════════════════════════════════════════════════════ */
typedef enum { SEL_IDLE, SEL_PENDING } sel_state_t;
static sel_state_t sel_state = SEL_IDLE;
static uint32_t    sel_tap_time = 0;

static void handle_select_tap(void)
{
    if (current_mode == INPUT_MODE_DIRECT) {
        enqueue(INPUT_SELECT);
        return;
    }

    uint32_t now = now_ms();
    if (sel_state == SEL_PENDING &&
        (now - sel_tap_time) <= DOUBLE_TAP_WINDOW_MS) {
        /* Second tap inside window */
        sel_state = SEL_IDLE;
        enqueue(INPUT_SELECT_DOUBLE);
    } else {
        /* First tap — start window */
        sel_state = SEL_PENDING;
        sel_tap_time = now;
    }
}

static void handle_select_long(void)
{
    sel_state = SEL_IDLE;              /* cancel any pending double-tap */
    warning_beep_play(BEEP_SHORT);     /* audible confirmation */
    enqueue(INPUT_SELECT_LONG);
}

static void check_select_timeout(uint32_t now)
{
    if (sel_state != SEL_PENDING) return;
    if ((now - sel_tap_time) > DOUBLE_TAP_WINDOW_MS) {
        sel_state = SEL_IDLE;
        enqueue(INPUT_SELECT);         /* single-tap expired */
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  Button state machine (GPIO next / prev / combo)
 *
 *  BTN_IDLE       – nothing pressed
 *  BTN_WAIT_COMBO – one button pressed, waiting for the other
 *  BTN_COMBO      – both held, tracking long-press
 *  BTN_COMBO_DONE – long-press already fired, waiting for release
 *  BTN_NEXT_HELD  – next button held, auto-repeating
 *  BTN_PREV_HELD  – prev button held, auto-repeating
 * ══════════════════════════════════════════════════════════════════ */
typedef enum {
    BTN_IDLE,
    BTN_WAIT_COMBO,
    BTN_COMBO,
    BTN_COMBO_DONE,
    BTN_NEXT_HELD,
    BTN_PREV_HELD,
} btn_state_t;

static btn_state_t btn_state       = BTN_IDLE;
static uint32_t    btn_start_time  = 0;
static uint32_t    btn_repeat_time = 0;
static bool        btn_wait_is_next = false;

static void poll_buttons(uint32_t now)
{
    bool next_held = button_input_next_held();
    bool prev_held = button_input_prev_held();
    bool both      = next_held && prev_held;

    switch (btn_state) {

    /* ── IDLE ──────────────────────────────────────────────────── */
    case BTN_IDLE: {
        bool next_edge = button_input_pressed();
        bool prev_edge = button_input_prev_pressed();

        if (next_edge && prev_edge) {
            btn_state      = BTN_COMBO;
            btn_start_time = now;
        } else if (next_edge) {
            btn_state        = BTN_WAIT_COMBO;
            btn_wait_is_next = true;
            btn_start_time   = now;
        } else if (prev_edge) {
            btn_state        = BTN_WAIT_COMBO;
            btn_wait_is_next = false;
            btn_start_time   = now;
        }
        break;
    }

    /* ── WAIT_COMBO: one button pressed, waiting for the other ── */
    case BTN_WAIT_COMBO:
        if (both) {
            /* Second button arrived → combo */
            btn_state      = BTN_COMBO;
            btn_start_time = now;
            button_input_pressed();       /* consume edges */
            button_input_prev_pressed();
        } else if (btn_wait_is_next && !next_held) {
            /* Quick tap — button already released */
            enqueue(INPUT_NEXT);
            btn_state = BTN_IDLE;
            button_input_pressed();
        } else if (!btn_wait_is_next && !prev_held) {
            enqueue(INPUT_PREV);
            btn_state = BTN_IDLE;
            button_input_prev_pressed();
        } else if ((now - btn_start_time) >= COMBO_TOLERANCE_MS) {
            /* Tolerance expired → commit to single button */
            if (btn_wait_is_next) {
                enqueue(INPUT_NEXT);
                btn_state       = BTN_NEXT_HELD;
                btn_start_time  = now;
                btn_repeat_time = now;
            } else {
                enqueue(INPUT_PREV);
                btn_state       = BTN_PREV_HELD;
                btn_start_time  = now;
                btn_repeat_time = now;
            }
        }
        break;

    /* ── COMBO: both buttons held — tracking long-press ────────── */
    case BTN_COMBO:
        if (both) {
            if ((now - btn_start_time) >= LONG_PRESS_MS) {
                handle_select_long();
                btn_state = BTN_COMBO_DONE;
            }
        } else {
            /* Released before long-press threshold → short select tap */
            handle_select_tap();
            btn_state = BTN_IDLE;
            button_input_pressed();
            button_input_prev_pressed();
        }
        break;

    /* ── COMBO_DONE: long-press fired, waiting for release ────── */
    case BTN_COMBO_DONE:
        if (!both) {
            btn_state = BTN_IDLE;
            button_input_pressed();
            button_input_prev_pressed();
        }
        break;

    /* ── NEXT_HELD: auto-repeat ───────────────────────────────── */
    case BTN_NEXT_HELD:
        if (!next_held) {
            btn_state = BTN_IDLE;
        } else {
            bool initial = (btn_repeat_time == btn_start_time);
            uint32_t threshold = initial ? REPEAT_INITIAL_MS : REPEAT_INTERVAL_MS;
            if ((now - btn_repeat_time) >= threshold) {
                enqueue(INPUT_NEXT);
                btn_repeat_time = now;
            }
        }
        break;

    /* ── PREV_HELD: auto-repeat ───────────────────────────────── */
    case BTN_PREV_HELD:
        if (!prev_held) {
            btn_state = BTN_IDLE;
        } else {
            bool initial = (btn_repeat_time == btn_start_time);
            uint32_t threshold = initial ? REPEAT_INITIAL_MS : REPEAT_INTERVAL_MS;
            if ((now - btn_repeat_time) >= threshold) {
                enqueue(INPUT_PREV);
                btn_repeat_time = now;
            }
        }
        break;
    }
}



/* ══════════════════════════════════════════════════════════════════
 *  Public API
 * ══════════════════════════════════════════════════════════════════ */

void user_input_init(void)
{
    q_head = q_tail = q_count = 0;
    sel_state       = SEL_IDLE;
    btn_state       = BTN_IDLE;
    current_mode    = INPUT_MODE_NORMAL;
    ESP_LOGI(TAG, "Unified input initialised (repeat=%d/%dms, combo=%dms, "
             "double-tap=%dms, long=%dms)",
             REPEAT_INITIAL_MS, REPEAT_INTERVAL_MS, COMBO_TOLERANCE_MS,
             DOUBLE_TAP_WINDOW_MS, LONG_PRESS_MS);
}

void user_input_set_mode(input_mode_t m)
{
    if (m != current_mode) {
        current_mode = m;
        sel_state = SEL_IDLE;
        ESP_LOGD(TAG, "Input mode → %s", m == INPUT_MODE_DIRECT ? "DIRECT" : "NORMAL");
    }
}

input_mode_t user_input_get_mode(void)
{
    return current_mode;
}

input_event_t user_input_poll(void)
{
    /* Drain queued events first */
    if (q_count > 0) return dequeue();

    /* Queue was empty — do the actual work */
    uint32_t now = now_ms();
    poll_buttons(now);
    check_select_timeout(now);

    if (q_count > 0) return dequeue();
    return INPUT_NONE;
}

void user_input_feed_next(void)         { enqueue(INPUT_NEXT); }
void user_input_feed_prev(void)         { enqueue(INPUT_PREV); }
void user_input_feed_select_tap(void)   { handle_select_tap(); }
void user_input_feed_select_long(void)  { handle_select_long(); }

void user_input_reset(void)
{
    q_head = q_tail = q_count = 0;
    sel_state = SEL_IDLE;
    /* Don't reset btn_state — buttons may still be physically held */
}
