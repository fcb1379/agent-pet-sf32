#include <rtthread.h>
#include <stdint.h>

#include "littlevgl2rtt.h"
#include "lv_ext_resource_manager.h"
#include "gui_app_fwk.h"
#include "watch_alarm_service.h"

#define APP_ID "timer"
#define TIMER_DEFAULT_SECONDS (5U * 60U)
#define TIMER_MAX_SECONDS (24U * 60U * 60U)

typedef enum
{
    TIMER_BUTTON_ADD_ONE = 1,
    TIMER_BUTTON_ADD_FIVE,
    TIMER_BUTTON_START_PAUSE,
    TIMER_BUTTON_RESET,
} timer_button_t;

typedef struct
{
    lv_obj_t *root;
    lv_obj_t *value;
    lv_obj_t *state;
    lv_obj_t *start_pause;
    lv_timer_t *refresh_timer;
    uint32_t configured_seconds;
} timer_ui_t;

static timer_ui_t g_timer_ui;

static void timer_format(uint32_t seconds, char *buffer, size_t buffer_size)
{
    rt_snprintf(buffer, buffer_size, "%02lu:%02lu:%02lu",
                (unsigned long)(seconds / 3600U),
                (unsigned long)((seconds / 60U) % 60U),
                (unsigned long)(seconds % 60U));
}

static void timer_refresh(void)
{
    watch_alarm_snapshot_t snapshot;
    char value[16];

    watch_alarm_get_snapshot(&snapshot);
    if (snapshot.timer_running || snapshot.timer_remaining_seconds > 0)
    {
        g_timer_ui.configured_seconds = snapshot.timer_remaining_seconds;
    }
    timer_format(g_timer_ui.configured_seconds, value, sizeof(value));
    lv_label_set_text(g_timer_ui.value, value);

    if (snapshot.timer_ringing)
    {
        lv_label_set_text(g_timer_ui.state, "Done - tap Reset");
        lv_label_set_text(g_timer_ui.start_pause, "Start");
    }
    else if (snapshot.timer_running)
    {
        lv_label_set_text(g_timer_ui.state, "Running");
        lv_label_set_text(g_timer_ui.start_pause, "Pause");
    }
    else
    {
        lv_label_set_text(g_timer_ui.state, "Ready");
        lv_label_set_text(g_timer_ui.start_pause, "Start");
    }
}

static void timer_refresh_callback(lv_timer_t *timer)
{
    (void)timer;
    timer_refresh();
}

static void timer_button_event(lv_event_t *event)
{
    timer_button_t button;
    watch_alarm_snapshot_t snapshot;

    if (lv_event_get_code(event) != LV_EVENT_SHORT_CLICKED)
    {
        return;
    }
    button = (timer_button_t)(uintptr_t)lv_event_get_user_data(event);
    watch_alarm_get_snapshot(&snapshot);

    if (button == TIMER_BUTTON_ADD_ONE && !snapshot.timer_running)
    {
        g_timer_ui.configured_seconds = LV_MIN(g_timer_ui.configured_seconds + 60U,
                                                TIMER_MAX_SECONDS);
    }
    else if (button == TIMER_BUTTON_ADD_FIVE && !snapshot.timer_running)
    {
        g_timer_ui.configured_seconds = LV_MIN(g_timer_ui.configured_seconds + 300U,
                                                TIMER_MAX_SECONDS);
    }
    else if (button == TIMER_BUTTON_START_PAUSE)
    {
        if (snapshot.timer_running)
        {
            watch_timer_pause();
        }
        else
        {
            if (g_timer_ui.configured_seconds == 0)
            {
                g_timer_ui.configured_seconds = TIMER_DEFAULT_SECONDS;
            }
            watch_timer_start(g_timer_ui.configured_seconds);
        }
    }
    else if (button == TIMER_BUTTON_RESET)
    {
        watch_timer_reset();
        g_timer_ui.configured_seconds = TIMER_DEFAULT_SECONDS;
    }

    timer_refresh();
}

static lv_obj_t *timer_create_button(lv_obj_t *parent, const char *text, lv_coord_t x,
                                     lv_coord_t y, lv_coord_t width, lv_coord_t height,
                                     timer_button_t button, lv_color_t color)
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
    lv_obj_add_event_cb(object, timer_button_event, LV_EVENT_SHORT_CLICKED,
                        (void *)(uintptr_t)button);

    label = lv_label_create(object);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    return object;
}

static void timer_on_start(void)
{
    const lv_coord_t margin = 12;
    const lv_coord_t gap = 8;
    const lv_coord_t button_width = (LV_HOR_RES_MAX - margin * 2 - gap) / 2;
    const lv_coord_t button_height = 44;
    lv_obj_t *title;
    lv_obj_t *start_button;

    rt_memset(&g_timer_ui, 0, sizeof(g_timer_ui));
    g_timer_ui.configured_seconds = TIMER_DEFAULT_SECONDS;
    g_timer_ui.root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_timer_ui.root, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_set_style_bg_color(g_timer_ui.root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_timer_ui.root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_timer_ui.root, 0, 0);
    lv_obj_set_style_pad_all(g_timer_ui.root, 0, 0);
    lv_obj_clear_flag(g_timer_ui.root, LV_OBJ_FLAG_SCROLLABLE);

    title = lv_label_create(g_timer_ui.root);
    lv_label_set_text(title, "Timer");
    lv_obj_set_pos(title, margin, 10);
    lv_obj_set_style_text_color(title, lv_color_hex(0x9bd3ff), 0);

    g_timer_ui.value = lv_label_create(g_timer_ui.root);
    lv_obj_set_pos(g_timer_ui.value, margin, 44);
    lv_obj_set_size(g_timer_ui.value, LV_HOR_RES_MAX - margin * 2, 46);
    lv_obj_set_style_text_align(g_timer_ui.value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(g_timer_ui.value, lv_color_white(), 0);

    g_timer_ui.state = lv_label_create(g_timer_ui.root);
    lv_obj_set_pos(g_timer_ui.state, margin, 94);
    lv_obj_set_size(g_timer_ui.state, LV_HOR_RES_MAX - margin * 2, 22);
    lv_obj_set_style_text_align(g_timer_ui.state, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(g_timer_ui.state, lv_color_hex(0x8ca4bf), 0);

    timer_create_button(g_timer_ui.root, "+1m", margin, 128, button_width, button_height,
                        TIMER_BUTTON_ADD_ONE, lv_color_hex(0x26364a));
    timer_create_button(g_timer_ui.root, "+5m", margin + button_width + gap, 128,
                        button_width, button_height, TIMER_BUTTON_ADD_FIVE,
                        lv_color_hex(0x26364a));
    start_button = timer_create_button(g_timer_ui.root, "Start", margin, 180,
                                       button_width, button_height,
                                       TIMER_BUTTON_START_PAUSE, lv_color_hex(0x2475c1));
    g_timer_ui.start_pause = lv_obj_get_child(start_button, 0);
    timer_create_button(g_timer_ui.root, "Reset", margin + button_width + gap, 180,
                        button_width, button_height, TIMER_BUTTON_RESET, lv_color_hex(0x49566b));

    g_timer_ui.refresh_timer = lv_timer_create(timer_refresh_callback, 250, NULL);
    timer_refresh();
}

static void timer_on_stop(void)
{
    if (g_timer_ui.refresh_timer)
    {
        lv_timer_del(g_timer_ui.refresh_timer);
    }
    if (g_timer_ui.root)
    {
        lv_obj_del(g_timer_ui.root);
    }
    rt_memset(&g_timer_ui, 0, sizeof(g_timer_ui));
}

static void timer_msg_handler(gui_app_msg_type_t msg, void *param)
{
    (void)param;
    if (msg == GUI_APP_MSG_ONSTART)
    {
        timer_on_start();
    }
    else if (msg == GUI_APP_MSG_ONSTOP)
    {
        timer_on_stop();
    }
}

static int timer_app_main(intent_t intent)
{
    (void)intent;
    gui_app_regist_msg_handler(APP_ID, timer_msg_handler);
    return 0;
}

LV_IMG_DECLARE(img_timer);
BUILTIN_APP_EXPORT(LV_EXT_STR_ID(timer), LV_EXT_IMG_GET(img_timer), APP_ID, timer_app_main);
