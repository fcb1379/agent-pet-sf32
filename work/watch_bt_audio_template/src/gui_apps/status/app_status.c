#include <rtthread.h>
#include <stdint.h>

#include "littlevgl2rtt.h"
#include "lv_ext_resource_manager.h"
#include "gui_app_fwk.h"
#include "badge_transfer.h"
#include "ble_ios_services.h"
#include "bt_audio_sink.h"
#include "watch_settings.h"

#define APP_ID "status"
#define STATUS_REFRESH_MS 1000

typedef enum
{
    STATUS_ACTION_BT_RECOVER = 1,
    STATUS_ACTION_BADGE_CLEAR,
} status_action_t;

typedef struct
{
    lv_obj_t *root;
    lv_obj_t *content;
    lv_obj_t *last_action;
    lv_timer_t *refresh_timer;
    char last_action_text[48];
    char content_text[768];
} status_ui_t;

static status_ui_t g_status_ui;

static const char *status_text_or_dash(const char *text)
{
    return (text && text[0]) ? text : "-";
}

static void status_update_action_text(const char *text)
{
    rt_snprintf(g_status_ui.last_action_text, sizeof(g_status_ui.last_action_text), "%s", text);
    if (g_status_ui.last_action)
    {
        lv_obj_invalidate(g_status_ui.last_action);
        lv_label_set_text(g_status_ui.last_action, g_status_ui.last_action_text);
        lv_obj_invalidate(g_status_ui.last_action);
    }
}

static void status_refresh(void)
{
    bt_audio_sink_health_t bt;
    ble_ios_services_snapshot_t ios;
    watch_settings_snapshot_t settings;
    badge_transfer_snapshot_t badge;
    char text[sizeof(g_status_ui.content_text)];

    bt_audio_sink_get_health(&bt);
    ble_ios_services_get_snapshot(&ios);
    watch_settings_get_snapshot(&settings);
    badge_transfer_get_snapshot(&badge);

    rt_snprintf(text, sizeof(text),
                "BT audio\n"
                "state %d  connected %d\n"
                "streaming %d  err %d\n"
                "disc %lu  recover %lu\n\n"
                "BLE / iOS\n"
                "ANCS %lu  AMS %lu\n"
                "player %s\n"
                "track %s\n\n"
                "Audio\n"
                "local vol %u\n"
                "bt vol %u\n"
                "route %d\n\n"
                "Badge\n"
                "state %d  image %d\n"
                "rx %lu / %lu\n"
                "gen %lu  err %d",
                bt.state,
                bt_audio_sink_is_connected(),
                bt_audio_sink_is_streaming(),
                bt.last_error,
                (unsigned long)bt.disconnect_count,
                (unsigned long)bt.recovery_count,
                (unsigned long)ios.ancs_count,
                (unsigned long)ios.ams_count,
                status_text_or_dash(ios.player),
                status_text_or_dash(ios.track),
                settings.local_volume,
                settings.bt_volume,
                settings.route,
                badge.state,
                badge.image_available,
                (unsigned long)badge.received,
                (unsigned long)badge.total,
                (unsigned long)badge.generation,
                badge.last_error);

    if (rt_strncmp(g_status_ui.content_text, text, sizeof(g_status_ui.content_text)) != 0)
    {
        rt_snprintf(g_status_ui.content_text, sizeof(g_status_ui.content_text), "%s", text);
        lv_obj_invalidate(g_status_ui.content);
        lv_label_set_text(g_status_ui.content, g_status_ui.content_text);
        lv_obj_invalidate(g_status_ui.content);
    }
}

static void status_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    status_refresh();
}

static void status_button_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    status_action_t action = (status_action_t)(uintptr_t)lv_event_get_user_data(event);

    if (code != LV_EVENT_CLICKED && code != LV_EVENT_SHORT_CLICKED)
    {
        return;
    }

    if (action == STATUS_ACTION_BT_RECOVER)
    {
        int ret = bt_audio_sink_request_recovery();
        char text[48];

        rt_snprintf(text, sizeof(text), "BT recover ret %d", ret);
        status_update_action_text(text);
    }
    else if (action == STATUS_ACTION_BADGE_CLEAR)
    {
        int ret = badge_transfer_clear();
        char text[48];

        rt_snprintf(text, sizeof(text), "Badge clear ret %d", ret);
        status_update_action_text(text);
    }

    status_refresh();
}

static lv_obj_t *status_create_button(lv_obj_t *parent,
                                      const char *text,
                                      lv_coord_t x,
                                      lv_coord_t y,
                                      lv_coord_t w,
                                      status_action_t action)
{
    lv_obj_t *button = lv_obj_create(parent);
    lv_obj_t *label;

    lv_obj_set_size(button, w, 48);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x2f5d8c), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(button, 6, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(button, status_button_event, LV_EVENT_ALL, (void *)(uintptr_t)action);

    label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    return button;
}

static void status_on_start(void)
{
    lv_coord_t button_w = (LV_HOR_RES_MAX - 48) / 2;

    rt_memset(&g_status_ui, 0, sizeof(g_status_ui));
    status_update_action_text("Ready");

    g_status_ui.root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_status_ui.root, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_set_style_bg_color(g_status_ui.root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_status_ui.root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_status_ui.root, 0, 0);
    lv_obj_set_style_pad_all(g_status_ui.root, 16, 0);
    lv_obj_set_scroll_dir(g_status_ui.root, LV_DIR_VER);

    lv_obj_t *title = lv_label_create(g_status_ui.root);
    lv_label_set_text(title, "Status");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_pos(title, 16, 10);

    status_create_button(g_status_ui.root, "BT recover", 16, 58, button_w,
                         STATUS_ACTION_BT_RECOVER);
    status_create_button(g_status_ui.root, "Clear badge", 32 + button_w, 58, button_w,
                         STATUS_ACTION_BADGE_CLEAR);

    g_status_ui.last_action = lv_label_create(g_status_ui.root);
    lv_label_set_text(g_status_ui.last_action, g_status_ui.last_action_text);
    lv_obj_set_size(g_status_ui.last_action, LV_HOR_RES_MAX - 32, 28);
    lv_label_set_long_mode(g_status_ui.last_action, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(g_status_ui.last_action, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_status_ui.last_action, lv_color_hex(0x9bd3ff), 0);
    lv_obj_set_style_bg_color(g_status_ui.last_action, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_status_ui.last_action, LV_OPA_COVER, 0);
    lv_obj_set_pos(g_status_ui.last_action, 16, 114);

    g_status_ui.content = lv_label_create(g_status_ui.root);
    lv_label_set_long_mode(g_status_ui.content, LV_LABEL_LONG_WRAP);
    lv_obj_set_size(g_status_ui.content, LV_HOR_RES_MAX - 32, LV_VER_RES_MAX + 180);
    lv_obj_set_style_text_font(g_status_ui.content, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_status_ui.content, lv_color_white(), 0);
    lv_obj_set_style_bg_color(g_status_ui.content, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_status_ui.content, LV_OPA_COVER, 0);
    lv_obj_set_pos(g_status_ui.content, 16, 148);

    status_refresh();
    g_status_ui.refresh_timer = lv_timer_create(status_timer_cb, STATUS_REFRESH_MS, NULL);
}

static void status_on_stop(void)
{
    if (g_status_ui.refresh_timer)
    {
        lv_timer_del(g_status_ui.refresh_timer);
    }
    if (g_status_ui.root)
    {
        lv_obj_del(g_status_ui.root);
    }
    rt_memset(&g_status_ui, 0, sizeof(g_status_ui));
}

static void status_msg_handler(gui_app_msg_type_t msg, void *param)
{
    (void)param;
    switch (msg)
    {
    case GUI_APP_MSG_ONSTART:
        status_on_start();
        break;
    case GUI_APP_MSG_ONSTOP:
        status_on_stop();
        break;
    default:
        break;
    }
}

static int status_app_main(intent_t intent)
{
    (void)intent;
    gui_app_regist_msg_handler(APP_ID, status_msg_handler);
    return 0;
}

LV_IMG_DECLARE(img_settings);
BUILTIN_APP_EXPORT(LV_EXT_STR_ID(status), LV_EXT_IMG_GET(img_settings), APP_ID, status_app_main);
