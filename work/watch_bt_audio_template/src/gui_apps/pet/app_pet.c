#include <rtthread.h>

#include "littlevgl2rtt.h"
#include "agent_pet_ble_service.h"
#if !defined(BSP_USING_PC_SIMULATOR) || !defined(AGENT_PET_STANDALONE_PREVIEW)
    #include "lv_ext_resource_manager.h"
    #include "gui_app_fwk.h"
#endif
#ifndef BSP_USING_PC_SIMULATOR
    #include <time.h>
    #include "share_prefs.h"
#endif

LV_IMG_DECLARE(agent_pet_mascot);

#define APP_ID "pet"
#define PET_STATUS_REFRESH_MS (250U)
#define PET_MASCOT_SIZE (336)
#define PET_MASCOT_X ((LV_HOR_RES_MAX - PET_MASCOT_SIZE) / 2)
#define PET_MASCOT_Y (((LV_VER_RES_MAX - PET_MASCOT_SIZE) / 2) - 12)
#define PET_WOODEN_FISH_IDLE_MS (950U)
#define PET_DAILY_SUMMARY_MS (1900U)
#define PET_TURBO_INTERVAL_MS (180U)
#define PET_FAST_INTERVAL_MS (360U)
#define PET_QUICK_INTERVAL_MS (700U)
#define PET_WOODEN_FISH_WIDTH (210)
#define PET_WOODEN_FISH_HEIGHT (180)
#ifndef BSP_USING_PC_SIMULATOR
    #define PET_PREF_NAME "agent_pet_daily_merit_pref_v1__"
    #define PET_PREF_DAY_KEY "merit_day"
    #define PET_PREF_COUNT_KEY "merit_count"
#endif

typedef struct
{
    lv_obj_t *root;
    lv_obj_t *stage;
    lv_obj_t *mascot;
    lv_obj_t *wooden_fish;
    lv_obj_t *fish_body;
    lv_obj_t *mallet;
    lv_obj_t *merit_label;
    lv_obj_t *speed_label;
    lv_obj_t *daily_summary;
    lv_obj_t *sparkle_a;
    lv_obj_t *sparkle_b;
    lv_obj_t *sparkle_c;
    lv_obj_t *status_label;
    lv_obj_t *task_label;
    lv_timer_t *status_timer;
    lv_timer_t *wooden_timer;
    lv_timer_t *daily_timer;
#ifndef BSP_USING_PC_SIMULATOR
    share_prefs_t *pPrefs;
#endif
    uint32_t ulRenderedGeneration;
    uint32_t ulLastHitTick;
    uint32_t ulMeritCount;
    uint32_t ulMeritDay;
    uint8_t ucRenderedState;
    bool bRenderedConnected;
} pet_ui_t;

static pet_ui_t g_pet_ui;

static void PET_ApplyStateAnimation(uint8_t ucState);

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
        PET_ApplyStateAnimation(AGENTPET_STATE_IDLE);
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

    PET_ApplyStateAnimation(tStatus.tSnapshot.ucAggregateState);

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
    lv_obj_clear_flag(object, LV_OBJ_FLAG_CLICKABLE);
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

/*
 * PET_ApplyStateAnimation
 * Function: Match the desktop Agent Pet motion for each aggregate state.
 */
static void PET_ApplyStateAnimation(uint8_t ucState)
{
    lv_anim_t tAnimation;
    lv_anim_exec_xcb_t pExecCallback;
    int32_t lFrom;
    int32_t lTo;
    uint32_t ulTime;
    uint16_t usRepeatCount;

    if ((NULL == g_pet_ui.stage) ||
        (g_pet_ui.ucRenderedState == ucState))
    {
        return;
    }

    g_pet_ui.ucRenderedState = ucState;
    lv_anim_del(g_pet_ui.stage, NULL);
    lv_obj_set_pos(g_pet_ui.stage, PET_MASCOT_X, PET_MASCOT_Y);
    pExecCallback = (lv_anim_exec_xcb_t)lv_obj_set_y;
    lFrom = PET_MASCOT_Y + 2;
    lTo = PET_MASCOT_Y - 2;
    ulTime = 1600U;
    usRepeatCount = LV_ANIM_REPEAT_INFINITE;

    if (AGENTPET_STATE_RUNNING == ucState)
    {
        lFrom = PET_MASCOT_Y + 5;
        lTo = PET_MASCOT_Y - 5;
        ulTime = 290U;
    }
    else if (AGENTPET_STATE_NEEDS_INPUT == ucState)
    {
        pExecCallback = (lv_anim_exec_xcb_t)lv_obj_set_x;
        lFrom = PET_MASCOT_X - 4;
        lTo = PET_MASCOT_X + 4;
        ulTime = 275U;
    }
    else if (AGENTPET_STATE_COMPLETED == ucState)
    {
        lFrom = PET_MASCOT_Y + 4;
        lTo = PET_MASCOT_Y - 14;
        ulTime = 375U;
        usRepeatCount = 2U;
    }
    else if (AGENTPET_STATE_ERROR == ucState)
    {
        pExecCallback = (lv_anim_exec_xcb_t)lv_obj_set_x;
        lFrom = PET_MASCOT_X - 3;
        lTo = PET_MASCOT_X + 4;
        ulTime = 110U;
    }

    lv_anim_init(&tAnimation);
    lv_anim_set_var(&tAnimation, g_pet_ui.stage);
    lv_anim_set_values(&tAnimation, lFrom, lTo);
    lv_anim_set_exec_cb(&tAnimation, pExecCallback);
    lv_anim_set_time(&tAnimation, ulTime);
    lv_anim_set_playback_time(&tAnimation, ulTime);
    lv_anim_set_repeat_delay(&tAnimation, 100U);
    lv_anim_set_repeat_count(&tAnimation, usRepeatCount);
    lv_anim_start(&tAnimation);

    return;
}

/*
 * PET_CurrentDay
 * Function: Build a stable UTC day identifier for daily merit rollover.
 */
static uint32_t PET_CurrentDay(void)
{
#ifndef BSP_USING_PC_SIMULATOR
    time_t tNow;
    struct tm tDate;

    tNow = time(NULL);
    if ((time_t)86400 > tNow)
    {
        return 0U;
    }

    gmtime_r(&tNow, &tDate);
    return ((uint32_t)(tDate.tm_year + 1900) * 1000U) +
           (uint32_t)tDate.tm_yday;
#else
    return 0U;
#endif
}

/*
 * PET_LoadMerit
 * Function: Restore today's merit count from product storage on hardware.
 */
static void PET_LoadMerit(void)
{
    g_pet_ui.ulMeritDay = PET_CurrentDay();
    g_pet_ui.ulMeritCount = 0U;

#ifndef BSP_USING_PC_SIMULATOR
    g_pet_ui.pPrefs = share_prefs_open(PET_PREF_NAME, SHAREPREFS_MODE_PRIVATE);
    if (NULL != g_pet_ui.pPrefs)
    {
        uint32_t ulSavedDay;

        ulSavedDay = (uint32_t)share_prefs_get_int(
            g_pet_ui.pPrefs, PET_PREF_DAY_KEY, 0);
        if ((0U != g_pet_ui.ulMeritDay) &&
            (g_pet_ui.ulMeritDay == ulSavedDay))
        {
            g_pet_ui.ulMeritCount = (uint32_t)share_prefs_get_int(
                g_pet_ui.pPrefs, PET_PREF_COUNT_KEY, 0);
        }
    }
#endif

    return;
}

/*
 * PET_SaveMerit
 * Function: Save once after a click burst to reduce Flash wear.
 */
static void PET_SaveMerit(void)
{
#ifndef BSP_USING_PC_SIMULATOR
    rt_err_t tDayResult;
    rt_err_t tCountResult;

    if (NULL == g_pet_ui.pPrefs)
    {
        return;
    }

    tDayResult = share_prefs_set_int(
        g_pet_ui.pPrefs, PET_PREF_DAY_KEY, (int32_t)g_pet_ui.ulMeritDay);
    tCountResult = share_prefs_set_int(
        g_pet_ui.pPrefs, PET_PREF_COUNT_KEY, (int32_t)g_pet_ui.ulMeritCount);
    if ((RT_EOK != tDayResult) || (RT_EOK != tCountResult))
    {
        rt_kprintf("agent pet: save merit failed %d/%d\n",
                   tDayResult, tCountResult);
    }
#endif

    return;
}

/*
 * PET_HideDailySummary
 * Function: Hide the transient daily merit summary.
 */
static void PET_HideDailySummary(lv_timer_t *pTimer)
{
    if (NULL != pTimer)
    {
        lv_timer_pause(pTimer);
    }
    if (NULL != g_pet_ui.daily_summary)
    {
        lv_obj_add_flag(g_pet_ui.daily_summary, LV_OBJ_FLAG_HIDDEN);
    }

    return;
}

/*
 * PET_SetMalletAngle
 * Function: Animate the wooden-fish mallet using LVGL transform angles.
 */
static void PET_SetMalletAngle(void *pObject, int32_t lAngle)
{
    if (NULL != pObject)
    {
        lv_obj_set_style_transform_angle((lv_obj_t *)pObject, lAngle, 0);
    }

    return;
}

/*
 * PET_EndWoodenFish
 * Function: Restore the mascot after the interaction idle timeout.
 */
static void PET_EndWoodenFish(lv_timer_t *pTimer)
{
    if (NULL != pTimer)
    {
        lv_timer_pause(pTimer);
    }

    if (NULL != g_pet_ui.wooden_fish)
    {
        lv_obj_add_flag(g_pet_ui.wooden_fish, LV_OBJ_FLAG_HIDDEN);
    }
    if (NULL != g_pet_ui.stage)
    {
        lv_obj_clear_flag(g_pet_ui.stage, LV_OBJ_FLAG_HIDDEN);
    }
    if ((NULL != g_pet_ui.daily_summary) &&
        (NULL != g_pet_ui.daily_timer))
    {
        lv_label_set_text_fmt(
            g_pet_ui.daily_summary,
            "Today's merit  %lu",
            (unsigned long)g_pet_ui.ulMeritCount);
        lv_obj_clear_flag(g_pet_ui.daily_summary, LV_OBJ_FLAG_HIDDEN);
        lv_timer_set_period(g_pet_ui.daily_timer, PET_DAILY_SUMMARY_MS);
        lv_timer_reset(g_pet_ui.daily_timer);
        lv_timer_resume(g_pet_ui.daily_timer);
    }

    g_pet_ui.ulLastHitTick = 0U;
    PET_SaveMerit();

    return;
}

/*
 * PET_PlayWoodenFish
 * Function: Replace the mascot with a repeatable wooden-fish interaction.
 */
static void PET_PlayWoodenFish(lv_event_t *pEvent)
{
    lv_indev_t *pInput;
    lv_point_t tPoint;
    lv_anim_t tAnimation;
    lv_coord_t tFishX;
    lv_coord_t tFishY;
    uint32_t ulInterval;
    uint32_t ulAnimationTime;
    uint32_t ulAccentColor;
    const char *pSpeedText;

    if ((NULL == pEvent) ||
        (LV_EVENT_SHORT_CLICKED != lv_event_get_code(pEvent)) ||
        (NULL == g_pet_ui.wooden_timer))
    {
        return;
    }

    pInput = lv_indev_get_act();
    if (NULL != pInput)
    {
        lv_indev_get_point(pInput, &tPoint);
        tFishX = tPoint.x - (PET_WOODEN_FISH_WIDTH / 2);
        tFishY = tPoint.y - (PET_WOODEN_FISH_HEIGHT / 2);
        tFishX = LV_MAX(0, LV_MIN(tFishX,
                                  LV_HOR_RES_MAX - PET_WOODEN_FISH_WIDTH));
        tFishY = LV_MAX(0, LV_MIN(tFishY,
                                  LV_VER_RES_MAX - PET_WOODEN_FISH_HEIGHT));
        lv_obj_set_pos(g_pet_ui.wooden_fish, tFishX, tFishY);
    }

    if (NULL != g_pet_ui.daily_timer)
    {
        lv_timer_pause(g_pet_ui.daily_timer);
    }
    if (NULL != g_pet_ui.daily_summary)
    {
        lv_obj_add_flag(g_pet_ui.daily_summary, LV_OBJ_FLAG_HIDDEN);
    }

    ulInterval = (0U == g_pet_ui.ulLastHitTick) ?
        0xFFFFFFFFUL : lv_tick_elaps(g_pet_ui.ulLastHitTick);
    g_pet_ui.ulLastHitTick = lv_tick_get();
    g_pet_ui.ulMeritCount++;

    if (PET_TURBO_INTERVAL_MS >= ulInterval)
    {
        pSpeedText = "Triple dong!";
        ulAnimationTime = 140U;
        ulAccentColor = 0xFF6B7AU;
    }
    else if (PET_FAST_INTERVAL_MS >= ulInterval)
    {
        pSpeedText = "Merit combo";
        ulAnimationTime = 190U;
        ulAccentColor = 0x83EAFFU;
    }
    else if (PET_QUICK_INTERVAL_MS >= ulInterval)
    {
        pSpeedText = "Quick tap";
        ulAnimationTime = 260U;
        ulAccentColor = 0xFFE994U;
    }
    else
    {
        pSpeedText = "Calm tap";
        ulAnimationTime = 330U;
        ulAccentColor = 0xF6C75EU;
    }

    lv_obj_add_flag(g_pet_ui.stage, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_pet_ui.wooden_fish, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_fmt(
        g_pet_ui.merit_label,
        "Merit +1   #%lu",
        (unsigned long)g_pet_ui.ulMeritCount);
    lv_label_set_text(g_pet_ui.speed_label, pSpeedText);
    lv_obj_set_style_text_color(
        g_pet_ui.speed_label,
        lv_color_hex(ulAccentColor),
        0);

    lv_anim_del(g_pet_ui.mallet, NULL);
    lv_anim_init(&tAnimation);
    lv_anim_set_var(&tAnimation, g_pet_ui.mallet);
    lv_anim_set_values(&tAnimation, -360, 160);
    lv_anim_set_exec_cb(&tAnimation, PET_SetMalletAngle);
    lv_anim_set_time(&tAnimation, ulAnimationTime);
    lv_anim_set_playback_time(&tAnimation, 120U);
    lv_anim_start(&tAnimation);

    lv_anim_del(g_pet_ui.fish_body, NULL);
    lv_anim_init(&tAnimation);
    lv_anim_set_var(&tAnimation, g_pet_ui.fish_body);
    lv_anim_set_values(&tAnimation, 42, 35);
    lv_anim_set_exec_cb(&tAnimation, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_set_time(&tAnimation, ulAnimationTime);
    lv_anim_set_playback_time(&tAnimation, 120U);
    lv_anim_start(&tAnimation);

    lv_timer_set_period(g_pet_ui.wooden_timer, PET_WOODEN_FISH_IDLE_MS);
    lv_timer_reset(g_pet_ui.wooden_timer);
    lv_timer_resume(g_pet_ui.wooden_timer);

    return;
}

/*
 * PET_CreateWoodenFish
 * Function: Build the wooden fish from small LVGL primitives.
 */
static void PET_CreateWoodenFish(void)
{
    lv_obj_t *pMouth;
    lv_obj_t *pMalletHead;

    g_pet_ui.wooden_fish = lv_obj_create(g_pet_ui.root);
    lv_obj_set_pos(g_pet_ui.wooden_fish, 90, 155);
    lv_obj_set_size(g_pet_ui.wooden_fish,
                    PET_WOODEN_FISH_WIDTH, PET_WOODEN_FISH_HEIGHT);
    lv_obj_set_style_bg_opa(g_pet_ui.wooden_fish, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_pet_ui.wooden_fish, 0, 0);
    lv_obj_set_style_pad_all(g_pet_ui.wooden_fish, 0, 0);
    lv_obj_clear_flag(g_pet_ui.wooden_fish, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_pet_ui.wooden_fish, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_pet_ui.wooden_fish, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(
        g_pet_ui.wooden_fish,
        PET_PlayWoodenFish,
        LV_EVENT_SHORT_CLICKED,
        NULL);

    g_pet_ui.fish_body = pet_shape(
        g_pet_ui.wooden_fish, 38, 68, 132, 82, 0xC8651DU, 41);
    lv_obj_set_style_border_width(g_pet_ui.fish_body, 4, 0);
    lv_obj_set_style_border_color(
        g_pet_ui.fish_body, lv_color_hex(0x4F250FU), 0);
    pMouth = pet_shape(
        g_pet_ui.fish_body, 23, 15, 70, 12, 0x3D1B0DU, 6);
    (void)pMouth;
    pet_shape(g_pet_ui.fish_body, 99, 52, 17, 15, 0xFFD877U, 8);

    g_pet_ui.mallet = pet_shape(
        g_pet_ui.wooden_fish, 57, 34, 116, 16, 0xC77A32U, 8);
    lv_obj_set_style_border_width(g_pet_ui.mallet, 2, 0);
    lv_obj_set_style_border_color(
        g_pet_ui.mallet, lv_color_hex(0x633514U), 0);
    lv_obj_set_style_transform_pivot_x(g_pet_ui.mallet, 10, 0);
    lv_obj_set_style_transform_pivot_y(g_pet_ui.mallet, 8, 0);
    pMalletHead = pet_shape(
        g_pet_ui.mallet, -19, -12, 40, 40, 0xD99348U, 20);
    lv_obj_set_style_border_width(pMalletHead, 2, 0);
    lv_obj_set_style_border_color(
        pMalletHead, lv_color_hex(0x633514U), 0);

    g_pet_ui.merit_label = lv_label_create(g_pet_ui.wooden_fish);
    lv_obj_set_width(g_pet_ui.merit_label, PET_WOODEN_FISH_WIDTH);
    lv_obj_set_pos(g_pet_ui.merit_label, 0, 3);
    lv_obj_set_style_text_align(
        g_pet_ui.merit_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(g_pet_ui.merit_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(
        g_pet_ui.merit_label, lv_color_hex(0xFFF3A4U), 0);
    lv_label_set_text(g_pet_ui.merit_label, "Merit +1");

    g_pet_ui.speed_label = lv_label_create(g_pet_ui.wooden_fish);
    lv_obj_set_width(g_pet_ui.speed_label, PET_WOODEN_FISH_WIDTH);
    lv_obj_set_pos(g_pet_ui.speed_label, 0, 158);
    lv_obj_set_style_text_align(
        g_pet_ui.speed_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(
        g_pet_ui.speed_label, lv_color_hex(0xF6C75EU), 0);
    lv_label_set_text(g_pet_ui.speed_label, "Calm tap");

    return;
}

static void pet_on_start(void)
{
    lv_obj_t *name;
    lv_obj_t *floor;

    rt_memset(&g_pet_ui, 0, sizeof(g_pet_ui));
    g_pet_ui.ucRenderedState = 0xFFU;
    g_pet_ui.root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(g_pet_ui.root, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_set_style_bg_color(g_pet_ui.root, lv_color_hex(0x10232b), 0);
    lv_obj_set_style_bg_opa(g_pet_ui.root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(g_pet_ui.root, 0, 0);
    lv_obj_set_style_pad_all(g_pet_ui.root, 0, 0);
    lv_obj_clear_flag(g_pet_ui.root, LV_OBJ_FLAG_SCROLLABLE);

    name = lv_label_create(g_pet_ui.root);
    lv_label_set_text(name, "Agent Pet");
    lv_obj_set_width(name, LV_HOR_RES_MAX);
    lv_obj_set_pos(name, 0, 8);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(0xd8f7ee), 0);

    floor = pet_shape(g_pet_ui.root, 40,
                      PET_MASCOT_Y + PET_MASCOT_SIZE - 18,
                      LV_HOR_RES_MAX - 80, 20, 0x183942, 20);
    lv_obj_set_style_bg_opa(floor, LV_OPA_60, 0);

    g_pet_ui.stage = lv_obj_create(g_pet_ui.root);
    lv_obj_set_pos(g_pet_ui.stage, PET_MASCOT_X, PET_MASCOT_Y);
    lv_obj_set_size(g_pet_ui.stage, PET_MASCOT_SIZE, PET_MASCOT_SIZE);
    lv_obj_set_style_bg_opa(g_pet_ui.stage, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_pet_ui.stage, 0, 0);
    lv_obj_set_style_pad_all(g_pet_ui.stage, 0, 0);
    lv_obj_clear_flag(g_pet_ui.stage, LV_OBJ_FLAG_SCROLLABLE);

    g_pet_ui.mascot = lv_img_create(g_pet_ui.stage);
    lv_img_set_src(g_pet_ui.mascot, &agent_pet_mascot);
    lv_obj_center(g_pet_ui.mascot);
    lv_obj_add_flag(g_pet_ui.mascot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        g_pet_ui.mascot,
        PET_PlayWoodenFish,
        LV_EVENT_SHORT_CLICKED,
        NULL);

    g_pet_ui.sparkle_a = pet_shape(g_pet_ui.root, 30,
                                   PET_MASCOT_Y + 95, 10, 10, 0xf6c75e, 10);
    g_pet_ui.sparkle_b = pet_shape(g_pet_ui.root, LV_HOR_RES_MAX - 42,
                                   PET_MASCOT_Y + 150, 9, 9, 0xff8aae, 9);
    g_pet_ui.sparkle_c = pet_shape(g_pet_ui.root, LV_HOR_RES_MAX - 56,
                                   PET_MASCOT_Y + 65, 8, 8, 0x7cc8ff, 8);

    pet_start_y_animation(g_pet_ui.sparkle_a,
                          PET_MASCOT_Y + 95, PET_MASCOT_Y + 78, 1000, 250);
    pet_start_y_animation(g_pet_ui.sparkle_b,
                          PET_MASCOT_Y + 150, PET_MASCOT_Y + 130, 1150, 80);
    pet_start_y_animation(g_pet_ui.sparkle_c,
                          PET_MASCOT_Y + 65, PET_MASCOT_Y + 47, 900, 360);

    PET_CreateWoodenFish();
    g_pet_ui.daily_summary = lv_label_create(g_pet_ui.root);
    lv_obj_set_width(g_pet_ui.daily_summary, 220);
    lv_obj_set_pos(g_pet_ui.daily_summary,
                   (LV_HOR_RES_MAX - 220) / 2, 48);
    lv_obj_set_style_pad_all(g_pet_ui.daily_summary, 9, 0);
    lv_obj_set_style_radius(g_pet_ui.daily_summary, 16, 0);
    lv_obj_set_style_border_width(g_pet_ui.daily_summary, 2, 0);
    lv_obj_set_style_border_color(
        g_pet_ui.daily_summary, lv_color_hex(0xFFDD6AU), 0);
    lv_obj_set_style_bg_color(
        g_pet_ui.daily_summary, lv_color_hex(0x6B340DU), 0);
    lv_obj_set_style_bg_opa(g_pet_ui.daily_summary, LV_OPA_90, 0);
    lv_obj_set_style_text_align(
        g_pet_ui.daily_summary, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(
        g_pet_ui.daily_summary, lv_color_hex(0xFFF4AAU), 0);
    lv_obj_set_style_text_font(
        g_pet_ui.daily_summary, &lv_font_montserrat_20, 0);
    lv_label_set_text(g_pet_ui.daily_summary, "Today's merit  0");
    lv_obj_add_flag(g_pet_ui.daily_summary, LV_OBJ_FLAG_HIDDEN);

    g_pet_ui.status_label = lv_label_create(g_pet_ui.root);
    lv_obj_set_width(g_pet_ui.status_label, LV_HOR_RES_MAX - 24);
    lv_obj_set_pos(g_pet_ui.status_label, 12, LV_VER_RES_MAX - 72);
    lv_obj_set_style_text_align(g_pet_ui.status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(g_pet_ui.status_label, lv_color_hex(0xA7B0B5), 0);
    lv_obj_set_style_text_font(g_pet_ui.status_label, &lv_font_montserrat_20, 0);

    g_pet_ui.task_label = lv_label_create(g_pet_ui.root);
    lv_obj_set_width(g_pet_ui.task_label, LV_HOR_RES_MAX - 24);
    lv_obj_set_pos(g_pet_ui.task_label, 12, LV_VER_RES_MAX - 40);
    lv_obj_set_style_text_align(g_pet_ui.task_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(g_pet_ui.task_label, lv_color_hex(0xD8F7EE), 0);

    PET_LoadMerit();
    g_pet_ui.wooden_timer = lv_timer_create(
        PET_EndWoodenFish,
        PET_WOODEN_FISH_IDLE_MS,
        NULL);
    lv_timer_pause(g_pet_ui.wooden_timer);

    g_pet_ui.daily_timer = lv_timer_create(
        PET_HideDailySummary,
        PET_DAILY_SUMMARY_MS,
        NULL);
    if (NULL != g_pet_ui.daily_timer)
    {
        lv_timer_pause(g_pet_ui.daily_timer);
    }

    g_pet_ui.ulRenderedGeneration = 0xFFFFFFFFUL;
    g_pet_ui.bRenderedConnected = false;
    g_pet_ui.status_timer = lv_timer_create(
        PET_RefreshStatus,
        PET_STATUS_REFRESH_MS,
        NULL);
    PET_ApplyStateAnimation(AGENTPET_STATE_IDLE);
    PET_RefreshStatus(g_pet_ui.status_timer);
}

static void pet_on_stop(void)
{
    if (g_pet_ui.status_timer)
    {
        lv_timer_del(g_pet_ui.status_timer);
        g_pet_ui.status_timer = NULL;
    }
    if (g_pet_ui.wooden_timer)
    {
        lv_timer_del(g_pet_ui.wooden_timer);
        g_pet_ui.wooden_timer = NULL;
    }
    if (g_pet_ui.daily_timer)
    {
        lv_timer_del(g_pet_ui.daily_timer);
        g_pet_ui.daily_timer = NULL;
    }
#ifndef BSP_USING_PC_SIMULATOR
    if (NULL != g_pet_ui.pPrefs)
    {
        rt_err_t tCloseResult;

        tCloseResult = share_prefs_close(g_pet_ui.pPrefs);
        if (RT_EOK != tCloseResult)
        {
            rt_kprintf("agent pet: close merit storage failed %d\n", tCloseResult);
        }
        g_pet_ui.pPrefs = NULL;
    }
#endif
    if (g_pet_ui.root)
    {
        lv_obj_del(g_pet_ui.root);
    }
    rt_memset(&g_pet_ui, 0, sizeof(g_pet_ui));
}

#if defined(BSP_USING_PC_SIMULATOR) && defined(AGENT_PET_STANDALONE_PREVIEW)
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
