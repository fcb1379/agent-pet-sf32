#include <rtthread.h>

#include "littlevgl2rtt.h"
#include "lv_ext_resource_manager.h"
#include "gui_app_fwk.h"

#define APP_ID "pet"

typedef struct
{
    lv_obj_t *root;
    lv_obj_t *stage;
    lv_obj_t *tail;
    lv_obj_t *left_eye;
    lv_obj_t *right_eye;
    lv_obj_t *sparkle_a;
    lv_obj_t *sparkle_b;
    lv_obj_t *sparkle_c;
} pet_ui_t;

static pet_ui_t g_pet_ui;

static lv_obj_t *pet_shape(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                           lv_coord_t width, lv_coord_t height, uint32_t color,
                           lv_coord_t radius)
{
    lv_obj_t *object = lv_obj_create(parent);

    lv_obj_set_pos(object, x, y);
    lv_obj_set_size(object, width, height);
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, radius, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
    return object;
}

static void pet_start_y_animation(lv_obj_t *object, lv_coord_t from, lv_coord_t to,
                                  uint32_t time, uint32_t delay)
{
    lv_anim_t animation;

    lv_anim_init(&animation);
    lv_anim_set_var(&animation, object);
    lv_anim_set_values(&animation, from, to);
    lv_anim_set_exec_cb(&animation, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_set_time(&animation, time);
    lv_anim_set_playback_time(&animation, time);
    lv_anim_set_repeat_delay(&animation, delay);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&animation);
}

static void pet_start_x_animation(lv_obj_t *object, lv_coord_t from, lv_coord_t to,
                                  uint32_t time)
{
    lv_anim_t animation;

    lv_anim_init(&animation);
    lv_anim_set_var(&animation, object);
    lv_anim_set_values(&animation, from, to);
    lv_anim_set_exec_cb(&animation, (lv_anim_exec_xcb_t)lv_obj_set_x);
    lv_anim_set_time(&animation, time);
    lv_anim_set_playback_time(&animation, time);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&animation);
}

static void pet_start_blink_animation(lv_obj_t *object)
{
    lv_anim_t animation;

    lv_anim_init(&animation);
    lv_anim_set_var(&animation, object);
    lv_anim_set_values(&animation, 18, 3);
    lv_anim_set_exec_cb(&animation, (lv_anim_exec_xcb_t)lv_obj_set_height);
    lv_anim_set_time(&animation, 90);
    lv_anim_set_playback_time(&animation, 90);
    lv_anim_set_repeat_delay(&animation, 2200);
    lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&animation);
}

static void pet_on_start(void)
{
    lv_obj_t *name;
    lv_obj_t *floor;
    lv_obj_t *head;
    lv_obj_t *body;
    lv_obj_t *ear;

    rt_memset(&g_pet_ui, 0, sizeof(g_pet_ui));
    g_pet_ui.root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_pet_ui.root, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_set_style_bg_color(g_pet_ui.root, lv_color_hex(0x10232b), 0);
    lv_obj_set_style_bg_opa(g_pet_ui.root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_pet_ui.root, 0, 0);
    lv_obj_set_style_pad_all(g_pet_ui.root, 0, 0);
    lv_obj_clear_flag(g_pet_ui.root, LV_OBJ_FLAG_SCROLLABLE);

    name = lv_label_create(g_pet_ui.root);
    lv_label_set_text(name, "Momo");
    lv_obj_set_pos(name, 12, 10);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(0xd8f7ee), 0);

    floor = pet_shape(g_pet_ui.root, 48, 185, 144, 18, 0x183942, 18);
    lv_obj_set_style_bg_opa(floor, LV_OPA_80, 0);

    g_pet_ui.stage = lv_obj_create(g_pet_ui.root);
    lv_obj_set_pos(g_pet_ui.stage, 62, 48);
    lv_obj_set_size(g_pet_ui.stage, 116, 132);
    lv_obj_set_style_bg_opa(g_pet_ui.stage, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_pet_ui.stage, 0, 0);
    lv_obj_set_style_pad_all(g_pet_ui.stage, 0, 0);
    lv_obj_clear_flag(g_pet_ui.stage, LV_OBJ_FLAG_SCROLLABLE);

    g_pet_ui.tail = pet_shape(g_pet_ui.stage, 91, 70, 16, 43, 0x58d5c3, 10);
    lv_obj_set_style_transform_angle(g_pet_ui.tail, -180, 0);
    lv_obj_set_style_transform_pivot_x(g_pet_ui.tail, 8, 0);
    lv_obj_set_style_transform_pivot_y(g_pet_ui.tail, 38, 0);

    body = pet_shape(g_pet_ui.stage, 24, 67, 70, 54, 0x58d5c3, 28);
    pet_shape(body, 17, 20, 36, 25, 0xd8f7ee, 14);
    pet_shape(body, 6, 43, 22, 12, 0xf6c75e, 8);
    pet_shape(body, 43, 43, 22, 12, 0xf6c75e, 8);

    ear = pet_shape(g_pet_ui.stage, 24, 16, 24, 35, 0x58d5c3, 12);
    lv_obj_set_style_transform_angle(ear, -140, 0);
    lv_obj_set_style_transform_pivot_x(ear, 12, 0);
    lv_obj_set_style_transform_pivot_y(ear, 28, 0);
    pet_shape(ear, 7, 9, 10, 18, 0xff8aae, 6);
    ear = pet_shape(g_pet_ui.stage, 70, 16, 24, 35, 0x58d5c3, 12);
    lv_obj_set_style_transform_angle(ear, 140, 0);
    lv_obj_set_style_transform_pivot_x(ear, 12, 0);
    lv_obj_set_style_transform_pivot_y(ear, 28, 0);
    pet_shape(ear, 7, 9, 10, 18, 0xff8aae, 6);

    head = pet_shape(g_pet_ui.stage, 17, 31, 82, 64, 0x58d5c3, 30);
    g_pet_ui.left_eye = pet_shape(head, 20, 25, 12, 18, 0x183942, 8);
    g_pet_ui.right_eye = pet_shape(head, 50, 25, 12, 18, 0x183942, 8);
    pet_shape(head, 37, 43, 8, 6, 0xff8aae, 5);
    pet_shape(head, 9, 49, 16, 7, 0xffb4c8, 5);
    pet_shape(head, 57, 49, 16, 7, 0xffb4c8, 5);

    g_pet_ui.sparkle_a = pet_shape(g_pet_ui.root, 35, 84, 7, 7, 0xf6c75e, 7);
    g_pet_ui.sparkle_b = pet_shape(g_pet_ui.root, 196, 103, 6, 6, 0xff8aae, 6);
    g_pet_ui.sparkle_c = pet_shape(g_pet_ui.root, 184, 58, 5, 5, 0x7cc8ff, 5);

    pet_start_y_animation(g_pet_ui.stage, 48, 43, 800, 120);
    pet_start_x_animation(g_pet_ui.tail, 89, 96, 560);
    pet_start_blink_animation(g_pet_ui.left_eye);
    pet_start_blink_animation(g_pet_ui.right_eye);
    pet_start_y_animation(g_pet_ui.sparkle_a, 84, 72, 1000, 250);
    pet_start_y_animation(g_pet_ui.sparkle_b, 103, 89, 1150, 80);
    pet_start_y_animation(g_pet_ui.sparkle_c, 58, 45, 900, 360);
}

static void pet_on_stop(void)
{
    if (g_pet_ui.root)
    {
        lv_obj_del(g_pet_ui.root);
    }
    rt_memset(&g_pet_ui, 0, sizeof(g_pet_ui));
}

static void pet_msg_handler(gui_app_msg_type_t msg, void *param)
{
    (void)param;
    if (msg == GUI_APP_MSG_ONSTART)
    {
        pet_on_start();
    }
    else if (msg == GUI_APP_MSG_ONSTOP)
    {
        pet_on_stop();
    }
}

static int pet_app_main(intent_t intent)
{
    (void)intent;
    gui_app_regist_msg_handler(APP_ID, pet_msg_handler);
    return 0;
}

LV_IMG_DECLARE(img_pet);
BUILTIN_APP_EXPORT(LV_EXT_STR_ID(pet), LV_EXT_IMG_GET(img_pet), APP_ID, pet_app_main);
