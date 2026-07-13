#include <rtthread.h>
#include <stdint.h>

#include "littlevgl2rtt.h"
#include "lv_ext_resource_manager.h"
#include "gui_app_fwk.h"
#include "bf0_ble_ams.h"
#include "ble_ios_services.h"
#include "local_music_player.h"

#define APP_ID "music"
#define MUSIC_REFRESH_MS 1000

typedef enum
{
    MUSIC_ACTION_LOCAL_PLAY = 1,
    MUSIC_ACTION_LOCAL_PAUSE,
    MUSIC_ACTION_LOCAL_RESUME,
    MUSIC_ACTION_LOCAL_STOP,
    MUSIC_ACTION_AMS_PREV,
    MUSIC_ACTION_AMS_TOGGLE,
    MUSIC_ACTION_AMS_NEXT,
    MUSIC_ACTION_AMS_VOL_DOWN,
    MUSIC_ACTION_AMS_VOL_UP,
} music_action_t;

typedef struct
{
    lv_obj_t *root;
    lv_obj_t *status;
    lv_obj_t *last_action;
    lv_timer_t *refresh_timer;
    char last_action_text[48];
    char status_text[384];
} music_ui_t;

static music_ui_t g_music_ui;

static const char *music_text_or_dash(const char *text)
{
    return (text && text[0]) ? text : "-";
}

static void music_update_action_text(const char *text)
{
    rt_snprintf(g_music_ui.last_action_text, sizeof(g_music_ui.last_action_text), "%s", text);
    if (g_music_ui.last_action)
    {
        lv_label_set_text(g_music_ui.last_action, g_music_ui.last_action_text);
    }
}

static void music_refresh(void)
{
    ble_ios_services_snapshot_t ios;
    char text[sizeof(g_music_ui.status_text)];

    ble_ios_services_get_snapshot(&ios);
    rt_snprintf(text, sizeof(text),
                "iOS media\n"
                "player %s\n"
                "state %s\n"
                "track %s\n"
                "artist %s\n"
                "album %s\n\n"
                "Local file\n"
                "/16k.wav",
                music_text_or_dash(ios.player),
                music_text_or_dash(ios.playback),
                music_text_or_dash(ios.track),
                music_text_or_dash(ios.artist),
                music_text_or_dash(ios.album));
    if (rt_strncmp(g_music_ui.status_text, text, sizeof(g_music_ui.status_text)) != 0)
    {
        rt_snprintf(g_music_ui.status_text, sizeof(g_music_ui.status_text), "%s", text);
        lv_label_set_text(g_music_ui.status, g_music_ui.status_text);
    }
}

static int music_run_action(music_action_t action)
{
    switch (action)
    {
    case MUSIC_ACTION_LOCAL_PLAY:
        return local_music_play_file(NULL, 0);
    case MUSIC_ACTION_LOCAL_PAUSE:
        return local_music_pause();
    case MUSIC_ACTION_LOCAL_RESUME:
        return local_music_resume();
    case MUSIC_ACTION_LOCAL_STOP:
        return local_music_stop();
    case MUSIC_ACTION_AMS_PREV:
        return ble_ios_services_send_ams_cmd(BLE_AMS_CMD_PREV);
    case MUSIC_ACTION_AMS_TOGGLE:
        return ble_ios_services_send_ams_cmd(BLE_AMS_CMD_TOGGLE_PLAY_PAUSE);
    case MUSIC_ACTION_AMS_NEXT:
        return ble_ios_services_send_ams_cmd(BLE_AMS_CMD_NEXT);
    case MUSIC_ACTION_AMS_VOL_DOWN:
        return ble_ios_services_send_ams_cmd(BLE_AMS_CMD_VOL_DOWN);
    case MUSIC_ACTION_AMS_VOL_UP:
        return ble_ios_services_send_ams_cmd(BLE_AMS_CMD_VOL_UP);
    default:
        return -RT_ERROR;
    }
}

static const char *music_action_name(music_action_t action)
{
    switch (action)
    {
    case MUSIC_ACTION_LOCAL_PLAY:
        return "Local play";
    case MUSIC_ACTION_LOCAL_PAUSE:
        return "Local pause";
    case MUSIC_ACTION_LOCAL_RESUME:
        return "Local resume";
    case MUSIC_ACTION_LOCAL_STOP:
        return "Local stop";
    case MUSIC_ACTION_AMS_PREV:
        return "AMS prev";
    case MUSIC_ACTION_AMS_TOGGLE:
        return "AMS toggle";
    case MUSIC_ACTION_AMS_NEXT:
        return "AMS next";
    case MUSIC_ACTION_AMS_VOL_DOWN:
        return "AMS vol-";
    case MUSIC_ACTION_AMS_VOL_UP:
        return "AMS vol+";
    default:
        return "Unknown";
    }
}

static void music_button_event(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    music_action_t action = (music_action_t)(uintptr_t)lv_event_get_user_data(event);
    int ret;
    char text[48];

    if (code != LV_EVENT_CLICKED && code != LV_EVENT_SHORT_CLICKED)
    {
        return;
    }

    ret = music_run_action(action);
    rt_snprintf(text, sizeof(text), "%s ret %d", music_action_name(action), ret);
    music_update_action_text(text);
    music_refresh();
}

static lv_obj_t *music_create_button(lv_obj_t *parent,
                                     const char *text,
                                     lv_coord_t x,
                                     lv_coord_t y,
                                     lv_coord_t w,
                                     music_action_t action)
{
    lv_obj_t *button = lv_obj_create(parent);
    lv_obj_t *label;

    lv_obj_set_size(button, w, 44);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x34523d), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(button, 6, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_clear_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(button, music_button_event, LV_EVENT_ALL, (void *)(uintptr_t)action);

    label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_center(label);
    return button;
}

static void music_create_button_row(lv_obj_t *parent,
                                    lv_coord_t y,
                                    const char *left_text,
                                    music_action_t left_action,
                                    const char *right_text,
                                    music_action_t right_action)
{
    lv_coord_t button_w = (LV_HOR_RES_MAX - 48) / 2;

    music_create_button(parent, left_text, 16, y, button_w, left_action);
    music_create_button(parent, right_text, 32 + button_w, y, button_w, right_action);
}

static void music_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    music_refresh();
}

static void music_on_start(void)
{
    rt_memset(&g_music_ui, 0, sizeof(g_music_ui));
    music_update_action_text("Ready");

    g_music_ui.root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_music_ui.root, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_set_style_bg_color(g_music_ui.root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_music_ui.root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_music_ui.root, 0, 0);
    lv_obj_set_style_pad_all(g_music_ui.root, 16, 0);
    lv_obj_set_scroll_dir(g_music_ui.root, LV_DIR_VER);

    lv_obj_t *title = lv_label_create(g_music_ui.root);
    lv_label_set_text(title, "Music");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_pos(title, 16, 10);

    music_create_button_row(g_music_ui.root, 58, "Local play", MUSIC_ACTION_LOCAL_PLAY,
                            "Local stop", MUSIC_ACTION_LOCAL_STOP);
    music_create_button_row(g_music_ui.root, 110, "Pause", MUSIC_ACTION_LOCAL_PAUSE,
                            "Resume", MUSIC_ACTION_LOCAL_RESUME);
    music_create_button_row(g_music_ui.root, 162, "Prev", MUSIC_ACTION_AMS_PREV,
                            "Next", MUSIC_ACTION_AMS_NEXT);
    music_create_button_row(g_music_ui.root, 214, "Play/Pause", MUSIC_ACTION_AMS_TOGGLE,
                            "Vol +", MUSIC_ACTION_AMS_VOL_UP);
    music_create_button(g_music_ui.root, "Vol -", 16, 266, (LV_HOR_RES_MAX - 48) / 2,
                        MUSIC_ACTION_AMS_VOL_DOWN);

    g_music_ui.last_action = lv_label_create(g_music_ui.root);
    lv_label_set_text(g_music_ui.last_action, g_music_ui.last_action_text);
    lv_obj_set_size(g_music_ui.last_action, LV_HOR_RES_MAX - 32, 28);
    lv_label_set_long_mode(g_music_ui.last_action, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(g_music_ui.last_action, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_music_ui.last_action, lv_color_hex(0xb8e8c2), 0);
    lv_obj_set_style_bg_color(g_music_ui.last_action, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_music_ui.last_action, LV_OPA_COVER, 0);
    lv_obj_set_pos(g_music_ui.last_action, 16, 320);

    g_music_ui.status = lv_label_create(g_music_ui.root);
    lv_label_set_long_mode(g_music_ui.status, LV_LABEL_LONG_WRAP);
    lv_obj_set_size(g_music_ui.status, LV_HOR_RES_MAX - 32, LV_VER_RES_MAX);
    lv_obj_set_style_text_font(g_music_ui.status, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(g_music_ui.status, lv_color_white(), 0);
    lv_obj_set_style_bg_color(g_music_ui.status, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_music_ui.status, LV_OPA_COVER, 0);
    lv_obj_set_pos(g_music_ui.status, 16, 356);

    music_refresh();
    g_music_ui.refresh_timer = lv_timer_create(music_timer_cb, MUSIC_REFRESH_MS, NULL);
}

static void music_on_stop(void)
{
    if (g_music_ui.refresh_timer)
    {
        lv_timer_del(g_music_ui.refresh_timer);
    }
    if (g_music_ui.root)
    {
        lv_obj_del(g_music_ui.root);
    }
    rt_memset(&g_music_ui, 0, sizeof(g_music_ui));
}

static void music_msg_handler(gui_app_msg_type_t msg, void *param)
{
    (void)param;
    switch (msg)
    {
    case GUI_APP_MSG_ONSTART:
        music_on_start();
        break;
    case GUI_APP_MSG_ONSTOP:
        music_on_stop();
        break;
    default:
        break;
    }
}

static int music_app_main(intent_t intent)
{
    (void)intent;
    gui_app_regist_msg_handler(APP_ID, music_msg_handler);
    return 0;
}

LV_IMG_DECLARE(img_itunes);
BUILTIN_APP_EXPORT(LV_EXT_STR_ID(music), LV_EXT_IMG_GET(img_itunes), APP_ID, music_app_main);
