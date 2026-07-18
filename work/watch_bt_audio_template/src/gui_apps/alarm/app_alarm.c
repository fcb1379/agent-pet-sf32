#include <rtthread.h>
#include <stdint.h>

#include "littlevgl2rtt.h"
#include "lv_ext_resource_manager.h"
#include "gui_app_fwk.h"
#include "watch_alarm_service.h"

#define APP_ID "alarm"

typedef enum
{
    ALARM_BUTTON_HOUR_DOWN = 1,
    ALARM_BUTTON_HOUR_UP,
    ALARM_BUTTON_MINUTE_DOWN,
    ALARM_BUTTON_MINUTE_UP,
    ALARM_BUTTON_TOGGLE,
    ALARM_BUTTON_DISMISS,
} alarm_button_t;

typedef struct
{
    lv_obj_t *root;
    lv_obj_t *value;
    lv_obj_t *state;
    lv_obj_t *toggle_label;
    lv_obj_t *dismiss_label;
    lv_timer_t *refresh_timer;
} alarm_ui_t;

static alarm_ui_t g_alarm_ui;

static void alarm_refresh(void)
{
    watch_alarm_snapshot_t snapshot;
    char value[12];

    watch_alarm_get_snapshot(&snapshot);
    rt_snprintf(value, sizeof(value), "%02d:%02d", snapshot.alarm_hour, snapshot.alarm_minute);
    lv_label_set_text(g_alarm_ui.value, value);
    lv_label_set_text(g_alarm_ui.toggle_label, snapshot.alarm_enabled ? "Disable" : "Enable");
    lv_label_set_text(g_alarm_ui.dismiss_label, snapshot.alarm_ringing ? "Stop Ring" : "Dismiss");
    if (snapshot.alarm_ringing)
    {
        lv_label_set_text(g_alarm_ui.state, "Alarm ringing");
    }
    else if (snapshot.alarm_enabled)
    {
        lv_label_set_text(g_alarm_ui.state, "Daily alarm on");
    }
    else
    {
        lv_label_set_text(g_alarm_ui.state, "Daily alarm off");
    }
}

static void alarm_refresh_callback(lv_timer_t *timer)
{
    (void)timer;
    alarm_refresh();
}

static void alarm_button_event(lv_event_t *event)
{
    alarm_button_t button;
    watch_alarm_snapshot_t snapshot;
    uint8_t hour;
    uint8_t minute;

    if (lv_event_get_code(event) != LV_EVENT_SHORT_CLICKED)
    {
        return;
    }
    button = (alarm_button_t)(uintptr_t)lv_event_get_user_data(event);
    watch_alarm_get_snapshot(&snapshot);
    hour = snapshot.alarm_hour;
    minute = snapshot.alarm_minute;

    if (button == ALARM_BUTTON_HOUR_DOWN)
    {
        hour = hour == 0 ? 23 : hour - 1;
        watch_alarm_set(snapshot.alarm_enabled, hour, minute);
    }
    else if (button == ALARM_BUTTON_HOUR_UP)
    {
        hour = hour == 23 ? 0 : hour + 1;
        watch_alarm_set(snapshot.alarm_enabled, hour, minute);
    }
    else if (button == ALARM_BUTTON_MINUTE_DOWN)
    {
        minute = minute < 5 ? 55 : minute - 5;
        watch_alarm_set(snapshot.alarm_enabled, hour, minute);
    }
    else if (button == ALARM_BUTTON_MINUTE_UP)
    {
        minute = minute >= 55 ? 0 : minute + 5;
        watch_alarm_set(snapshot.alarm_enabled, hour, minute);
    }
    else if (button == ALARM_BUTTON_TOGGLE)
    {
        watch_alarm_set(!snapshot.alarm_enabled, hour, minute);
    }
    else if (button == ALARM_BUTTON_DISMISS)
    {
        watch_alarm_dismiss();
    }
    alarm_refresh();
}

static lv_obj_t *alarm_create_button(lv_obj_t *parent, const char *text, lv_coord_t x,
                                     lv_coord_t y, lv_coord_t width, lv_coord_t height,
                                     alarm_button_t button, lv_color_t color)
{
    lv_obj_t *object = lv_obj_create(parent);
    lv_obj_t *label;

    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_bg_color(object, color, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(object, 5, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(object, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(object, alarm_button_event, LV_EVENT_SHORT_CLICKED,
                        (void *)(uintptr_t)button);

    label = lv_label_create(object);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    return object;
}

static void alarm_on_start(void)
{
    const lv_coord_t margin = 12;
    const lv_coord_t gap = 8;
    const lv_coord_t button_width = (LV_HOR_RES_MAX - margin * 2 - gap) / 2;
    const lv_coord_t button_height = 36;
    lv_obj_t *title;
    lv_obj_t *toggle_button;
    lv_obj_t *dismiss_button;

    rt_memset(&g_alarm_ui, 0, sizeof(g_alarm_ui));
    g_alarm_ui.root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_alarm_ui.root, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_set_style_bg_color(g_alarm_ui.root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_alarm_ui.root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_alarm_ui.root, 0, 0);
    lv_obj_set_style_pad_all(g_alarm_ui.root, 0, 0);
    lv_obj_clear_flag(g_alarm_ui.root, LV_OBJ_FLAG_SCROLLABLE);

    title = lv_label_create(g_alarm_ui.root);
    lv_label_set_text(title, "Alarm");
    lv_obj_set_pos(title, margin, 8);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x9bd3ff), 0);

    g_alarm_ui.value = lv_label_create(g_alarm_ui.root);
    lv_obj_set_pos(g_alarm_ui.value, margin, 35);
    lv_obj_set_size(g_alarm_ui.value, LV_HOR_RES_MAX - margin * 2, 45);
    lv_obj_set_style_text_align(g_alarm_ui.value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_alarm_ui.value, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(g_alarm_ui.value, lv_color_white(), 0);

    g_alarm_ui.state = lv_label_create(g_alarm_ui.root);
    lv_obj_set_pos(g_alarm_ui.state, margin, 82);
    lv_obj_set_size(g_alarm_ui.state, LV_HOR_RES_MAX - margin * 2, 20);
    lv_obj_set_style_text_align(g_alarm_ui.state, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_alarm_ui.state, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(g_alarm_ui.state, lv_color_hex(0x8ca4bf), 0);

    alarm_create_button(g_alarm_ui.root, "H-", margin, 110, button_width, button_height,
                        ALARM_BUTTON_HOUR_DOWN, lv_color_hex(0x26364a));
    alarm_create_button(g_alarm_ui.root, "H+", margin + button_width + gap, 110,
                        button_width, button_height, ALARM_BUTTON_HOUR_UP,
                        lv_color_hex(0x26364a));
    alarm_create_button(g_alarm_ui.root, "M-", margin, 154, button_width, button_height,
                        ALARM_BUTTON_MINUTE_DOWN, lv_color_hex(0x26364a));
    alarm_create_button(g_alarm_ui.root, "M+", margin + button_width + gap, 154,
                        button_width, button_height, ALARM_BUTTON_MINUTE_UP,
                        lv_color_hex(0x26364a));
    toggle_button = alarm_create_button(g_alarm_ui.root, "Enable", margin, 198,
                                        button_width, button_height, ALARM_BUTTON_TOGGLE,
                                        lv_color_hex(0x2475c1));
    dismiss_button = alarm_create_button(g_alarm_ui.root, "Dismiss", margin + button_width + gap,
                                         198, button_width, button_height, ALARM_BUTTON_DISMISS,
                                         lv_color_hex(0x49566b));
    g_alarm_ui.toggle_label = lv_obj_get_child(toggle_button, 0);
    g_alarm_ui.dismiss_label = lv_obj_get_child(dismiss_button, 0);
    g_alarm_ui.refresh_timer = lv_timer_create(alarm_refresh_callback, 500, NULL);
    alarm_refresh();
}

static void alarm_on_stop(void)
{
    if (g_alarm_ui.refresh_timer)
    {
        lv_timer_del(g_alarm_ui.refresh_timer);
    }
    if (g_alarm_ui.root)
    {
        lv_obj_del(g_alarm_ui.root);
    }
    rt_memset(&g_alarm_ui, 0, sizeof(g_alarm_ui));
}

static void alarm_msg_handler(gui_app_msg_type_t msg, void *param)
{
    (void)param;
    if (msg == GUI_APP_MSG_ONSTART)
    {
        alarm_on_start();
    }
    else if (msg == GUI_APP_MSG_ONSTOP)
    {
        alarm_on_stop();
    }
}

static int alarm_app_main(intent_t intent)
{
    (void)intent;
    gui_app_regist_msg_handler(APP_ID, alarm_msg_handler);
    return 0;
}

LV_IMG_DECLARE(img_alarm);
BUILTIN_APP_EXPORT(LV_EXT_STR_ID(alarm), LV_EXT_IMG_GET(img_alarm), APP_ID, alarm_app_main);
