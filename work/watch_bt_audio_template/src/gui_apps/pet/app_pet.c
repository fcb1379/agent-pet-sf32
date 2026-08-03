#include <rtthread.h>

#include "littlevgl2rtt.h"
#include "agent_pet_ble_service.h"
#ifndef BSP_USING_PC_SIMULATOR
    #include "lv_ext_resource_manager.h"
    #include "gui_app_fwk.h"
#else
    LV_IMG_DECLARE(mascot_small);
#endif

#define APP_ID "pet"
#define PET_STATUS_REFRESH_MS (250U)

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
    lv_obj_t *status_label;
    lv_obj_t *task_label;
    lv_timer_t *status_timer;
    uint32_t ulRenderedGeneration;
    bool bRenderedConnected;
} pet_ui_t;

static pet_ui_t g_pet_ui;

static const char *PET_StateName(uint8_t ucState)
{
    static const char *l_aStateNames[] =
    {
        "Idle",
        "Running",
        "Needs input",
        "Completed",
        "Error"
    };

    if (AGENTPET_STATE_ERROR < ucState)
    {
        return "Unknown";
    }

    return l_aStateNames[ucState];
}

static lv_color_t PET_StateColor(uint8_t ucState)
{
    static const uint32_t l_aStateColors[] =
    {
        0xA7B0B5U,
        0x7CC8FFU,
        0xF6C75EU,
        0x65D69EU,
        0xFF6B7AU
    };

    if (AGENTPET_STATE_ERROR < ucState)
    {
        ucState = AGENTPET_STATE_IDLE;
    }

    return lv_color_hex(l_aStateColors[ucState]);
}

static const AGENTPET_SESSION *PET_SelectSession(
    const AGENTPET_SNAPSHOT *pSnapshot)
{
    uint8_t ucIndex;

    if ((NULL == pSnapshot) || (0U == pSnapshot->ucSessionCount))
    {
        return NULL;
    }

    for (ucIndex = 0U; ucIndex < pSnapshot->ucSessionCount; ucIndex++)
    {
        if (0U != (pSnapshot->aSessions[ucIndex].ucFlags & AGENTPET_TASK_FLAG_ACTIVE))
        {
            return &pSnapshot->aSessions[ucIndex];
        }
    }

    return &pSnapshot->aSessions[0];
}

static const char *PET_ProviderName(uint8_t ucProvider)
{
    static const char *l_aProviderNames[] = {"Agent", "Codex", "Claude"};

    if (2U < ucProvider)
    {
        return l_aProviderNames[0];
    }

    return l_aProviderNames[ucProvider];
}

/*
 * PET_RefreshStatus
 * 功能：在 LVGL 线程中读取已发布快照并刷新桌宠状态文字。
 * 参数：
 *   - pTimer: LVGL 周期定时器。
 * 返回值：无。
 */
static void PET_RefreshStatus(lv_timer_t *pTimer)
{
    AGENTPET_BLE_STATUS tStatus;
    const AGENTPET_SESSION *pSession;

    (void)pTimer;
    if (!AGENTPETBLE_GetStatus(&tStatus))
    {
        return;
    }
    if (
        (g_pet_ui.ulRenderedGeneration == tStatus.ulGeneration) &&
        (g_pet_ui.bRenderedConnected == tStatus.bConnected)
    )
    {
        return;
    }

    g_pet_ui.ulRenderedGeneration = tStatus.ulGeneration;
    g_pet_ui.bRenderedConnected = tStatus.bConnected;
    if (!tStatus.bHasSnapshot)
    {
        lv_label_set_text(
            g_pet_ui.status_label,
            tStatus.bConnected ? "BLE connected - waiting" : "BLE disconnected");
        lv_label_set_text(g_pet_ui.task_label, "No Agent snapshot");
        return;
    }

    lv_label_set_text_fmt(
        g_pet_ui.status_label,
        "%s%s  %u tasks",
        tStatus.bConnected ? "" : "Offline - ",
        PET_StateName(tStatus.tSnapshot.ucAggregateState),
        tStatus.tSnapshot.ucSessionCount);
    lv_obj_set_style_text_color(
        g_pet_ui.status_label,
        PET_StateColor(tStatus.tSnapshot.ucAggregateState),
        0);

    pSession = PET_SelectSession(&tStatus.tSnapshot);
    if (NULL == pSession)
    {
        lv_label_set_text(g_pet_ui.task_label, "Agent is ready");
    }
    else
    {
        lv_label_set_text_fmt(
            g_pet_ui.task_label,
            "%s #%04lX  %s%s",
            PET_ProviderName(pSession->ucProvider),
            (unsigned long)(pSession->ulTaskHash & 0xFFFFUL),
            PET_StateName(pSession->ucState),
            (0U != (pSession->ucFlags & AGENTPET_TASK_FLAG_APPROVAL)) ?
                " !" : "");
    }

    return;
}

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
#ifndef BSP_USING_PC_SIMULATOR
    lv_obj_t *head;
    lv_obj_t *body;
    lv_obj_t *ear;
#endif

    rt_memset(&g_pet_ui, 0, sizeof(g_pet_ui));
    g_pet_ui.root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_pet_ui.root, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_set_style_bg_color(g_pet_ui.root, lv_color_hex(0x10232b), 0);
    lv_obj_set_style_bg_opa(g_pet_ui.root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_pet_ui.root, 0, 0);
    lv_obj_set_style_pad_all(g_pet_ui.root, 0, 0);
    lv_obj_clear_flag(g_pet_ui.root, LV_OBJ_FLAG_SCROLLABLE);

    name = lv_label_create(g_pet_ui.root);
    lv_label_set_text(name, "Agent Pet");
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

#ifdef BSP_USING_PC_SIMULATOR
    {
        lv_obj_t *mascot = lv_img_create(g_pet_ui.stage);
        lv_img_set_src(mascot, &mascot_small);
        lv_obj_set_pos(mascot, 8, 8);
    }
#else
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
#endif

    g_pet_ui.sparkle_a = pet_shape(g_pet_ui.root, 35, 84, 7, 7, 0xf6c75e, 7);
    g_pet_ui.sparkle_b = pet_shape(g_pet_ui.root, 196, 103, 6, 6, 0xff8aae, 6);
    g_pet_ui.sparkle_c = pet_shape(g_pet_ui.root, 184, 58, 5, 5, 0x7cc8ff, 5);

    pet_start_y_animation(g_pet_ui.stage, 48, 43, 800, 120);
    pet_start_x_animation(g_pet_ui.tail, 89, 96, 560);
#ifndef BSP_USING_PC_SIMULATOR
    pet_start_blink_animation(g_pet_ui.left_eye);
    pet_start_blink_animation(g_pet_ui.right_eye);
#endif
    pet_start_y_animation(g_pet_ui.sparkle_a, 84, 72, 1000, 250);
    pet_start_y_animation(g_pet_ui.sparkle_b, 103, 89, 1150, 80);
    pet_start_y_animation(g_pet_ui.sparkle_c, 58, 45, 900, 360);

    g_pet_ui.status_label = lv_label_create(g_pet_ui.root);
    lv_obj_set_width(g_pet_ui.status_label, 220);
    lv_obj_set_pos(g_pet_ui.status_label, 10, 204);
    lv_obj_set_style_text_align(g_pet_ui.status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(g_pet_ui.status_label, lv_color_hex(0xA7B0B5), 0);

    g_pet_ui.task_label = lv_label_create(g_pet_ui.root);
    lv_obj_set_width(g_pet_ui.task_label, 220);
    lv_obj_set_pos(g_pet_ui.task_label, 10, 222);
    lv_obj_set_style_text_align(g_pet_ui.task_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(g_pet_ui.task_label, lv_color_hex(0xD8F7EE), 0);

    g_pet_ui.ulRenderedGeneration = 0xFFFFFFFFUL;
    g_pet_ui.bRenderedConnected = false;
    g_pet_ui.status_timer = lv_timer_create(
        PET_RefreshStatus,
        PET_STATUS_REFRESH_MS,
        NULL);
    PET_RefreshStatus(g_pet_ui.status_timer);
}

static void pet_on_stop(void)
{
    if (g_pet_ui.status_timer)
    {
        lv_timer_del(g_pet_ui.status_timer);
        g_pet_ui.status_timer = NULL;
    }
    if (g_pet_ui.root)
    {
        lv_obj_del(g_pet_ui.root);
    }
    rt_memset(&g_pet_ui, 0, sizeof(g_pet_ui));
}

#ifdef BSP_USING_PC_SIMULATOR
/* PC simulator entry: drive the pet UI directly without the GUI app framework.
 * Guarded so the hardware build path is completely unchanged. */
void pet_simulator_run(void)
{
    pet_on_start();
}

void pet_simulator_stop(void)
{
    pet_on_stop();
}
#else
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
#endif /* BSP_USING_PC_SIMULATOR */
