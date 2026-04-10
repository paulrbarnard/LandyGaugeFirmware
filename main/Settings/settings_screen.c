/**
 * @file settings_screen.c
 * @brief Hidden settings screen — WiFi credentials + TPMS sensor pairing
 *
 * Layout on 360×360 circular display:
 *   - Main menu: list of items, highlight bar moves with nav
 *   - WiFi editor: roller-style character picker for SSID/password
 *   - TPMS learn: step-by-step sensor removal flow
 */

#include "settings_screen.h"
#include "settings.h"
#include "lvgl.h"
#include "esp_log.h"
#include "BLE_TPMS/ble_tpms.h"
#include "warning_beep.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "SETTINGS_UI";

/* ── Colours ──────────────────────────────────────────────────────── */
#define COLOR_ACCENT_DAY   lv_color_white()
#define COLOR_ACCENT_NIGHT lv_color_make(0, 200, 0)
#define COLOR_DIM          lv_color_make(100, 100, 100)
#define COLOR_HIGHLIGHT    lv_color_make(0, 100, 200)
#define COLOR_CONFIGURED   lv_color_make(0, 200, 0)
#define COLOR_BG           lv_color_black()

static lv_color_t accent_color;
static bool night_mode = false;

/* ── Screen states ────────────────────────────────────────────────── */
typedef enum {
    SS_MENU,           // Main menu list
    SS_WIFI_PICK,      // Pick which WiFi to edit (home / phone)
    SS_WIFI_EDIT,      // Character-by-character text editor
    SS_TPMS_LEARN,     // TPMS learn flow (step through FL/FR/RL/RR)
    SS_TPMS_DONE,      // TPMS learn complete message
    SS_TZ_PICK,        // Timezone roller picker
    SS_INFO,           // About / info screen
} settings_state_t;

static settings_state_t state = SS_MENU;

/* ── Main menu ────────────────────────────────────────────────────── */
#define MENU_ITEMS  5
static const char *menu_labels[MENU_ITEMS] = {
    "WiFi Setup",
    "TPMS Learn",
    "Timezone",
    "About",
    "Exit",
};
static int menu_sel = 0;

/* ── LVGL objects ─────────────────────────────────────────────────── */
static lv_obj_t *container = NULL;
static lv_obj_t *title_label = NULL;
static lv_obj_t *item_labels[MENU_ITEMS] = {0};
static bool       item_configured[MENU_ITEMS] = {0}; // Green indicator per item
static lv_obj_t *info_label = NULL;       // Multi-purpose info text
static lv_obj_t *status_label = NULL;     // Bottom status/hint text
static lv_obj_t *char_label = NULL;       // Current editing character
static lv_obj_t *edit_str_label = NULL;   // Full string being edited

/* ── WiFi editor state ────────────────────────────────────────────── */
static int wifi_pick_sel = 0;             // 0=home, 1=phone, 2=back
#define WIFI_PICK_COUNT 3
static const char *wifi_pick_labels[WIFI_PICK_COUNT] = {"Home Network", "Phone Hotspot", "Back"};

/* ── Timezone picker state ────────────────────────────────────────── */
static int tz_pick_sel = 0;

// Character set for text input — \x7f at end acts as DEL
#define DEL_CHAR '\x7f'
static const char charset[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
    "0123456789 .-_'@!#$%&*+=/\\:;,?()\"\x7f";
#define CHARSET_LEN  (sizeof(charset) - 1)

static char edit_buf[65] = "";            // Buffer being edited
static int  edit_pos = 0;                 // Cursor position in edit_buf
static int  edit_char_idx = 0;            // Index into charset for current char
static int  edit_max_len = 32;            // Max length (32 for SSID, 64 for pass)
static bool editing_password = false;     // true = editing password, false = SSID
static bool editing_phone = false;        // true = phone network, false = home
static char edit_ssid_buf[33] = "";       // Saved SSID while editing password

/* ── TPMS learn state ─────────────────────────────────────────────── */
static const char *position_names[TPMS_POSITION_COUNT] = {
    "FRONT LEFT", "FRONT RIGHT", "REAR LEFT", "REAR RIGHT"
};
static uint32_t learn_anim_tick = 0;

/* ── Forward declarations ─────────────────────────────────────────── */
static void show_menu(void);
static void show_wifi_pick(void);
static void show_wifi_editor(const char *initial, int max_len, bool is_pass, bool is_phone);
static void show_tpms_learn(void);
static void show_tz_pick(void);
static void show_info(void);
static void update_menu_highlight(void);
static void clear_content(void);
static void set_hint(const char *text);

/*******************************************************************************
 * Init / Cleanup
 ******************************************************************************/

void settings_screen_init(void)
{
    accent_color = night_mode ? COLOR_ACCENT_NIGHT : COLOR_ACCENT_DAY;

    lv_obj_t *scr = lv_scr_act();

    container = lv_obj_create(scr);
    lv_obj_set_size(container, 360, 360);
    lv_obj_set_pos(container, 0, 0);
    lv_obj_set_style_bg_color(container, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_CLICKABLE);

    state = SS_MENU;
    menu_sel = 0;
    show_menu();
}

void settings_screen_cleanup(void)
{
    /* If TPMS learn was active, stop it */
    if (ble_tpms_learn_active()) {
        ble_tpms_learn_stop();
    }

    if (container) {
        lv_obj_del(container);
        container = NULL;
    }
    title_label = NULL;
    info_label = NULL;
    status_label = NULL;
    char_label = NULL;
    edit_str_label = NULL;
    memset(item_labels, 0, sizeof(item_labels));
}

void settings_screen_set_night_mode(bool night)
{
    night_mode = night;
    accent_color = night ? COLOR_ACCENT_NIGHT : COLOR_ACCENT_DAY;
}

/*******************************************************************************
 * Helpers
 ******************************************************************************/

static void clear_content(void)
{
    if (!container) return;
    /* Delete all children except container itself */
    lv_obj_clean(container);
    title_label = NULL;
    info_label = NULL;
    status_label = NULL;
    char_label = NULL;
    edit_str_label = NULL;
    memset(item_labels, 0, sizeof(item_labels));
}

static lv_obj_t *create_label(lv_obj_t *parent, const lv_font_t *font,
                               lv_color_t color, lv_align_t align, int x, int y)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_align(lbl, align, x, y);
    return lbl;
}

static void set_hint(const char *text)
{
    if (!status_label) {
        status_label = create_label(container, &lv_font_montserrat_12,
                                    COLOR_DIM, LV_ALIGN_BOTTOM_MID, 0, -30);
    }
    lv_label_set_text(status_label, text);
}

/*******************************************************************************
 * Main menu
 ******************************************************************************/

static void show_menu(void)
{
    clear_content();
    state = SS_MENU;

    title_label = create_label(container, &lv_font_montserrat_16,
                               accent_color, LV_ALIGN_TOP_MID, 0, 50);
    lv_label_set_text(title_label, "SETTINGS");

    /* Check which items have saved config */
    memset(item_configured, 0, sizeof(item_configured));
    item_configured[0] = (settings_get_wifi_home_ssid()[0] != '\0') ||
                         (settings_get_wifi_phone_ssid()[0] != '\0');
    uint8_t tmp_mac[6];
    for (int i = 0; i < 4; i++)
        if (settings_get_tpms_mac(i, tmp_mac)) { item_configured[1] = true; break; }
    item_configured[2] = settings_timezone_configured();

    int y_start = 90;
    int y_step = 32;
    for (int i = 0; i < MENU_ITEMS; i++) {
        lv_color_t color = item_configured[i] ? COLOR_CONFIGURED : accent_color;
        item_labels[i] = create_label(container, &lv_font_montserrat_16,
                                      color, LV_ALIGN_TOP_MID, 0, y_start + i * y_step);
        lv_label_set_text(item_labels[i], menu_labels[i]);
    }

    set_hint("Prev/Next: navigate   Select: choose");
    update_menu_highlight();
}

static void update_menu_highlight(void)
{
    for (int i = 0; i < MENU_ITEMS; i++) {
        if (!item_labels[i]) continue;
        lv_color_t item_color = item_configured[i] ? COLOR_CONFIGURED : accent_color;
        if (i == menu_sel) {
            lv_obj_set_style_text_color(item_labels[i], COLOR_BG, 0);
            lv_obj_set_style_bg_color(item_labels[i], item_color, 0);
            lv_obj_set_style_bg_opa(item_labels[i], LV_OPA_COVER, 0);
            lv_obj_set_style_pad_hor(item_labels[i], 12, 0);
            lv_obj_set_style_pad_ver(item_labels[i], 4, 0);
        } else {
            lv_obj_set_style_text_color(item_labels[i], item_color, 0);
            lv_obj_set_style_bg_opa(item_labels[i], LV_OPA_TRANSP, 0);
            lv_obj_set_style_pad_hor(item_labels[i], 0, 0);
            lv_obj_set_style_pad_ver(item_labels[i], 0, 0);
        }
    }
}

/*******************************************************************************
 * WiFi pick (home / phone / back)
 ******************************************************************************/

static void show_wifi_pick(void)
{
    clear_content();
    state = SS_WIFI_PICK;
    wifi_pick_sel = 0;

    title_label = create_label(container, &lv_font_montserrat_16,
                               accent_color, LV_ALIGN_TOP_MID, 0, 50);
    lv_label_set_text(title_label, "WiFi SETUP");

    memset(item_configured, 0, sizeof(item_configured));
    item_configured[0] = (settings_get_wifi_home_ssid()[0] != '\0');
    item_configured[1] = (settings_get_wifi_phone_ssid()[0] != '\0');

    int y_start = 110;
    int y_step = 36;
    for (int i = 0; i < WIFI_PICK_COUNT; i++) {
        lv_color_t color = item_configured[i] ? COLOR_CONFIGURED : accent_color;
        item_labels[i] = create_label(container, &lv_font_montserrat_16,
                                      color, LV_ALIGN_TOP_MID, 0, y_start + i * y_step);
        lv_label_set_text(item_labels[i], wifi_pick_labels[i]);
    }

    /* Show current SSIDs below */
    info_label = create_label(container, &lv_font_montserrat_12,
                              COLOR_DIM, LV_ALIGN_BOTTOM_MID, 0, -50);
    const char *h = settings_get_wifi_home_ssid();
    const char *p = settings_get_wifi_phone_ssid();
    static char buf[80];
    snprintf(buf, sizeof(buf), "Home: %s\nPhone: %s",
             h[0] ? h : "(default)", p[0] ? p : "(default)");
    lv_label_set_text(info_label, buf);
    lv_obj_set_style_text_align(info_label, LV_TEXT_ALIGN_CENTER, 0);

    set_hint("Prev/Next: navigate   Select: choose");

    /* Reuse the menu highlight logic — item_labels[0..2] */
    for (int i = 0; i < WIFI_PICK_COUNT; i++) {
        if (i == wifi_pick_sel) {
            lv_color_t item_color = item_configured[i] ? COLOR_CONFIGURED : accent_color;
            lv_obj_set_style_text_color(item_labels[i], COLOR_BG, 0);
            lv_obj_set_style_bg_color(item_labels[i], item_color, 0);
            lv_obj_set_style_bg_opa(item_labels[i], LV_OPA_COVER, 0);
            lv_obj_set_style_pad_hor(item_labels[i], 12, 0);
            lv_obj_set_style_pad_ver(item_labels[i], 4, 0);
        }
    }
}

/*******************************************************************************
 * WiFi text editor (roller-style character picker)
 ******************************************************************************/

static void update_wifi_editor_display(void);

static void show_wifi_editor(const char *initial, int max_len, bool is_pass, bool is_phone)
{
    clear_content();
    state = SS_WIFI_EDIT;
    editing_password = is_pass;
    editing_phone = is_phone;
    edit_max_len = max_len;

    strncpy(edit_buf, initial, sizeof(edit_buf) - 1);
    edit_buf[sizeof(edit_buf) - 1] = '\0';
    int len = strlen(edit_buf);

    if (len > 0) {
        /* Position on last character and set roller to match it */
        edit_pos = len - 1;
        const char *p = strchr(charset, edit_buf[edit_pos]);
        edit_char_idx = p ? (int)(p - charset) : 0;
    } else {
        edit_pos = 0;
        edit_char_idx = 0;
    }

    title_label = create_label(container, &lv_font_montserrat_16,
                               accent_color, LV_ALIGN_TOP_MID, 0, 40);
    {
        static char title_buf[40];
        snprintf(title_buf, sizeof(title_buf), "%s %s",
                 is_phone ? "Phone" : "Home",
                 is_pass ? "PASSWORD" : "SSID");
        lv_label_set_text(title_label, title_buf);
    }

    /* Current string display */
    edit_str_label = create_label(container, &lv_font_montserrat_14,
                                  accent_color, LV_ALIGN_TOP_MID, 0, 75);
    lv_obj_set_width(edit_str_label, 300);
    lv_obj_set_style_text_align(edit_str_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(edit_str_label, LV_LABEL_LONG_SCROLL_CIRCULAR);

    /* Large character roller display */
    char_label = create_label(container, &lv_font_montserrat_32,
                              accent_color, LV_ALIGN_CENTER, 0, -10);

    /* Navigation hints */
    info_label = create_label(container, &lv_font_montserrat_12,
                              COLOR_DIM, LV_ALIGN_CENTER, 0, 35);
    lv_label_set_text(info_label, "Prev / Next: change letter");

    set_hint(is_pass ? "Hold Select: SAVE   Select: advance"
                     : "Hold Select: NEXT   Select: advance");

    update_wifi_editor_display();
}

static void update_wifi_editor_display(void)
{
    if (!edit_str_label || !char_label) return;

    /* Show the string with cursor position indicated by [ ] */
    static char disp[200];
    int len = strlen(edit_buf);
    char cur_ch = charset[edit_char_idx];
    const char *cur_str = (cur_ch == DEL_CHAR) ? "DEL" : NULL;

    if (edit_pos >= len) {
        /* Cursor at end — appending mode */
        if (cur_str)
            snprintf(disp, sizeof(disp), "%s[%s]", edit_buf, cur_str);
        else
            snprintf(disp, sizeof(disp), "%s[%c]", edit_buf, cur_ch);
    } else {
        /* Cursor in middle — editing existing char */
        char before[65] = "";
        if (edit_pos > 0) {
            strncpy(before, edit_buf, edit_pos);
            before[edit_pos] = '\0';
        }
        if (cur_str)
            snprintf(disp, sizeof(disp), "%s[%s]%s", before,
                     cur_str, &edit_buf[edit_pos + 1]);
        else
            snprintf(disp, sizeof(disp), "%s[%c]%s", before,
                     cur_ch, &edit_buf[edit_pos + 1]);
    }
    lv_label_set_text(edit_str_label, disp);

    /* Show the large character */
    if (cur_ch == DEL_CHAR) {
        lv_label_set_text(char_label, "DEL");
    } else {
        static char ch_buf[4];
        snprintf(ch_buf, sizeof(ch_buf), "%c", cur_ch);
        lv_label_set_text(char_label, ch_buf);
    }
}

/*******************************************************************************
 * TPMS learn screen
 ******************************************************************************/

static void show_tpms_learn(void)
{
    clear_content();
    state = SS_TPMS_LEARN;

    ble_tpms_learn_start();

    title_label = create_label(container, &lv_font_montserrat_16,
                               accent_color, LV_ALIGN_TOP_MID, 0, 50);
    lv_label_set_text(title_label, "TPMS LEARN");

    info_label = create_label(container, &lv_font_montserrat_16,
                              accent_color, LV_ALIGN_CENTER, 0, -20);

    status_label = create_label(container, &lv_font_montserrat_12,
                                COLOR_DIM, LV_ALIGN_CENTER, 0, 30);

    set_hint("Select: accept   Next: skip");

    /* Show initial instruction */
    lv_label_set_text(info_label, "Remove sensor\nFRONT LEFT");
    lv_obj_set_style_text_align(info_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(status_label, "Scanning...");
}

static void show_tpms_done(void)
{
    clear_content();
    state = SS_TPMS_DONE;

    title_label = create_label(container, &lv_font_montserrat_16,
                               accent_color, LV_ALIGN_TOP_MID, 0, 50);
    lv_label_set_text(title_label, "TPMS LEARN");

    info_label = create_label(container, &lv_font_montserrat_16,
                              lv_color_make(0, 255, 0), LV_ALIGN_CENTER, 0, -10);
    lv_label_set_text(info_label, "ALL SENSORS\nPAIRED");
    lv_obj_set_style_text_align(info_label, LV_TEXT_ALIGN_CENTER, 0);

    set_hint("Select: return");
}

/*******************************************************************************
 * Info / About screen
 ******************************************************************************/

static void show_info(void)
{
    clear_content();
    state = SS_INFO;

    title_label = create_label(container, &lv_font_montserrat_16,
                               accent_color, LV_ALIGN_TOP_MID, 0, 50);
    lv_label_set_text(title_label, "ABOUT");

    info_label = create_label(container, &lv_font_montserrat_14,
                              accent_color, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(info_label, "Landy Gauge v1.0\n\nBy Paul Barnard\n\ngithub.com/pbarnard/\nLandyGauge");
    lv_obj_set_style_text_align(info_label, LV_TEXT_ALIGN_CENTER, 0);

    set_hint("Select: return");
}

/*******************************************************************************
 * Timezone picker — scroll through zones with Next/Prev
 ******************************************************************************/

static void show_tz_pick(void)
{
    clear_content();
    state = SS_TZ_PICK;
    tz_pick_sel = settings_get_timezone_index();

    title_label = create_label(container, &lv_font_montserrat_16,
                               accent_color, LV_ALIGN_TOP_MID, 0, 50);
    lv_label_set_text(title_label, "TIMEZONE");

    /* Large timezone name display */
    edit_str_label = create_label(container, &lv_font_montserrat_16,
                                  accent_color, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_width(edit_str_label, 280);
    lv_obj_set_style_text_align(edit_str_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(edit_str_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(edit_str_label, settings_get_timezone_name(tz_pick_sel));

    set_hint("Prev/Next: browse   Select: save");
}

/*******************************************************************************
 * Navigation
 ******************************************************************************/

void settings_screen_navigate(int dir)
{
    switch (state) {
    case SS_MENU:
        menu_sel += dir;
        if (menu_sel < 0) menu_sel = MENU_ITEMS - 1;
        if (menu_sel >= MENU_ITEMS) menu_sel = 0;
        update_menu_highlight();
        break;

    case SS_WIFI_PICK:
        wifi_pick_sel += dir;
        if (wifi_pick_sel < 0) wifi_pick_sel = WIFI_PICK_COUNT - 1;
        if (wifi_pick_sel >= WIFI_PICK_COUNT) wifi_pick_sel = 0;
        /* Update highlight */
        for (int i = 0; i < WIFI_PICK_COUNT; i++) {
            if (!item_labels[i]) continue;
            if (i == wifi_pick_sel) {
                lv_color_t item_color = item_configured[i] ? COLOR_CONFIGURED : accent_color;
                lv_obj_set_style_text_color(item_labels[i], COLOR_BG, 0);
                lv_obj_set_style_bg_color(item_labels[i], item_color, 0);
                lv_obj_set_style_bg_opa(item_labels[i], LV_OPA_COVER, 0);
                lv_obj_set_style_pad_hor(item_labels[i], 12, 0);
                lv_obj_set_style_pad_ver(item_labels[i], 4, 0);
            } else {
                lv_color_t item_color = item_configured[i] ? COLOR_CONFIGURED : accent_color;
                lv_obj_set_style_text_color(item_labels[i], item_color, 0);
                lv_obj_set_style_bg_opa(item_labels[i], LV_OPA_TRANSP, 0);
                lv_obj_set_style_pad_hor(item_labels[i], 0, 0);
                lv_obj_set_style_pad_ver(item_labels[i], 0, 0);
            }
        }
        break;

    case SS_WIFI_EDIT:
        if (dir > 0) {
            /* Right: advance cursor (confirm current char) */
            int len = strlen(edit_buf);
            if (charset[edit_char_idx] == DEL_CHAR) {
                /* DEL selected — delete char at/before cursor */
                if (edit_pos > 0 && edit_pos <= len) {
                    int del_pos = (edit_pos >= len) ? edit_pos - 1 : edit_pos;
                    memmove(&edit_buf[del_pos], &edit_buf[del_pos + 1],
                            len - del_pos);
                    edit_pos = del_pos;
                }
                /* Set roller to match char at new cursor position */
                len = strlen(edit_buf);
                if (edit_pos < len) {
                    const char *p = strchr(charset, edit_buf[edit_pos]);
                    edit_char_idx = p ? (int)(p - charset) : 0;
                } else {
                    edit_char_idx = 0;
                }
            } else if (edit_pos >= len && len < edit_max_len) {
                /* Append the current character */
                edit_buf[len] = charset[edit_char_idx];
                edit_buf[len + 1] = '\0';
                edit_pos = len + 1;
                edit_char_idx = 0;  // Reset to 'A' for next char
            } else if (edit_pos < len) {
                /* Replace current char and advance */
                edit_buf[edit_pos] = charset[edit_char_idx];
                edit_pos++;
                if (edit_pos < (int)strlen(edit_buf)) {
                    /* Set roller to match existing char at new position */
                    const char *p = strchr(charset, edit_buf[edit_pos]);
                    edit_char_idx = p ? (int)(p - charset) : 0;
                } else {
                    edit_char_idx = 0;
                }
            }
        } else {
            /* Left: backspace / move cursor back */
            if (edit_pos > 0) {
                edit_pos--;
                /* Set roller to match char at cursor */
                if (edit_pos < (int)strlen(edit_buf)) {
                    const char *p = strchr(charset, edit_buf[edit_pos]);
                    edit_char_idx = p ? (int)(p - charset) : 0;
                }
            }
        }
        update_wifi_editor_display();
        break;

    case SS_TPMS_LEARN:
        /* Up/down scrolls through charset in editor; in learn mode, use
           up = skip this position, down = not used */
        if (dir > 0) {
            ESP_LOGI(TAG, "TPMS learn: skipping %s",
                     ble_tpms_position_str(ble_tpms_learn_current_position()));
            ble_tpms_learn_skip();
        }
        break;

    case SS_TZ_PICK: {
        int count = settings_get_timezone_count();
        tz_pick_sel += dir;
        if (tz_pick_sel < 0) tz_pick_sel = count - 1;
        if (tz_pick_sel >= count) tz_pick_sel = 0;
        /* Update display */
        if (edit_str_label)
            lv_label_set_text(edit_str_label, settings_get_timezone_name(tz_pick_sel));
        break;
    }

    default:
        break;
    }
}

/*******************************************************************************
 * Up/Down for character roller (touch top/bottom half in edit mode,
 * or mapped from main.c for button-only operation)
 ******************************************************************************/

void settings_screen_char_change(int dir)
{
    if (state != SS_WIFI_EDIT) return;

    edit_char_idx += dir;
    if (edit_char_idx < 0) edit_char_idx = CHARSET_LEN - 1;
    if (edit_char_idx >= (int)CHARSET_LEN) edit_char_idx = 0;

    update_wifi_editor_display();
}

/*******************************************************************************
 * Select / Confirm
 ******************************************************************************/

void settings_screen_select(void)
{
    switch (state) {
    case SS_MENU:
        switch (menu_sel) {
        case 0: /* WiFi Setup */
            show_wifi_pick();
            break;
        case 1: /* TPMS Learn */
            show_tpms_learn();
            break;
        case 2: /* Timezone */
            show_tz_pick();
            break;
        case 3: /* About */
            show_info();
            break;
        case 4: /* Exit */
            /* Main.c will detect this and switch back to clock */
            state = SS_MENU;  /* Signal handled by main.c checking settings_screen_wants_exit() */
            break;
        }
        break;

    case SS_WIFI_PICK:
        if (wifi_pick_sel == 2) {
            /* Back */
            show_menu();
        } else {
            /* Start editing SSID for the selected network */
            editing_phone = (wifi_pick_sel == 1);
            const char *existing_ssid = editing_phone ?
                settings_get_wifi_phone_ssid() : settings_get_wifi_home_ssid();
            show_wifi_editor(existing_ssid, 32, false, editing_phone);
        }
        break;

    case SS_WIFI_EDIT: {
        /* Long-press = done editing current field */
        /* First, commit any pending character at cursor */
        int len = strlen(edit_buf);
        if (edit_pos >= len && len < edit_max_len && len > 0) {
            /* Don't append — cursor is past end, string is complete */
        } else if (edit_pos < len) {
            /* Replace char at cursor */
            edit_buf[edit_pos] = charset[edit_char_idx];
        }

        if (!editing_password) {
            /* Just finished SSID — save it and move to password */
            strncpy(edit_ssid_buf, edit_buf, sizeof(edit_ssid_buf) - 1);
            edit_ssid_buf[sizeof(edit_ssid_buf) - 1] = '\0';
            ESP_LOGI(TAG, "SSID entered: %s", edit_ssid_buf);

            const char *existing_pass = editing_phone ?
                settings_get_wifi_phone_pass() : settings_get_wifi_home_pass();
            show_wifi_editor(existing_pass, 64, true, editing_phone);
        } else {
            /* Finished password — save both to NVS */
            ESP_LOGI(TAG, "Password entered, saving credentials");
            if (editing_phone) {
                settings_save_wifi_phone(edit_ssid_buf, edit_buf);
            } else {
                settings_save_wifi_home(edit_ssid_buf, edit_buf);
            }
            warning_beep_play(BEEP_SHORT);
            show_wifi_pick();  /* Return to WiFi selection */
        }
        break;
    }

    case SS_TPMS_LEARN:
        /* If a result is ready, accept it */
        if (ble_tpms_learn_check_result(NULL)) {
            uint8_t mac[6];
            ble_tpms_learn_check_result(mac);
            ble_tpms_learn_accept();
            settings_save_tpms_mac(ble_tpms_learn_current_position() - 1, mac);
            warning_beep_play(BEEP_SHORT);

            if (!ble_tpms_learn_active()) {
                /* Save all and show done screen */
                settings_save_all_tpms_macs();
                show_tpms_done();
            }
        }
        break;

    case SS_TPMS_DONE:
    case SS_INFO:
        show_menu();
        break;

    case SS_TZ_PICK:
        /* Confirm timezone selection */
        settings_save_timezone((uint8_t)tz_pick_sel);
        warning_beep_play(BEEP_SHORT);
        show_menu();
        break;
    }
}

/*******************************************************************************
 * Backspace for editor (mapped from left-tap in edit mode)
 ******************************************************************************/

void settings_screen_backspace(void)
{
    if (state != SS_WIFI_EDIT) return;

    int len = strlen(edit_buf);
    if (len > 0 && edit_pos <= len) {
        /* Delete the character before the cursor */
        if (edit_pos > 0) {
            memmove(&edit_buf[edit_pos - 1], &edit_buf[edit_pos], len - edit_pos + 1);
            edit_pos--;
        } else if (len > 0) {
            memmove(&edit_buf[0], &edit_buf[1], len);
        }

        /* Update roller to match char at new position */
        len = strlen(edit_buf);
        if (edit_pos < len) {
            const char *p = strchr(charset, edit_buf[edit_pos]);
            edit_char_idx = p ? (int)(p - charset) : 0;
        } else {
            edit_char_idx = 0;
        }
    }
    update_wifi_editor_display();
}

/*******************************************************************************
 * Periodic update
 ******************************************************************************/

void settings_screen_update(void)
{
    if (state != SS_TPMS_LEARN) return;
    if (!ble_tpms_learn_active()) return;

    /* Update the learn screen with current status */
    tpms_position_t pos = ble_tpms_learn_current_position();

    if (info_label) {
        if (ble_tpms_learn_check_result(NULL)) {
            /* Sensor detected! */
            uint8_t mac[6];
            ble_tpms_learn_check_result(mac);
            static char buf[60];
            snprintf(buf, sizeof(buf), "%s\nDETECTED!\n%02X:%02X:%02X:%02X:%02X:%02X",
                     position_names[pos],
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
            lv_label_set_text(info_label, buf);
            lv_obj_set_style_text_color(info_label, lv_color_make(0, 255, 0), 0);

            if (status_label) {
                lv_label_set_text(status_label, "Hold to confirm");
            }
        } else {
            /* Still waiting */
            static char buf[40];
            snprintf(buf, sizeof(buf), "Remove sensor\n%s",
                     pos < TPMS_POSITION_COUNT ? position_names[pos] : "DONE");
            lv_label_set_text(info_label, buf);
            lv_obj_set_style_text_color(info_label, accent_color, 0);

            /* Animated scanning indicator */
            learn_anim_tick++;
            if (status_label) {
                int dots = (learn_anim_tick / 50) % 4;  // ~500ms per dot
                static const char *anim[] = {"Scanning", "Scanning.", "Scanning..", "Scanning..."};
                int seen = ble_tpms_learn_discovered_count();
                static char sbuf[40];
                snprintf(sbuf, sizeof(sbuf), "%s  (%d sensors seen)", anim[dots], seen);
                lv_label_set_text(status_label, sbuf);
            }
        }
    }
}

/*******************************************************************************
 * State queries
 ******************************************************************************/

bool settings_screen_editing(void)
{
    return state == SS_WIFI_EDIT;
}

bool settings_screen_wants_exit(void)
{
    return (state == SS_MENU && menu_sel == 4);
}
