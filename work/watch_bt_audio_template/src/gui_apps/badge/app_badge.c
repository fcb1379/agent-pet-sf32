#include "littlevgl2rtt.h"
#include "lv_ext_resource_manager.h"
#include "gui_app_fwk.h"
#include "badge_transfer.h"

#define APP_ID "badge"
#define BADGE_LVGL_IMAGE_PATH "/:/badge.jpg"

typedef struct
{
    lv_obj_t *root;
    lv_obj_t *image;
    lv_obj_t *message;
    lv_timer_t *refresh_timer;
    uint32_t generation;
    uint32_t received;
    uint32_t total;
    int16_t last_error;
    badge_transfer_state_t state;
    uint8_t image_available;
} badge_ui_t;

static badge_ui_t g_badge_ui;

static const char *badge_ui_message(const badge_transfer_snapshot_t *snapshot,
                                    char *buffer,
                                    uint32_t buffer_len)
{
    if (snapshot->state == BADGE_TRANSFER_RECEIVING)
    {
        uint32_t percent = snapshot->total ? snapshot->received * 100 / snapshot->total : 0;

        rt_snprintf(buffer, buffer_len, "Receiving %lu%%\n%lu / %lu",
                    (unsigned long)percent,
                    (unsigned long)snapshot->received,
                    (unsigned long)snapshot->total);
        return buffer;
    }

    if (snapshot->state == BADGE_TRANSFER_ERROR)
    {
        rt_snprintf(buffer, buffer_len, "Transfer failed\nerr %d", snapshot->last_error);
        return buffer;
    }

    return "BLE image pending";
}

static void badge_ui_refresh(void)
{
    badge_transfer_snapshot_t snapshot;
    lv_img_header_t header;
    uint16_t zoom = LV_IMG_ZOOM_NONE;
    char message[64];

    badge_transfer_get_snapshot(&snapshot);
    if (snapshot.image_available && snapshot.state != BADGE_TRANSFER_RECEIVING &&
            snapshot.state != BADGE_TRANSFER_ERROR)
    {
        lv_img_cache_invalidate_src(BADGE_LVGL_IMAGE_PATH);
        lv_img_set_src(g_badge_ui.image, BADGE_LVGL_IMAGE_PATH);
        if (lv_img_decoder_get_info(BADGE_LVGL_IMAGE_PATH, &header) == LV_RES_OK &&
                header.w && header.h)
        {
            uint32_t zoom_w = (uint32_t)LV_HOR_RES_MAX * LV_IMG_ZOOM_NONE / header.w;
            uint32_t zoom_h = (uint32_t)LV_VER_RES_MAX * LV_IMG_ZOOM_NONE / header.h;
            zoom = (uint16_t)LV_MIN(LV_IMG_ZOOM_NONE, LV_MIN(zoom_w, zoom_h));
        }
        lv_img_set_zoom(g_badge_ui.image, zoom);
        lv_obj_center(g_badge_ui.image);
        lv_obj_clear_flag(g_badge_ui.image, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g_badge_ui.message, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_label_set_text(g_badge_ui.message,
                          badge_ui_message(&snapshot, message, sizeof(message)));
        lv_obj_center(g_badge_ui.message);
        lv_obj_add_flag(g_badge_ui.image, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(g_badge_ui.message, LV_OBJ_FLAG_HIDDEN);
    }

    g_badge_ui.generation = snapshot.generation;
    g_badge_ui.received = snapshot.received;
    g_badge_ui.total = snapshot.total;
    g_badge_ui.last_error = snapshot.last_error;
    g_badge_ui.state = snapshot.state;
    g_badge_ui.image_available = snapshot.image_available;
}

static void badge_refresh_timer_cb(lv_timer_t *timer)
{
    badge_transfer_snapshot_t snapshot;

    (void)timer;
    badge_transfer_get_snapshot(&snapshot);
    if (snapshot.generation != g_badge_ui.generation ||
            snapshot.received != g_badge_ui.received ||
            snapshot.total != g_badge_ui.total ||
            snapshot.last_error != g_badge_ui.last_error ||
            snapshot.state != g_badge_ui.state ||
            snapshot.image_available != g_badge_ui.image_available)
    {
        badge_ui_refresh();
    }
}

static void badge_on_start(void)
{
    rt_memset(&g_badge_ui, 0, sizeof(g_badge_ui));

    g_badge_ui.root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_badge_ui.root, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_set_style_bg_color(g_badge_ui.root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_badge_ui.root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_badge_ui.root, 0, 0);
    lv_obj_set_style_pad_all(g_badge_ui.root, 0, 0);
    lv_obj_clear_flag(g_badge_ui.root, LV_OBJ_FLAG_SCROLLABLE);

    g_badge_ui.image = lv_img_create(g_badge_ui.root);
    lv_obj_add_flag(g_badge_ui.image, LV_OBJ_FLAG_HIDDEN);

    g_badge_ui.message = lv_label_create(g_badge_ui.root);
    lv_label_set_text(g_badge_ui.message, "BLE image pending");
    lv_obj_set_width(g_badge_ui.message, LV_HOR_RES_MAX - 32);
    lv_label_set_long_mode(g_badge_ui.message, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(g_badge_ui.message, lv_color_white(), 0);
    lv_obj_set_style_text_align(g_badge_ui.message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(g_badge_ui.message, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(g_badge_ui.message, LV_OPA_COVER, 0);
    lv_obj_center(g_badge_ui.message);

    badge_ui_refresh();
    g_badge_ui.refresh_timer = lv_timer_create(badge_refresh_timer_cb, 500, NULL);
}

static void badge_on_stop(void)
{
    if (g_badge_ui.refresh_timer)
    {
        lv_timer_del(g_badge_ui.refresh_timer);
    }
    if (g_badge_ui.root)
    {
        lv_obj_del(g_badge_ui.root);
    }
    rt_memset(&g_badge_ui, 0, sizeof(g_badge_ui));
}

static void badge_msg_handler(gui_app_msg_type_t msg, void *param)
{
    (void)param;
    switch (msg)
    {
    case GUI_APP_MSG_ONSTART:
        badge_on_start();
        break;
    case GUI_APP_MSG_ONSTOP:
        badge_on_stop();
        break;
    default:
        break;
    }
}

static int badge_app_main(intent_t intent)
{
    (void)intent;
    gui_app_regist_msg_handler(APP_ID, badge_msg_handler);
    return 0;
}

LV_IMG_DECLARE(img_photo_album);
BUILTIN_APP_EXPORT(
    LV_EXT_STR_ID(badge),
    LV_EXT_IMG_GET(img_photo_album),
    APP_ID,
    badge_app_main);
