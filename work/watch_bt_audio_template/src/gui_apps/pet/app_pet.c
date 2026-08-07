#include <rtthread.h>

#include "littlevgl2rtt.h"
#include "app_mem.h"
#include "agent_pet_ble_service.h"
#include "agent_quest_garden.h"
#include "agent_pet_merit.h"
#include "local_music_player.h"
#if !defined(BSP_USING_PC_SIMULATOR) || !defined(AGENT_PET_STANDALONE_PREVIEW)
    #include "lv_ext_resource_manager.h"
    #include "gui_app_fwk.h"
#endif
#ifndef BSP_USING_PC_SIMULATOR
    #include <time.h>
    #include "share_prefs.h"
#endif
#if defined(AGENT_PET_USING_IMU) && !defined(BSP_USING_PC_SIMULATOR)
    #include "pet_imu.h"
#endif

LV_IMG_DECLARE(agent_pet_mascot);
LV_IMG_DECLARE(agent_pet_wooden_fish);
LV_IMG_DECLARE(agent_pet_wooden_fish_mallet);
LV_IMG_DECLARE(agent_pet_merit_plus_one);

#define APP_ID "pet"
#define PET_STATUS_REFRESH_MS (100U)
#define PET_MAX_REMOTE_HITS_PER_REFRESH (4U)
#define PET_MASCOT_SIZE (192)
#define PET_MASCOT_X ((LV_HOR_RES_MAX - PET_MASCOT_SIZE) / 2)
#define PET_MASCOT_Y (((LV_VER_RES_MAX - PET_MASCOT_SIZE) / 2) - 12)
#define PET_GIF_MIN_FRAME_MS (20U)
#define PET_WOODEN_FISH_IDLE_MS (1700U)
#define PET_WOODEN_FISH_SOUND_PATH "/940muyu3.wav"
#define PET_DAILY_SUMMARY_MS (1900U)
#define PET_TURBO_INTERVAL_MS (180U)
#define PET_FAST_INTERVAL_MS (360U)
#define PET_QUICK_INTERVAL_MS (700U)
#define PET_WOODEN_FISH_WIDTH (210)
#define PET_WOODEN_FISH_HEIGHT (180)
#define PET_WOODEN_FISH_MALLET_X (50)
#define PET_WOODEN_FISH_MALLET_Y (18)
#define PET_ATTENTION_PANEL_WIDTH (320)
#define PET_ATTENTION_PANEL_HEIGHT (90)
#define PET_ATTENTION_PANEL_X ((LV_HOR_RES_MAX - PET_ATTENTION_PANEL_WIDTH) / 2)
#define PET_ATTENTION_PANEL_Y (38)
#define PET_MOTION_SAMPLE_MS (20U)
#define PET_MOTION_SWING_DYN_MG (120U)
#define PET_MOTION_SWING_GYRO_MDPS (60000U)
#define PET_MOTION_IMPACT_DYN_MG (800U)
#define PET_MOTION_IMPACT_GYRO_MDPS (25000U)
#define PET_MOTION_MAX_IMPACT_DELAY_MS (300U)
#define PET_MOTION_COOLDOWN_MS (180U)
#define PET_MOTION_FILTER_DIVISOR (2U)
#define PET_MOTION_MAX_READ_ERRORS (5U)
#define PET_MOTION_LABEL_X (LV_HOR_RES_MAX - 220)
#define PET_MOTION_LABEL_Y (11)
#define PET_MOTION_LABEL_WIDTH (100)
#define PET_MOTION_SWITCH_X (LV_HOR_RES_MAX - 112)
#define PET_MOTION_SWITCH_Y (8)
#define PET_MOTION_SWITCH_WIDTH (52)
#define PET_MOTION_SWITCH_HEIGHT (26)
#define PET_QUEST_GARDEN_ENABLED (1U)
#define PET_QUEST_GARDEN_WIDTH (76)
#ifndef BSP_USING_PC_SIMULATOR
    #define PET_QUEST_PREF_NAME "agent_pet_quest_garden_pref_v1_"
    #define PET_QUEST_PREF_VERSION_KEY "q_ver"
    #define PET_QUEST_PREF_DAY_KEY "q_day"
    #define PET_QUEST_PREF_COMPLETED_KEY "q_done"
    #define PET_QUEST_PREF_COLLECTED_KEY "q_claim"
    #define PET_QUEST_PREF_PENDING_KEY "q_pending"
    #define PET_QUEST_PREF_STREAK_KEY "q_streak"
    #define PET_QUEST_PREF_OVERFLOW_KEY "q_over"
#endif

#if defined(AGENT_PET_USING_IMU) && !defined(BSP_USING_PC_SIMULATOR)
/* PET_MOTION_STATE: 体感敲木鱼动作识别状态。 */
typedef enum _PET_MOTION_STATE
{
    PET_MOTION_STATE_IDLE = 0,
    PET_MOTION_STATE_SWING,
    PET_MOTION_STATE_COOLDOWN
} PET_MOTION_STATE;

/* PET_MOTION_DETECTOR: 体感动作状态机，仅保存定点滤波结果和阶段计时。
 * 成员说明：
 *   - eState: 当前识别阶段
 *   - ulFilteredDynamicMg: 滤波后的动态加速度，单位mg
 *   - ulFilteredGyroMdps: 滤波后的角速度模长，单位mdps
 *   - usStateTimeMs: 摆动阶段累计时间，范围0~450ms
 *   - usCooldownMs: 防重复触发冷却时间，范围0~280ms
 */
typedef struct _PET_MOTION_DETECTOR
{
    PET_MOTION_STATE eState;
    uint32_t ulFilteredDynamicMg;
    uint32_t ulFilteredGyroMdps;
    uint16_t usStateTimeMs;
    uint16_t usCooldownMs;
} PET_MOTION_DETECTOR;
#endif

typedef struct
{
    lv_obj_t *root;
    lv_obj_t *stage;
    lv_obj_t *mascot;
#if LV_USE_GIF
    lv_obj_t *mascot_gif;
    lv_timer_t *gif_timer;
    gd_GIF *pCustomGifDecoder;
#endif
    lv_obj_t *wooden_fish;
    lv_obj_t *fish_body;
    lv_obj_t *mallet;
    lv_obj_t *merit_image;
    lv_obj_t *daily_summary;
    lv_obj_t *sparkle_a;
    lv_obj_t *sparkle_b;
    lv_obj_t *sparkle_c;
    lv_obj_t *attention_panel;
    lv_obj_t *attention_title;
    lv_obj_t *attention_hint;
    lv_obj_t *garden_pot;
    lv_obj_t *garden_stem;
    lv_obj_t *aGardenLeaves[QUEST_GARDEN_MAX_LEAVES];
    lv_obj_t *garden_flower;
    lv_obj_t *garden_label;
    lv_obj_t *seed_button;
    lv_obj_t *seed_label;
    lv_obj_t *status_label;
    lv_obj_t *task_label;
    lv_obj_t *image_progress_panel;
    lv_obj_t *image_progress_label;
    lv_obj_t *image_progress_bar;
    lv_timer_t *status_timer;
    lv_timer_t *wooden_timer;
    lv_timer_t *daily_timer;
    lv_timer_t *motion_timer;
    lv_obj_t *motion_label;
    lv_obj_t *motion_switch;
#ifndef BSP_USING_PC_SIMULATOR
    share_prefs_t *pQuestPrefs;
#endif
    QUEST_GARDEN tQuestGarden;
    uint32_t ulRenderedGeneration;
    uint32_t ulRenderedWoodenFishGeneration;
    uint32_t ulRenderedImageGeneration;
    uint32_t ulRenderedMeritGeneration;
    uint32_t ulLastHitTick;
    uint32_t ulMeritCount;
    uint8_t ucRenderedState;
    uint8_t ucRenderedImageProgress;
    AGENTPET_IMAGE_STATE eRenderedImageState;
    lv_img_dsc_t tCustomMascot;
    uint8_t *pCustomMascotPixels;
#if LV_USE_GIF
    lv_img_dsc_t tCustomGif;
    uint8_t *pCustomGifData;
    uint8_t *pCustomGifPixels;
#endif
    bool bRenderedConnected;
    bool bRenderedCustomImage;
#if defined(AGENT_PET_USING_IMU) && !defined(BSP_USING_PC_SIMULATOR)
    PET_MOTION_DETECTOR tMotionDetector;
    uint8_t ucMotionReadErrors;
    bool bMotionEnabled;
#endif
} pet_ui_t;

static pet_ui_t g_pet_ui;

static void PET_ApplyStateAnimation(uint8_t ucState);
static void PET_PlayWoodenFishAnimation(const lv_point_t *pPoint);
static void PET_PlayWoodenFish(lv_event_t *pEvent);

#if defined(AGENT_PET_USING_IMU) && !defined(BSP_USING_PC_SIMULATOR)
/***************************
 * PET_IntegerSquareRoot: 计算64位无符号整数平方根的向下取整值
 * 参数：
 *   - udValue: 待开方数
 * 返回值：平方根的向下取整值
 ***************************/
static uint32_t PET_IntegerSquareRoot(uint64_t udValue)
{
    uint64_t udBit;
    uint64_t udResult;

    udBit = 1ULL << 62U;
    udResult = 0ULL;
    while (udValue < udBit)
    {
        udBit >>= 2U;
    }

    while (0ULL != udBit)
    {
        if (udResult + udBit <= udValue)
        {
            udValue -= udResult + udBit;
            udResult = (udResult >> 1U) + udBit;
        }
        else
        {
            udResult >>= 1U;
        }
        udBit >>= 2U;
    }

    return (uint32_t)udResult;
}

/***************************
 * PET_VectorMagnitude: 计算三轴有符号向量的模长
 * 参数：
 *   - lX/lY/lZ: 三轴输入，三轴必须使用相同单位
 * 返回值：三轴向量模长，保持输入单位
 ***************************/
static uint32_t PET_VectorMagnitude(int32_t lX, int32_t lY, int32_t lZ)
{
    int64_t dX;
    int64_t dY;
    int64_t dZ;
    uint64_t udSum;

    dX = lX;
    dY = lY;
    dZ = lZ;
    udSum = (uint64_t)(dX * dX) + (uint64_t)(dY * dY) +
        (uint64_t)(dZ * dZ);

    return PET_IntegerSquareRoot(udSum);
}

/***************************
 * PET_LowPassFilter: 对无符号传感器幅值执行四分之一权重低通滤波
 * 参数：
 *   - ulFiltered: 上一次滤波结果
 *   - ulInput: 本次输入值
 * 返回值：更新后的滤波结果
 ***************************/
static uint32_t PET_LowPassFilter(uint32_t ulFiltered, uint32_t ulInput)
{
    int64_t dFiltered;
    int64_t dInput;

    dFiltered = ulFiltered;
    dInput = ulInput;
    dFiltered += (dInput - dFiltered) / (int64_t)PET_MOTION_FILTER_DIVISOR;

    return (uint32_t)dFiltered;
}

/***************************
 * PET_MotionDetectorReset: 重置体感动作状态机和滤波历史
 * 参数：
 *   - pDetector: 状态机指针
 * 返回值：无
 ***************************/
static void PET_MotionDetectorReset(PET_MOTION_DETECTOR *pDetector)
{
    if (NULL != pDetector)
    {
        rt_memset(pDetector, 0, sizeof(*pDetector));
        pDetector->eState = PET_MOTION_STATE_IDLE;
    }

    return;
}

/***************************
 * PET_MotionDetectorUpdate: 使用一次六轴采样更新敲木鱼动作状态机
 * 参数：
 *   - pDetector: 状态机指针
 *   - pSample: 六轴采样输入指针
 *   - usDeltaMs: 本次采样与上次采样的间隔，单位ms
 * 返回值：检测到一次有效敲击返回true，否则返回false
 ***************************/
static bool PET_MotionDetectorUpdate(PET_MOTION_DETECTOR *pDetector,
                                     const PET_IMU_SAMPLE *pSample,
                                     uint16_t usDeltaMs)
{
    uint32_t ulAccelMagnitude;
    uint32_t ulGyroMagnitude;
    uint32_t ulDynamicMg;

    if ((NULL == pDetector) || (NULL == pSample))
    {
        return false;
    }

    ulAccelMagnitude = PET_VectorMagnitude(
        pSample->lAccelXMg,
        pSample->lAccelYMg,
        pSample->lAccelZMg);
    ulGyroMagnitude = PET_VectorMagnitude(
        pSample->lGyroXMdps,
        pSample->lGyroYMdps,
        pSample->lGyroZMdps);
    ulDynamicMg = (1000U < ulAccelMagnitude) ?
        (ulAccelMagnitude - 1000U) : (1000U - ulAccelMagnitude);

    pDetector->ulFilteredDynamicMg = PET_LowPassFilter(
        pDetector->ulFilteredDynamicMg,
        ulDynamicMg);
    pDetector->ulFilteredGyroMdps = PET_LowPassFilter(
        pDetector->ulFilteredGyroMdps,
        ulGyroMagnitude);

    switch (pDetector->eState)
    {
    case PET_MOTION_STATE_IDLE:
        if ((PET_MOTION_IMPACT_DYN_MG < ulDynamicMg) &&
            (PET_MOTION_IMPACT_GYRO_MDPS <
             pDetector->ulFilteredGyroMdps))
        {
            pDetector->eState = PET_MOTION_STATE_COOLDOWN;
            pDetector->usCooldownMs = 0U;
            return true;
        }
        if ((PET_MOTION_SWING_DYN_MG < pDetector->ulFilteredDynamicMg) ||
            (PET_MOTION_SWING_GYRO_MDPS < pDetector->ulFilteredGyroMdps))
        {
            pDetector->eState = PET_MOTION_STATE_SWING;
            pDetector->usStateTimeMs = 0U;
        }
        break;

    case PET_MOTION_STATE_SWING:
        pDetector->usStateTimeMs = (uint16_t)(pDetector->usStateTimeMs + usDeltaMs);
        if ((PET_MOTION_IMPACT_DYN_MG < ulDynamicMg) &&
            (PET_MOTION_IMPACT_GYRO_MDPS < pDetector->ulFilteredGyroMdps))
        {
            pDetector->eState = PET_MOTION_STATE_COOLDOWN;
            pDetector->usCooldownMs = 0U;
            return true;
        }
        if (PET_MOTION_MAX_IMPACT_DELAY_MS < pDetector->usStateTimeMs)
        {
            pDetector->eState = PET_MOTION_STATE_IDLE;
            pDetector->usStateTimeMs = 0U;
        }
        break;

    case PET_MOTION_STATE_COOLDOWN:
        pDetector->usCooldownMs = (uint16_t)(pDetector->usCooldownMs + usDeltaMs);
        if (PET_MOTION_COOLDOWN_MS <= pDetector->usCooldownMs)
        {
            pDetector->eState = PET_MOTION_STATE_IDLE;
            pDetector->usStateTimeMs = 0U;
        }
        break;

    default:
        PET_MotionDetectorReset(pDetector);
        break;
    }

    return false;
}

/***************************
 * PET_StopMotionDetection: 停止体感采样并关闭六轴数据通道
 * 参数：无
 * 返回值：无
 ***************************/
static void PET_StopMotionDetection(void)
{
    g_pet_ui.bMotionEnabled = false;
    g_pet_ui.ucMotionReadErrors = 0U;
    PET_MotionDetectorReset(&g_pet_ui.tMotionDetector);
    if (NULL != g_pet_ui.motion_timer)
    {
        lv_timer_pause(g_pet_ui.motion_timer);
    }
    if (PETIMU_IsReady())
    {
        (void)PETIMU_SetEnabled(false);
    }

    return;
}

/***************************
 * PET_MotionSample: 周期读取六轴并在检测到有效动作时触发木鱼动画
 * 参数：
 *   - pTimer: 20ms LVGL采样定时器
 * 返回值：无
 ***************************/
static void PET_MotionSample(lv_timer_t *pTimer)
{
    PET_IMU_SAMPLE tSample;
    int32_t lRetVal;

    (void)pTimer;
    if (false == g_pet_ui.bMotionEnabled)
    {
        return;
    }

    lRetVal = PETIMU_Read(&tSample);
    if (RT_EOK != lRetVal)
    {
        g_pet_ui.ucMotionReadErrors++;
        if (PET_MOTION_MAX_READ_ERRORS <= g_pet_ui.ucMotionReadErrors)
        {
            PET_StopMotionDetection();
            if (NULL != g_pet_ui.motion_switch)
            {
                lv_obj_clear_state(g_pet_ui.motion_switch, LV_STATE_CHECKED);
            }
            if (NULL != g_pet_ui.motion_label)
            {
                lv_label_set_text(g_pet_ui.motion_label, "Motion error");
            }
            rt_kprintf("agent pet: motion sampling stopped %ld\n", (long)lRetVal);
        }
        return;
    }

    g_pet_ui.ucMotionReadErrors = 0U;
    if (PET_MotionDetectorUpdate(
            &g_pet_ui.tMotionDetector,
            &tSample,
            PET_MOTION_SAMPLE_MS))
    {
        PET_PlayWoodenFishAnimation(NULL);
        rt_kprintf("agent pet: motion wooden fish hit\n");
    }

    return;
}

/***************************
 * PET_MotionSwitchChanged: 处理宠物页面体感模式开关变化
 * 参数：
 *   - pEvent: LVGL开关状态变化事件
 * 返回值：无
 ***************************/
static void PET_MotionSwitchChanged(lv_event_t *pEvent)
{
    int32_t lRetVal;

    if ((NULL == pEvent) || (NULL == g_pet_ui.motion_switch))
    {
        return;
    }

    if (lv_obj_has_state(g_pet_ui.motion_switch, LV_STATE_CHECKED))
    {
        lRetVal = PETIMU_Init();
        if (RT_EOK == lRetVal)
        {
            lRetVal = PETIMU_SetEnabled(true);
        }
        if ((RT_EOK != lRetVal) || (NULL == g_pet_ui.motion_timer))
        {
            PET_StopMotionDetection();
            lv_obj_clear_state(g_pet_ui.motion_switch, LV_STATE_CHECKED);
            lv_label_set_text(g_pet_ui.motion_label, "Motion unavailable");
            rt_kprintf("agent pet: enable motion failed %ld\n", (long)lRetVal);
            return;
        }

        PET_MotionDetectorReset(&g_pet_ui.tMotionDetector);
        g_pet_ui.ucMotionReadErrors = 0U;
        g_pet_ui.bMotionEnabled = true;
        lv_label_set_text(g_pet_ui.motion_label, "Motion On");
        lv_timer_reset(g_pet_ui.motion_timer);
        lv_timer_resume(g_pet_ui.motion_timer);
    }
    else
    {
        PET_StopMotionDetection();
        lv_label_set_text(g_pet_ui.motion_label, "Motion Off");
    }

    return;
}
#endif

/***************************
 * PET_CreateMotionSwitch: 创建宠物页面独立体感模式开关
 * 参数：无
 * 返回值：无
 ***************************/
static void PET_CreateMotionSwitch(void)
{
    g_pet_ui.motion_label = lv_label_create(g_pet_ui.root);
    lv_obj_set_pos(
        g_pet_ui.motion_label,
        PET_MOTION_LABEL_X,
        PET_MOTION_LABEL_Y);
    lv_obj_set_width(g_pet_ui.motion_label, PET_MOTION_LABEL_WIDTH);
    lv_obj_set_style_text_align(g_pet_ui.motion_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(
        g_pet_ui.motion_label,
        lv_color_hex(0xA7B0B5U),
        0);
    lv_obj_set_style_text_opa(g_pet_ui.motion_label, LV_OPA_COVER, 0);

    g_pet_ui.motion_switch = lv_switch_create(g_pet_ui.root);
    lv_obj_set_size(
        g_pet_ui.motion_switch,
        PET_MOTION_SWITCH_WIDTH,
        PET_MOTION_SWITCH_HEIGHT);
    lv_obj_set_pos(
        g_pet_ui.motion_switch,
        PET_MOTION_SWITCH_X,
        PET_MOTION_SWITCH_Y);

#if defined(AGENT_PET_USING_IMU) && !defined(BSP_USING_PC_SIMULATOR)
    lv_label_set_text(g_pet_ui.motion_label, "Motion Off");
    lv_obj_add_event_cb(
        g_pet_ui.motion_switch,
        PET_MotionSwitchChanged,
        LV_EVENT_VALUE_CHANGED,
        NULL);
    g_pet_ui.motion_timer = lv_timer_create(
        PET_MotionSample,
        PET_MOTION_SAMPLE_MS,
        NULL);
    if (NULL != g_pet_ui.motion_timer)
    {
        lv_timer_pause(g_pet_ui.motion_timer);
    }
    PET_StopMotionDetection();
#else
    lv_label_set_text(g_pet_ui.motion_label, "Motion N/A");
    lv_obj_add_state(g_pet_ui.motion_switch, LV_STATE_DISABLED);
#endif

    return;
}

/*
 * PET_CreateAttentionCue
 * Function: Create the bounded task-attention overlay used for blocked and
 * completed states. The objects remain allocated for the page lifetime and
 * are only hidden or restyled during state changes.
 * Parameters: none.
 * Return: none.
 */
static void PET_CreateAttentionCue(void)
{
    g_pet_ui.attention_panel = lv_obj_create(g_pet_ui.root);
    lv_obj_set_pos(
        g_pet_ui.attention_panel,
        PET_ATTENTION_PANEL_X,
        PET_ATTENTION_PANEL_Y);
    lv_obj_set_size(
        g_pet_ui.attention_panel,
        PET_ATTENTION_PANEL_WIDTH,
        PET_ATTENTION_PANEL_HEIGHT);
    lv_obj_set_style_bg_opa(g_pet_ui.attention_panel, LV_OPA_90, 0);
    lv_obj_set_style_border_width(g_pet_ui.attention_panel, 2, 0);
    lv_obj_set_style_radius(g_pet_ui.attention_panel, 18, 0);
    lv_obj_set_style_pad_all(g_pet_ui.attention_panel, 8, 0);
    lv_obj_clear_flag(g_pet_ui.attention_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(g_pet_ui.attention_panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(g_pet_ui.attention_panel, LV_OBJ_FLAG_HIDDEN);

    g_pet_ui.attention_title = lv_label_create(g_pet_ui.attention_panel);
    lv_obj_set_width(g_pet_ui.attention_title, LV_PCT(100));
    lv_obj_set_style_text_align(
        g_pet_ui.attention_title,
        LV_TEXT_ALIGN_CENTER,
        0);
    lv_obj_set_style_text_letter_space(g_pet_ui.attention_title, 2, 0);
    lv_obj_set_style_text_opa(g_pet_ui.attention_title, LV_OPA_COVER, 0);
    lv_obj_align(g_pet_ui.attention_title, LV_ALIGN_TOP_MID, 0, 0);

    g_pet_ui.attention_hint = lv_label_create(g_pet_ui.attention_panel);
    lv_obj_set_width(g_pet_ui.attention_hint, LV_PCT(100));
    lv_label_set_text(g_pet_ui.attention_hint, "Check your computer");
    lv_obj_set_style_text_align(
        g_pet_ui.attention_hint,
        LV_TEXT_ALIGN_CENTER,
        0);
    lv_obj_set_style_text_color(
        g_pet_ui.attention_hint,
        lv_color_hex(0xD8F7EEU),
        0);
    lv_obj_set_style_text_letter_space(g_pet_ui.attention_hint, 1, 0);
    lv_obj_set_style_text_opa(g_pet_ui.attention_hint, LV_OPA_COVER, 0);
    lv_obj_align(g_pet_ui.attention_hint, LV_ALIGN_BOTTOM_MID, 0, 0);

    return;
}

static void PET_ReleaseCustomMascot(void)
{
    if (NULL == g_pet_ui.pCustomMascotPixels)
    {
        return;
    }

    lv_img_cache_invalidate_src(&g_pet_ui.tCustomMascot);
    app_cache_free(g_pet_ui.pCustomMascotPixels);
    g_pet_ui.pCustomMascotPixels = NULL;
    rt_memset(&g_pet_ui.tCustomMascot, 0, sizeof(g_pet_ui.tCustomMascot));
}

#if LV_USE_GIF
/*
 * PET_CustomGifDelay
 * Function: convert the current GIF centisecond delay to a bounded LVGL period.
 * Parameters: none.
 * Return: frame period in milliseconds, never below 20 ms.
 */
static uint32_t PET_CustomGifDelay(void)
{
    uint32_t ulDelay;

    if (NULL == g_pet_ui.pCustomGifDecoder)
    {
        return PET_GIF_MIN_FRAME_MS;
    }
    ulDelay = (uint32_t)g_pet_ui.pCustomGifDecoder->gce.delay * 10U;

    return LV_MAX(PET_GIF_MIN_FRAME_MS, ulDelay);
}

/*
 * PET_AdvanceCustomGif
 * Function: decode one GIF frame and schedule the next callback from its delay.
 * Parameters:
 *   - pTimer: dedicated GIF playback timer.
 * Return: none.
 */
static void PET_AdvanceCustomGif(lv_timer_t *pTimer)
{
    int lResult;

    if ((NULL == pTimer) ||
        (NULL == g_pet_ui.pCustomGifDecoder) ||
        (NULL == g_pet_ui.pCustomGifPixels) ||
        (NULL == g_pet_ui.mascot_gif))
    {
        return;
    }

    lResult = gd_get_frame(g_pet_ui.pCustomGifDecoder);
    if (0 == lResult)
    {
        if (1U == g_pet_ui.pCustomGifDecoder->loop_count)
        {
            lv_timer_pause(pTimer);
            return;
        }
        if (1U < g_pet_ui.pCustomGifDecoder->loop_count)
        {
            g_pet_ui.pCustomGifDecoder->loop_count--;
        }
        gd_rewind(g_pet_ui.pCustomGifDecoder);
        lResult = gd_get_frame(g_pet_ui.pCustomGifDecoder);
    }
    if (1 != lResult)
    {
        lv_timer_pause(pTimer);
        rt_kprintf("agent pet: custom GIF decode stopped %d\n", lResult);
        return;
    }

    rt_memcpy(
        g_pet_ui.pCustomGifPixels,
        g_pet_ui.pCustomGifDecoder->canvas,
        g_pet_ui.tCustomGif.data_size);
    gd_render_frame(
        g_pet_ui.pCustomGifDecoder,
        g_pet_ui.pCustomGifPixels);
    lv_img_cache_invalidate_src(&g_pet_ui.tCustomGif);
    lv_obj_invalidate(g_pet_ui.mascot_gif);
    lv_timer_set_period(pTimer, PET_CustomGifDelay());

    return;
}

/*
 * PET_ReleaseCustomGif
 * Function: stop GIF decoding before releasing its persistent PSRAM source.
 * Parameters: none.
 * Return: none.
 */
static void PET_ReleaseCustomGif(void)
{
    if (NULL != g_pet_ui.gif_timer)
    {
        lv_timer_del(g_pet_ui.gif_timer);
        g_pet_ui.gif_timer = NULL;
    }
    if (NULL != g_pet_ui.mascot_gif)
    {
        lv_obj_del(g_pet_ui.mascot_gif);
        g_pet_ui.mascot_gif = NULL;
    }
    if (NULL != g_pet_ui.pCustomGifDecoder)
    {
        gd_close_gif(g_pet_ui.pCustomGifDecoder);
        g_pet_ui.pCustomGifDecoder = NULL;
    }
    if (NULL != g_pet_ui.pCustomGifPixels)
    {
        lv_img_cache_invalidate_src(&g_pet_ui.tCustomGif);
        app_cache_free(g_pet_ui.pCustomGifPixels);
        g_pet_ui.pCustomGifPixels = NULL;
    }
    if (NULL != g_pet_ui.pCustomGifData)
    {
        app_cache_free(g_pet_ui.pCustomGifData);
        g_pet_ui.pCustomGifData = NULL;
    }
    rt_memset(&g_pet_ui.tCustomGif, 0, sizeof(g_pet_ui.tCustomGif));

    return;
}

/*
 * PET_LoadCustomGif
 * Function: copy the bounded committed GIF into PSRAM and start LVGL playback.
 * Parameters:
 *   - pHeader: output logical GIF dimensions.
 * Return: true when the GIF object is playing, otherwise false.
 */
static bool PET_LoadCustomGif(lv_img_header_t *pHeader)
{
    lv_fs_file_t tFile;
    lv_fs_res_t eResult;
    uint32_t ulFileSize;
    uint32_t ulOffset;
    uint32_t ulReadLength;
    uint32_t ulChunkSize;

    if (NULL == pHeader)
    {
        return false;
    }

    (void)rt_memset(&tFile, 0, sizeof(tFile));
    eResult = lv_fs_open(&tFile, AGENTPET_IMAGE_LVGL_PATH, LV_FS_MODE_RD);
    if (LV_FS_RES_OK != eResult)
    {
        rt_kprintf("agent pet: custom GIF open failed %d\n", eResult);
        return false;
    }
    eResult = lv_fs_seek(&tFile, 0U, LV_FS_SEEK_END);
    if (LV_FS_RES_OK == eResult)
    {
        eResult = lv_fs_tell(&tFile, &ulFileSize);
    }
    if (LV_FS_RES_OK == eResult)
    {
        eResult = lv_fs_seek(&tFile, 0U, LV_FS_SEEK_SET);
    }
    if ((LV_FS_RES_OK != eResult) ||
        (14U > ulFileSize) ||
        (AGENTPET_IMAGE_MAX_FILE_SIZE < ulFileSize))
    {
        (void)lv_fs_close(&tFile);
        rt_kprintf("agent pet: custom GIF size invalid %lu\n",
                   (unsigned long)ulFileSize);
        return false;
    }

    g_pet_ui.pCustomGifData = app_cache_alloc(
        ulFileSize,
        IMAGE_CACHE_PSRAM);
    if (NULL == g_pet_ui.pCustomGifData)
    {
        (void)lv_fs_close(&tFile);
        rt_kprintf("agent pet: custom GIF source allocation failed %lu\n",
                   (unsigned long)ulFileSize);
        return false;
    }

    ulOffset = 0U;
    while (ulOffset < ulFileSize)
    {
        ulChunkSize = LV_MIN(4096U, ulFileSize - ulOffset);
        ulReadLength = 0U;
        eResult = lv_fs_read(
            &tFile,
            &g_pet_ui.pCustomGifData[ulOffset],
            ulChunkSize,
            &ulReadLength);
        if ((LV_FS_RES_OK != eResult) || (ulChunkSize != ulReadLength))
        {
            (void)lv_fs_close(&tFile);
            PET_ReleaseCustomGif();
            rt_kprintf("agent pet: custom GIF read failed at %lu\n",
                       (unsigned long)ulOffset);
            return false;
        }
        ulOffset += ulReadLength;
    }
    (void)lv_fs_close(&tFile);

    g_pet_ui.pCustomGifDecoder = gd_open_gif_data(g_pet_ui.pCustomGifData);
    if (NULL == g_pet_ui.pCustomGifDecoder)
    {
        PET_ReleaseCustomGif();
        rt_kprintf("agent pet: custom GIF decoder open failed\n");
        return false;
    }
    if ((0U == g_pet_ui.pCustomGifDecoder->width) ||
        (0U == g_pet_ui.pCustomGifDecoder->height) ||
        (PET_MASCOT_SIZE < g_pet_ui.pCustomGifDecoder->width) ||
        (PET_MASCOT_SIZE < g_pet_ui.pCustomGifDecoder->height))
    {
        rt_kprintf("agent pet: custom GIF dimensions invalid %u x %u\n",
                   g_pet_ui.pCustomGifDecoder->width,
                   g_pet_ui.pCustomGifDecoder->height);
        PET_ReleaseCustomGif();
        return false;
    }
    if (1 != gd_get_frame(g_pet_ui.pCustomGifDecoder))
    {
        PET_ReleaseCustomGif();
        rt_kprintf("agent pet: custom GIF first frame failed\n");
        return false;
    }

    pHeader->always_zero = 0U;
    pHeader->w = g_pet_ui.pCustomGifDecoder->width;
    pHeader->h = g_pet_ui.pCustomGifDecoder->height;
    pHeader->cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    g_pet_ui.tCustomGif.header = *pHeader;
    g_pet_ui.tCustomGif.data_size = (uint32_t)pHeader->w * pHeader->h *
        LV_IMG_PX_SIZE_ALPHA_BYTE;
    g_pet_ui.pCustomGifPixels = app_cache_alloc(
        g_pet_ui.tCustomGif.data_size,
        IMAGE_CACHE_PSRAM);
    if (NULL == g_pet_ui.pCustomGifPixels)
    {
        rt_kprintf("agent pet: custom GIF display allocation failed %lu\n",
                   (unsigned long)g_pet_ui.tCustomGif.data_size);
        PET_ReleaseCustomGif();
        return false;
    }
    g_pet_ui.tCustomGif.data = g_pet_ui.pCustomGifPixels;
    rt_memcpy(
        g_pet_ui.pCustomGifPixels,
        g_pet_ui.pCustomGifDecoder->canvas,
        g_pet_ui.tCustomGif.data_size);
    gd_render_frame(
        g_pet_ui.pCustomGifDecoder,
        g_pet_ui.pCustomGifPixels);
    g_pet_ui.mascot_gif = lv_img_create(g_pet_ui.stage);
    if (NULL == g_pet_ui.mascot_gif)
    {
        PET_ReleaseCustomGif();
        return false;
    }
    lv_img_set_src(g_pet_ui.mascot_gif, &g_pet_ui.tCustomGif);
    lv_obj_add_flag(g_pet_ui.mascot_gif, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        g_pet_ui.mascot_gif,
        PET_PlayWoodenFish,
        LV_EVENT_SHORT_CLICKED,
        NULL);
    g_pet_ui.gif_timer = lv_timer_create(
        PET_AdvanceCustomGif,
        PET_CustomGifDelay(),
        NULL);
    if (NULL == g_pet_ui.gif_timer)
    {
        PET_ReleaseCustomGif();
        return false;
    }
    rt_kprintf("agent pet: custom GIF playing %u x %u, %lu bytes\n",
               pHeader->w,
               pHeader->h,
               (unsigned long)ulFileSize);

    return true;
}
#endif /* LV_USE_GIF */

static bool PET_DecodeCustomMascot(lv_img_header_t *pHeader)
{
    lv_img_decoder_dsc_t tDecoder;
    uint32_t ulDataSize;
    uint16_t usRow;
    lv_res_t eResult;

    if (NULL == pHeader)
    {
        return false;
    }

    (void)rt_memset(&tDecoder, 0, sizeof(tDecoder));
    lv_img_cache_invalidate_src(NULL);
    eResult = lv_img_decoder_open(
        &tDecoder,
        AGENTPET_IMAGE_LVGL_PATH,
        lv_color_black(),
        0);
    if ((LV_RES_OK != eResult) ||
        (0U == tDecoder.header.w) ||
        (0U == tDecoder.header.h) ||
        (PET_MASCOT_SIZE < tDecoder.header.w) ||
        (PET_MASCOT_SIZE < tDecoder.header.h))
    {
        rt_kprintf("agent pet: custom JPEG open failed %d (%u x %u)\n",
                   eResult, tDecoder.header.w, tDecoder.header.h);
        if (LV_RES_OK == eResult)
        {
            lv_img_decoder_close(&tDecoder);
        }
        return false;
    }

    ulDataSize = (uint32_t)tDecoder.header.w * tDecoder.header.h *
        sizeof(lv_color_t);
    g_pet_ui.pCustomMascotPixels = app_cache_alloc(
        ulDataSize,
        IMAGE_CACHE_PSRAM);
    if (NULL == g_pet_ui.pCustomMascotPixels)
    {
        rt_kprintf("agent pet: custom RGB buffer allocation failed %lu\n",
                   (unsigned long)ulDataSize);
        lv_img_decoder_close(&tDecoder);
        return false;
    }

    for (usRow = 0U; usRow < tDecoder.header.h; usRow++)
    {
        eResult = lv_img_decoder_read_line(
            &tDecoder,
            0,
            usRow,
            tDecoder.header.w,
            g_pet_ui.pCustomMascotPixels +
                ((uint32_t)usRow * tDecoder.header.w * sizeof(lv_color_t)));
        if (LV_RES_OK != eResult)
        {
            rt_kprintf("agent pet: custom JPEG decode failed at row %u\n",
                       usRow);
            lv_img_decoder_close(&tDecoder);
            PET_ReleaseCustomMascot();
            return false;
        }
    }

    *pHeader = tDecoder.header;
    lv_img_decoder_close(&tDecoder);
    g_pet_ui.tCustomMascot.header.always_zero = 0;
    g_pet_ui.tCustomMascot.header.w = pHeader->w;
    g_pet_ui.tCustomMascot.header.h = pHeader->h;
    g_pet_ui.tCustomMascot.header.cf = LV_IMG_CF_TRUE_COLOR;
    g_pet_ui.tCustomMascot.data_size = ulDataSize;
    g_pet_ui.tCustomMascot.data = g_pet_ui.pCustomMascotPixels;
    rt_kprintf("agent pet: custom JPEG decoded %u x %u\n",
               pHeader->w, pHeader->h);
    return true;
}

/*
 * PET_MascotZoom
 * Function: fit an image into the common 192px stage without enlarging it.
 * Parameters:
 *   - pHeader: decoded source dimensions; must be non-NULL and non-zero.
 * Return: bounded LVGL zoom value, or 1x for invalid dimensions.
 */
static uint16_t PET_MascotZoom(const lv_img_header_t *pHeader)
{
    uint32_t ulZoomWidth;
    uint32_t ulZoomHeight;

    if ((NULL == pHeader) || (0U == pHeader->w) || (0U == pHeader->h))
    {
        return LV_IMG_ZOOM_NONE;
    }

    ulZoomWidth = (uint32_t)PET_MASCOT_SIZE * LV_IMG_ZOOM_NONE / pHeader->w;
    ulZoomHeight = (uint32_t)PET_MASCOT_SIZE * LV_IMG_ZOOM_NONE / pHeader->h;

    return (uint16_t)LV_MIN(
        LV_IMG_ZOOM_NONE,
        LV_MIN(ulZoomWidth, ulZoomHeight));
}
static void PET_UpdateQuestGarden(void);
static void PET_SaveQuestGarden(void);
static uint32_t PET_QuestCurrentDay(void);

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
 * PET_RefreshMascotImage
 * Function: switch atomically committed custom JPEG or GIF images on the LVGL thread.
 * Parameters:
 *   - pStatus: read-only persistent image status.
 * Return: none.
 */
static void PET_RefreshMascotImage(const AGENTPET_IMAGE_STATUS *pStatus)
{
    lv_img_header_t tHeader;
    uint16_t usZoom;

    if ((NULL == pStatus) || (NULL == g_pet_ui.mascot))
    {
        return;
    }
    if (
        (g_pet_ui.ulRenderedImageGeneration == pStatus->ulGeneration) &&
        (g_pet_ui.bRenderedCustomImage == pStatus->bImageAvailable)
    )
    {
        return;
    }

    g_pet_ui.ulRenderedImageGeneration = pStatus->ulGeneration;
    g_pet_ui.bRenderedCustomImage = pStatus->bImageAvailable;
#if LV_USE_GIF
    PET_ReleaseCustomGif();
#endif
    lv_img_set_src(g_pet_ui.mascot, &agent_pet_mascot);
    lv_img_set_zoom(g_pet_ui.mascot, PET_MascotZoom(&agent_pet_mascot.header));
    lv_obj_clear_flag(g_pet_ui.mascot, LV_OBJ_FLAG_HIDDEN);
    PET_ReleaseCustomMascot();
    if (pStatus->bImageAvailable)
    {
#if LV_USE_GIF
        if ((AGENTPET_IMAGE_FORMAT_GIF == pStatus->ucFormat) &&
            PET_LoadCustomGif(&tHeader))
        {
            lv_anim_del(g_pet_ui.stage, NULL);
            lv_anim_del(g_pet_ui.attention_panel, NULL);
            lv_obj_set_pos(g_pet_ui.stage, PET_MASCOT_X, PET_MASCOT_Y);
            lv_obj_set_pos(
                g_pet_ui.attention_panel,
                PET_ATTENTION_PANEL_X,
                PET_ATTENTION_PANEL_Y);
            usZoom = PET_MascotZoom(&tHeader);
            lv_img_set_zoom(g_pet_ui.mascot_gif, usZoom);
            lv_img_set_antialias(g_pet_ui.mascot_gif, false);
            lv_obj_center(g_pet_ui.mascot_gif);
            lv_obj_add_flag(g_pet_ui.mascot, LV_OBJ_FLAG_HIDDEN);
        }
        else
#endif /* LV_USE_GIF */
        if ((AGENTPET_IMAGE_FORMAT_JPEG == pStatus->ucFormat) &&
            PET_DecodeCustomMascot(&tHeader))
        {
            usZoom = PET_MascotZoom(&tHeader);
            lv_img_set_src(g_pet_ui.mascot, &g_pet_ui.tCustomMascot);
            lv_img_set_zoom(g_pet_ui.mascot, usZoom);
            lv_img_set_antialias(g_pet_ui.mascot, false);
        }
    }
    lv_obj_center(g_pet_ui.mascot);

    return;
}

/*
 * PET_RefreshImageProgress
 * Function: show live BLE image-transfer progress on the pet screen.
 * Parameters:
 *   - pStatus: read-only image receiver status.
 * Return: none.
 */
static void PET_RefreshImageProgress(const AGENTPET_IMAGE_STATUS *pStatus)
{
    uint8_t ucProgress;

    if ((NULL == pStatus) || (NULL == g_pet_ui.image_progress_panel))
    {
        return;
    }

    if (AGENTPET_IMAGE_RECEIVING != pStatus->eState)
    {
        if (AGENTPET_IMAGE_RECEIVING == g_pet_ui.eRenderedImageState)
        {
            lv_obj_add_flag(
                g_pet_ui.image_progress_panel,
                LV_OBJ_FLAG_HIDDEN);
        }
        g_pet_ui.eRenderedImageState = pStatus->eState;
        g_pet_ui.ucRenderedImageProgress = 0xFFU;
        return;
    }

    ucProgress = 0U;
    if (0U != pStatus->ulTotal)
    {
        ucProgress = (uint8_t)(((uint64_t)pStatus->ulReceived * 100ULL) /
                               pStatus->ulTotal);
        if (100U < ucProgress)
        {
            ucProgress = 100U;
        }
    }

    if ((AGENTPET_IMAGE_RECEIVING != g_pet_ui.eRenderedImageState) ||
        (ucProgress != g_pet_ui.ucRenderedImageProgress))
    {
        lv_label_set_text_fmt(
            g_pet_ui.image_progress_label,
            "Receiving image  %u%%",
            ucProgress);
        lv_bar_set_value(g_pet_ui.image_progress_bar, ucProgress, LV_ANIM_OFF);
        lv_obj_clear_flag(
            g_pet_ui.image_progress_panel,
            LV_OBJ_FLAG_HIDDEN);
    }

    g_pet_ui.eRenderedImageState = pStatus->eState;
    g_pet_ui.ucRenderedImageProgress = ucProgress;
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
    AGENTPET_MERIT_SNAPSHOT tMeritSnapshot;
    const AGENTPET_SESSION *pSession;
    QUEST_GARDEN_RESULT tQuestResult;
    uint32_t ulQuestDay;
    uint32_t ulPendingHitCount;
    uint8_t ucHitIndex;

    (void)pTimer;
    if (AGENTPETMERIT_GetSnapshot(&tMeritSnapshot) &&
        (g_pet_ui.ulRenderedMeritGeneration != tMeritSnapshot.ulGeneration))
    {
        g_pet_ui.ulMeritCount = tMeritSnapshot.ulCount;
        g_pet_ui.ulRenderedMeritGeneration = tMeritSnapshot.ulGeneration;
        if (NULL != g_pet_ui.daily_summary)
        {
            lv_label_set_text_fmt(
                g_pet_ui.daily_summary,
                "Today's merit  %lu",
                (unsigned long)g_pet_ui.ulMeritCount);
        }
    }
    if (!AGENTPETBLE_GetStatus(&tStatus))
    {
        return;
    }
    PET_RefreshImageProgress(&tStatus.tImageStatus);
    PET_RefreshMascotImage(&tStatus.tImageStatus);
    ulQuestDay = PET_QuestCurrentDay();
    if (QUESTGARDEN_Rollover(
            &g_pet_ui.tQuestGarden, ulQuestDay, &tQuestResult) &&
        (true == tQuestResult.bChanged))
    {
        PET_UpdateQuestGarden();
        if (true == tQuestResult.bSaveRequired)
        {
            PET_SaveQuestGarden();
        }
    }
    if (tStatus.bHasWoodenFishEvent)
    {
        ulPendingHitCount = tStatus.ulWoodenFishGeneration -
            g_pet_ui.ulRenderedWoodenFishGeneration;
        if (PET_MAX_REMOTE_HITS_PER_REFRESH < ulPendingHitCount)
        {
            ulPendingHitCount = PET_MAX_REMOTE_HITS_PER_REFRESH;
        }
        for (ucHitIndex = 0U; ucHitIndex < ulPendingHitCount; ucHitIndex++)
        {
            PET_PlayWoodenFishAnimation(NULL);
        }
        g_pet_ui.ulRenderedWoodenFishGeneration += ulPendingHitCount;
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

    if (!QUESTGARDEN_ProcessSnapshot(
            &g_pet_ui.tQuestGarden, ulQuestDay,
            &tStatus.tSnapshot, &tQuestResult))
    {
        rt_kprintf("agent pet: quest snapshot rejected\n");
    }
    else if (true == tQuestResult.bChanged)
    {
        PET_UpdateQuestGarden();
        if (true == tQuestResult.bSaveRequired)
        {
            PET_SaveQuestGarden();
        }
        if ((0U != tQuestResult.ucNewSeedCount) &&
            (NULL != g_pet_ui.daily_summary) &&
            (NULL != g_pet_ui.daily_timer))
        {
            lv_label_set_text(g_pet_ui.daily_summary, "Seed ready");
            lv_obj_clear_flag(
                g_pet_ui.daily_summary, LV_OBJ_FLAG_HIDDEN);
            lv_timer_reset(g_pet_ui.daily_timer);
            lv_timer_resume(g_pet_ui.daily_timer);
        }
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
    lv_obj_t *object;

    if (NULL == parent)
    {
        return NULL;
    }

    object = lv_obj_create(parent);
    if (NULL == object)
    {
        return NULL;
    }

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
    lv_anim_t tAttentionAnimation;
    lv_anim_exec_xcb_t pExecCallback;
    bool bAnimateOverlay;
    int32_t lFrom;
    int32_t lTo;
    uint32_t ulTime;
    uint16_t usRepeatCount;

    if ((NULL == g_pet_ui.stage) ||
        (NULL == g_pet_ui.attention_panel) ||
        (g_pet_ui.ucRenderedState == ucState))
    {
        return;
    }

    g_pet_ui.ucRenderedState = ucState;
    lv_anim_del(g_pet_ui.stage, NULL);
    lv_anim_del(g_pet_ui.attention_panel, NULL);
    lv_obj_set_pos(g_pet_ui.stage, PET_MASCOT_X, PET_MASCOT_Y);
    lv_obj_set_pos(
        g_pet_ui.attention_panel,
        PET_ATTENTION_PANEL_X,
        PET_ATTENTION_PANEL_Y);
    lv_obj_add_flag(g_pet_ui.attention_panel, LV_OBJ_FLAG_HIDDEN);
    bAnimateOverlay = true;
#if LV_USE_GIF
    if (NULL != g_pet_ui.mascot_gif)
    {
        /* Keep GIF playback as the only continuous redraw source. */
        bAnimateOverlay = false;
    }
#endif /* LV_USE_GIF */
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
        lv_label_set_text(g_pet_ui.attention_title, "ACTION NEEDED");
        lv_obj_set_style_bg_color(
            g_pet_ui.attention_panel,
            lv_color_hex(0x5C3A0DU),
            0);
        lv_obj_set_style_border_color(
            g_pet_ui.attention_panel,
            lv_color_hex(0xF6C75EU),
            0);
        lv_obj_set_style_text_color(
            g_pet_ui.attention_title,
            lv_color_hex(0xFFF4AAU),
            0);
        lv_obj_clear_flag(g_pet_ui.attention_panel, LV_OBJ_FLAG_HIDDEN);
    }
    else if (AGENTPET_STATE_COMPLETED == ucState)
    {
        lFrom = PET_MASCOT_Y + 4;
        lTo = PET_MASCOT_Y - 14;
        ulTime = 375U;
        usRepeatCount = 2U;
        lv_label_set_text(g_pet_ui.attention_title, "TASK COMPLETE");
        lv_obj_set_style_bg_color(
            g_pet_ui.attention_panel,
            lv_color_hex(0x123C2BU),
            0);
        lv_obj_set_style_border_color(
            g_pet_ui.attention_panel,
            lv_color_hex(0x65D69EU),
            0);
        lv_obj_set_style_text_color(
            g_pet_ui.attention_title,
            lv_color_hex(0x8FFFC2U),
            0);
        lv_obj_clear_flag(g_pet_ui.attention_panel, LV_OBJ_FLAG_HIDDEN);
    }
    else if (AGENTPET_STATE_ERROR == ucState)
    {
        pExecCallback = (lv_anim_exec_xcb_t)lv_obj_set_x;
        lFrom = PET_MASCOT_X - 3;
        lTo = PET_MASCOT_X + 4;
        ulTime = 110U;
    }

    if (bAnimateOverlay)
    {
        lv_anim_init(&tAnimation);
        lv_anim_set_var(&tAnimation, g_pet_ui.stage);
        lv_anim_set_values(&tAnimation, lFrom, lTo);
        lv_anim_set_exec_cb(&tAnimation, pExecCallback);
        lv_anim_set_time(&tAnimation, ulTime);
        lv_anim_set_playback_time(&tAnimation, ulTime);
        lv_anim_set_repeat_delay(&tAnimation, 100U);
        lv_anim_set_repeat_count(&tAnimation, usRepeatCount);
        lv_anim_start(&tAnimation);
    }

    if ((AGENTPET_STATE_NEEDS_INPUT == ucState) ||
        (AGENTPET_STATE_COMPLETED == ucState))
    {
        lv_obj_move_foreground(g_pet_ui.attention_panel);
        if (bAnimateOverlay)
        {
            lv_anim_init(&tAttentionAnimation);
            lv_anim_set_var(&tAttentionAnimation, g_pet_ui.attention_panel);
            lv_anim_set_values(
                &tAttentionAnimation,
                PET_ATTENTION_PANEL_Y + 3,
                PET_ATTENTION_PANEL_Y - 5);
            lv_anim_set_exec_cb(
                &tAttentionAnimation,
                (lv_anim_exec_xcb_t)lv_obj_set_y);
            lv_anim_set_time(&tAttentionAnimation, 520U);
            lv_anim_set_playback_time(&tAttentionAnimation, 520U);
            lv_anim_set_repeat_delay(&tAttentionAnimation, 120U);
            lv_anim_set_repeat_count(
                &tAttentionAnimation,
                LV_ANIM_REPEAT_INFINITE);
            lv_anim_start(&tAttentionAnimation);
        }
    }

    return;
}

/*
 * PET_QuestCurrentDay
 * 功能：返回任务花园使用的 UTC epoch day，RTC 未同步时返回 0。
 * 参数：无。
 * 返回值：UTC epoch day，0 表示日期未知。
 */
static uint32_t PET_QuestCurrentDay(void)
{
#ifndef BSP_USING_PC_SIMULATOR
    time_t tNow;

    tNow = time(NULL);
    if ((time_t)86400 > tNow)
    {
        return 0U;
    }

    return (uint32_t)(tNow / (time_t)86400);
#else
    return 0U;
#endif
}

#ifndef BSP_USING_PC_SIMULATOR
/*
 * PET_ReadQuestPreference
 * 功能：读取非负花园整数，负值和存储不可用时返回默认值。
 * 参数：
 *   - pKey: share_prefs 键名，仅输入
 *   - ulDefault: 无有效值时使用的默认值
 * 返回值：经过非负校验的 32 位值。
 */
static uint32_t PET_ReadQuestPreference(const char *pKey,
                                         uint32_t ulDefault)
{
    int32_t lValue;

    if ((NULL == g_pet_ui.pQuestPrefs) || (NULL == pKey) ||
        (INT32_MAX < ulDefault))
    {
        return ulDefault;
    }

    lValue = share_prefs_get_int(
        g_pet_ui.pQuestPrefs, pKey, (int32_t)ulDefault);
    if (0 > lValue)
    {
        return ulDefault;
    }

    return (uint32_t)lValue;
}

/*
 * PET_QuestPersistedInteger
 * 功能：把业务无符号计数安全限制到 share_prefs 的 int32_t 范围。
 * 参数：
 *   - ulValue: 待持久化计数
 * 返回值：范围为 0~INT32_MAX 的持久化整数。
 */
static int32_t PET_QuestPersistedInteger(uint32_t ulValue)
{
    if ((uint32_t)INT32_MAX < ulValue)
    {
        return INT32_MAX;
    }

    return (int32_t)ulValue;
}
#endif

/*
 * PET_LoadQuestGarden
 * 功能：从版本化产品存储恢复花园聚合状态并初始化短期去重槽。
 * 参数：无。
 * 返回值：无。
 */
static void PET_LoadQuestGarden(void)
{
    QUEST_GARDEN_PERSISTED tPersisted;
    const QUEST_GARDEN_PERSISTED *pPersisted;

    rt_memset(&tPersisted, 0, sizeof(tPersisted));
    pPersisted = NULL;
#ifndef BSP_USING_PC_SIMULATOR
    g_pet_ui.pQuestPrefs = share_prefs_open(
        PET_QUEST_PREF_NAME, SHAREPREFS_MODE_PRIVATE);
    if (NULL != g_pet_ui.pQuestPrefs)
    {
        tPersisted.ulVersion = PET_ReadQuestPreference(
            PET_QUEST_PREF_VERSION_KEY, 0U);
        tPersisted.ulDay = PET_ReadQuestPreference(
            PET_QUEST_PREF_DAY_KEY, 0U);
        tPersisted.ulTodayCompleted = PET_ReadQuestPreference(
            PET_QUEST_PREF_COMPLETED_KEY, 0U);
        tPersisted.ulTodayCollected = PET_ReadQuestPreference(
            PET_QUEST_PREF_COLLECTED_KEY, 0U);
        tPersisted.ulPending = PET_ReadQuestPreference(
            PET_QUEST_PREF_PENDING_KEY, 0U);
        tPersisted.ulStreak = PET_ReadQuestPreference(
            PET_QUEST_PREF_STREAK_KEY, 0U);
        tPersisted.ulOverflow = PET_ReadQuestPreference(
            PET_QUEST_PREF_OVERFLOW_KEY, 0U);
        pPersisted = &tPersisted;
    }
#endif
    if (!QUESTGARDEN_Init(&g_pet_ui.tQuestGarden, pPersisted))
    {
        rt_kprintf("agent pet: quest garden init failed\n");
    }

    return;
}

/*
 * PET_SaveQuestGarden
 * 功能：在离散业务事件后保存花园聚合状态，版本键最后提交。
 * 参数：无。
 * 返回值：无。
 */
static void PET_SaveQuestGarden(void)
{
#ifndef BSP_USING_PC_SIMULATOR
    QUEST_GARDEN_PERSISTED tPersisted;
    rt_err_t tResult;
    rt_err_t tWriteResult;

    if ((NULL == g_pet_ui.pQuestPrefs) ||
        !QUESTGARDEN_GetPersisted(&g_pet_ui.tQuestGarden, &tPersisted))
    {
        return;
    }

    tResult = share_prefs_set_int(
        g_pet_ui.pQuestPrefs, PET_QUEST_PREF_VERSION_KEY, 0);
    tWriteResult = share_prefs_set_int(
        g_pet_ui.pQuestPrefs, PET_QUEST_PREF_DAY_KEY,
        PET_QuestPersistedInteger(tPersisted.ulDay));
    if (RT_EOK != tWriteResult)
    {
        tResult = tWriteResult;
    }
    tWriteResult = share_prefs_set_int(
        g_pet_ui.pQuestPrefs, PET_QUEST_PREF_COMPLETED_KEY,
        PET_QuestPersistedInteger(tPersisted.ulTodayCompleted));
    if (RT_EOK != tWriteResult)
    {
        tResult = tWriteResult;
    }
    tWriteResult = share_prefs_set_int(
        g_pet_ui.pQuestPrefs, PET_QUEST_PREF_COLLECTED_KEY,
        PET_QuestPersistedInteger(tPersisted.ulTodayCollected));
    if (RT_EOK != tWriteResult)
    {
        tResult = tWriteResult;
    }
    tWriteResult = share_prefs_set_int(
        g_pet_ui.pQuestPrefs, PET_QUEST_PREF_PENDING_KEY,
        PET_QuestPersistedInteger(tPersisted.ulPending));
    if (RT_EOK != tWriteResult)
    {
        tResult = tWriteResult;
    }
    tWriteResult = share_prefs_set_int(
        g_pet_ui.pQuestPrefs, PET_QUEST_PREF_STREAK_KEY,
        PET_QuestPersistedInteger(tPersisted.ulStreak));
    if (RT_EOK != tWriteResult)
    {
        tResult = tWriteResult;
    }
    tWriteResult = share_prefs_set_int(
        g_pet_ui.pQuestPrefs, PET_QUEST_PREF_OVERFLOW_KEY,
        PET_QuestPersistedInteger(tPersisted.ulOverflow));
    if (RT_EOK != tWriteResult)
    {
        tResult = tWriteResult;
    }
    if (RT_EOK == tResult)
    {
        tResult = share_prefs_set_int(
            g_pet_ui.pQuestPrefs, PET_QUEST_PREF_VERSION_KEY,
            (int32_t)QUEST_GARDEN_PERSIST_VERSION);
    }
    if (RT_EOK != tResult)
    {
        rt_kprintf("agent pet: save quest garden failed %d\n", tResult);
    }
#endif

    return;
}

/*
 * PET_UpdateQuestGarden
 * 功能：根据纯 C 状态视图更新固定数量的花园 LVGL 对象。
 * 参数：无。
 * 返回值：无。
 */
static void PET_UpdateQuestGarden(void)
{
    QUEST_GARDEN_VIEW tView;
    uint8_t ucIndex;

    if (!QUESTGARDEN_GetView(&g_pet_ui.tQuestGarden, &tView))
    {
        return;
    }

    if (NULL != g_pet_ui.garden_label)
    {
        lv_label_set_text_fmt(
            g_pet_ui.garden_label,
            "Today %lu / 5   Seeds %lu   Streak %lu",
            (unsigned long)tView.ulTodayCollected,
            (unsigned long)tView.ulPending,
            (unsigned long)tView.ulStreak);
    }
    for (ucIndex = 0U; ucIndex < QUEST_GARDEN_MAX_LEAVES; ucIndex++)
    {
        if (NULL == g_pet_ui.aGardenLeaves[ucIndex])
        {
            continue;
        }
        if (tView.ucVisibleLeaves > ucIndex)
        {
            lv_obj_clear_flag(
                g_pet_ui.aGardenLeaves[ucIndex], LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(
                g_pet_ui.aGardenLeaves[ucIndex], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (NULL != g_pet_ui.garden_flower)
    {
        if (true == tView.bFlowerVisible)
        {
            lv_obj_clear_flag(g_pet_ui.garden_flower, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(g_pet_ui.garden_flower, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (NULL != g_pet_ui.seed_button)
    {
        if (0U != tView.ulPending)
        {
            lv_obj_clear_flag(g_pet_ui.seed_button, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(g_pet_ui.seed_button, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (NULL != g_pet_ui.seed_label)
    {
        lv_label_set_text_fmt(
            g_pet_ui.seed_label, "Collect seed (%lu)",
            (unsigned long)tView.ulPending);
    }

    return;
}

/*
 * PET_CollectQuestSeed
 * 功能：处理花园领取点击，仅更新本地状态和持久化。
 * 参数：
 *   - pEvent: LVGL 点击事件
 * 返回值：无。
 */
static void PET_CollectQuestSeed(lv_event_t *pEvent)
{
    QUEST_GARDEN_RESULT tResult;

    if ((NULL == pEvent) ||
        (LV_EVENT_SHORT_CLICKED != lv_event_get_code(pEvent)))
    {
        return;
    }
    if (!QUESTGARDEN_Collect(&g_pet_ui.tQuestGarden, &tResult))
    {
        return;
    }
    if (true == tResult.bChanged)
    {
        PET_UpdateQuestGarden();
        PET_SaveQuestGarden();
        if ((NULL != g_pet_ui.daily_summary) &&
            (NULL != g_pet_ui.daily_timer))
        {
            lv_label_set_text(g_pet_ui.daily_summary, "Seed collected");
            lv_obj_clear_flag(
                g_pet_ui.daily_summary, LV_OBJ_FLAG_HIDDEN);
            lv_timer_reset(g_pet_ui.daily_timer);
            lv_timer_resume(g_pet_ui.daily_timer);
        }
    }

    return;
}

/*
 * PET_CreateQuestGarden
 * 功能：使用 LVGL 基本对象创建固定容量任务花园，不加载外部素材。
 * 参数：无。
 * 返回值：无。
 */
static void PET_CreateQuestGarden(void)
{
    lv_coord_t tGardenX;
    lv_coord_t tGardenY;
    uint8_t ucIndex;
    static const lv_coord_t l_aLeafX[QUEST_GARDEN_MAX_LEAVES] =
        {8, 42, 4, 44, 25};
    static const lv_coord_t l_aLeafY[QUEST_GARDEN_MAX_LEAVES] =
        {33, 28, 18, 12, 2};

    if (NULL == g_pet_ui.root)
    {
        return;
    }

    tGardenX = LV_HOR_RES_MAX - PET_QUEST_GARDEN_WIDTH - 8;
    tGardenY = 63;
    g_pet_ui.garden_pot = pet_shape(
        g_pet_ui.root, tGardenX + 13, tGardenY + 45,
        50, 22, 0xA85D32U, 6);
    g_pet_ui.garden_stem = pet_shape(
        g_pet_ui.root, tGardenX + 36, tGardenY + 10,
        4, 42, 0x65D69EU, 2);
    for (ucIndex = 0U; ucIndex < QUEST_GARDEN_MAX_LEAVES; ucIndex++)
    {
        g_pet_ui.aGardenLeaves[ucIndex] = pet_shape(
            g_pet_ui.root,
            tGardenX + l_aLeafX[ucIndex],
            tGardenY + l_aLeafY[ucIndex],
            24, 12, 0x65D69EU, 8);
        if (NULL != g_pet_ui.aGardenLeaves[ucIndex])
        {
            lv_obj_add_flag(
                g_pet_ui.aGardenLeaves[ucIndex], LV_OBJ_FLAG_HIDDEN);
        }
    }
    g_pet_ui.garden_flower = pet_shape(
        g_pet_ui.root, tGardenX + 27, tGardenY - 5,
        22, 22, 0xFF8AAEU, 11);
    if (NULL != g_pet_ui.garden_flower)
    {
        lv_obj_add_flag(g_pet_ui.garden_flower, LV_OBJ_FLAG_HIDDEN);
    }

    g_pet_ui.garden_label = lv_label_create(g_pet_ui.root);
    if (NULL != g_pet_ui.garden_label)
    {
        lv_obj_set_width(g_pet_ui.garden_label, LV_HOR_RES_MAX - 16);
        lv_obj_set_pos(g_pet_ui.garden_label, 8, 34);
        lv_obj_set_style_text_align(
            g_pet_ui.garden_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(
            g_pet_ui.garden_label, lv_color_hex(0xB9EBCBU), 0);
    }

    g_pet_ui.seed_button = lv_btn_create(g_pet_ui.root);
    if (NULL != g_pet_ui.seed_button)
    {
        lv_obj_set_pos(g_pet_ui.seed_button, 8, 64);
        lv_obj_set_size(g_pet_ui.seed_button, 122, 38);
        lv_obj_set_style_bg_color(
            g_pet_ui.seed_button, lv_color_hex(0x287A55U), 0);
        lv_obj_add_event_cb(
            g_pet_ui.seed_button, PET_CollectQuestSeed,
            LV_EVENT_SHORT_CLICKED, NULL);
        lv_obj_add_flag(g_pet_ui.seed_button, LV_OBJ_FLAG_HIDDEN);
        g_pet_ui.seed_label = lv_label_create(g_pet_ui.seed_button);
        if (NULL != g_pet_ui.seed_label)
        {
            lv_obj_center(g_pet_ui.seed_label);
        }
    }

    return;
}

/*
 * PET_LoadMerit
 * Function: Restore today's merit count from product storage on hardware.
 */
static void PET_LoadMerit(void)
{
    AGENTPET_MERIT_SNAPSHOT tMeritSnapshot;

    g_pet_ui.ulMeritCount = 0U;
    AGENTPETMERIT_Init();
    if (AGENTPETMERIT_GetSnapshot(&tMeritSnapshot))
    {
        g_pet_ui.ulMeritCount = tMeritSnapshot.ulCount;
        g_pet_ui.ulRenderedMeritGeneration = tMeritSnapshot.ulGeneration;
    }

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
 * Function: Animate the wooden-fish mallet using the native image angle API.
 */
static void PET_SetMalletAngle(void *pObject, int32_t lAngle)
{
    if (NULL != pObject)
    {
        lv_img_set_angle((lv_obj_t *)pObject, (int16_t)lAngle);
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

    return;
}

/*
 * PET_PlayWoodenFish
 * Function: Replace the mascot with a repeatable wooden-fish interaction.
 */
static void PET_PlayWoodenFishAnimation(const lv_point_t *pPoint)
{
    lv_anim_t tAnimation;
    lv_coord_t tFishX;
    lv_coord_t tFishY;
    uint32_t ulInterval;
    uint32_t ulAnimationTime;

    if (NULL == g_pet_ui.wooden_timer)
    {
        return;
    }

    if (RT_EOK != LOCALMUSIC_PlayEffect(PET_WOODEN_FISH_SOUND_PATH))
    {
        rt_kprintf("pet: wooden-fish sound queue failed\n");
    }

    if (NULL != pPoint)
    {
        tFishX = pPoint->x - (PET_WOODEN_FISH_WIDTH / 2);
        tFishY = pPoint->y - (PET_WOODEN_FISH_HEIGHT / 2);
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
    ulInterval = (0U == g_pet_ui.ulLastHitTick) ?
        0xFFFFFFFFUL : lv_tick_elaps(g_pet_ui.ulLastHitTick);
    g_pet_ui.ulLastHitTick = lv_tick_get();
    g_pet_ui.ulMeritCount = AGENTPETMERIT_Increment();
    AGENTPETBLE_NotifyMerit();
    if (NULL != g_pet_ui.daily_summary)
    {
        lv_label_set_text_fmt(
            g_pet_ui.daily_summary,
            "Today's merit  %lu",
            (unsigned long)g_pet_ui.ulMeritCount);
        lv_obj_clear_flag(g_pet_ui.daily_summary, LV_OBJ_FLAG_HIDDEN);
    }

    if (PET_TURBO_INTERVAL_MS >= ulInterval)
    {
        ulAnimationTime = 140U;
    }
    else if (PET_FAST_INTERVAL_MS >= ulInterval)
    {
        ulAnimationTime = 190U;
    }
    else if (PET_QUICK_INTERVAL_MS >= ulInterval)
    {
        ulAnimationTime = 260U;
    }
    else
    {
        ulAnimationTime = 330U;
    }

    lv_obj_add_flag(g_pet_ui.stage, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(g_pet_ui.wooden_fish, LV_OBJ_FLAG_HIDDEN);

    lv_anim_del(g_pet_ui.mallet, NULL);
    lv_img_set_angle(g_pet_ui.mallet, 140);
    lv_anim_init(&tAnimation);
    lv_anim_set_var(&tAnimation, g_pet_ui.mallet);
    lv_anim_set_values(&tAnimation, 140, 50);
    lv_anim_set_exec_cb(&tAnimation, PET_SetMalletAngle);
    lv_anim_set_time(&tAnimation, ulAnimationTime);
    lv_anim_set_playback_time(&tAnimation, 120U);
    lv_anim_start(&tAnimation);

    lv_anim_del(g_pet_ui.fish_body, NULL);
    lv_obj_set_y(g_pet_ui.fish_body, 50);
    lv_anim_init(&tAnimation);
    lv_anim_set_var(&tAnimation, g_pet_ui.fish_body);
    lv_anim_set_values(&tAnimation, 50, 43);
    lv_anim_set_exec_cb(&tAnimation, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_set_time(&tAnimation, ulAnimationTime);
    lv_anim_set_playback_time(&tAnimation, 120U);
    lv_anim_start(&tAnimation);

    lv_anim_del(g_pet_ui.merit_image, NULL);
    lv_obj_set_y(g_pet_ui.merit_image, 18);
    lv_obj_set_style_opa(g_pet_ui.merit_image, LV_OPA_COVER, 0);
    lv_anim_init(&tAnimation);
    lv_anim_set_var(&tAnimation, g_pet_ui.merit_image);
    lv_anim_set_values(&tAnimation, 18, -10);
    lv_anim_set_exec_cb(&tAnimation, (lv_anim_exec_xcb_t)lv_obj_set_y);
    lv_anim_set_time(&tAnimation, 1400U);
    lv_anim_start(&tAnimation);

    lv_timer_set_period(g_pet_ui.wooden_timer, PET_WOODEN_FISH_IDLE_MS);
    lv_timer_reset(g_pet_ui.wooden_timer);
    lv_timer_resume(g_pet_ui.wooden_timer);

    return;
}

/*
 * PET_PlayWoodenFish
 * Function: Convert a local short click into the shared wooden-fish animation.
 */
static void PET_PlayWoodenFish(lv_event_t *pEvent)
{
    lv_indev_t *pInput;
    lv_point_t tPoint;

    if ((NULL == pEvent) ||
        (LV_EVENT_SHORT_CLICKED != lv_event_get_code(pEvent)))
    {
        return;
    }

    pInput = lv_indev_get_act();
    if (NULL == pInput)
    {
        PET_PlayWoodenFishAnimation(NULL);
        return;
    }

    lv_indev_get_point(pInput, &tPoint);
    PET_PlayWoodenFishAnimation(&tPoint);

    return;
}

/*
 * PET_CreateWoodenFish
 * Function: Build independent image layers exported from the desktop CSS.
 */
static void PET_CreateWoodenFish(void)
{
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

    g_pet_ui.fish_body = lv_img_create(g_pet_ui.wooden_fish);
    lv_img_set_src(g_pet_ui.fish_body, &agent_pet_wooden_fish);
    lv_obj_set_pos(g_pet_ui.fish_body, 20, 50);

    g_pet_ui.mallet = lv_img_create(g_pet_ui.wooden_fish);
    lv_img_set_src(g_pet_ui.mallet, &agent_pet_wooden_fish_mallet);
    lv_obj_set_pos(
        g_pet_ui.mallet,
        PET_WOODEN_FISH_MALLET_X,
        PET_WOODEN_FISH_MALLET_Y);
    lv_img_set_pivot(g_pet_ui.mallet, 132, 50);
    lv_img_set_angle(g_pet_ui.mallet, 140);

    g_pet_ui.merit_image = lv_img_create(g_pet_ui.wooden_fish);
    lv_img_set_src(g_pet_ui.merit_image, &agent_pet_merit_plus_one);
    lv_obj_set_pos(g_pet_ui.merit_image, 15, 0);

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
    lv_obj_set_width(name, 120);
    lv_obj_set_pos(name, 55, 8);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(name, lv_color_hex(0xd8f7ee), 0);
    lv_obj_set_style_text_letter_space(name, 1, 0);
    lv_obj_set_style_text_opa(name, LV_OPA_COVER, 0);

    floor = pet_shape(g_pet_ui.root, 40,
                      PET_MASCOT_Y + PET_MASCOT_SIZE - 18,
                      LV_HOR_RES_MAX - 80, 20, 0x183942, 20);
    if (NULL != floor)
    {
        lv_obj_set_style_bg_opa(floor, LV_OPA_60, 0);
    }

    g_pet_ui.stage = lv_obj_create(g_pet_ui.root);
    lv_obj_set_pos(g_pet_ui.stage, PET_MASCOT_X, PET_MASCOT_Y);
    lv_obj_set_size(g_pet_ui.stage, PET_MASCOT_SIZE, PET_MASCOT_SIZE);
    lv_obj_set_style_bg_opa(g_pet_ui.stage, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_pet_ui.stage, 0, 0);
    lv_obj_set_style_pad_all(g_pet_ui.stage, 0, 0);
    lv_obj_clear_flag(g_pet_ui.stage, LV_OBJ_FLAG_SCROLLABLE);

    g_pet_ui.mascot = lv_img_create(g_pet_ui.stage);
    lv_img_set_src(g_pet_ui.mascot, &agent_pet_mascot);
    lv_img_set_zoom(g_pet_ui.mascot, PET_MascotZoom(&agent_pet_mascot.header));
    lv_img_set_antialias(g_pet_ui.mascot, false);
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
    lv_obj_set_width(g_pet_ui.daily_summary, 280);
    lv_obj_set_pos(g_pet_ui.daily_summary,
                   (LV_HOR_RES_MAX - 280) / 2, 48);
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
    lv_obj_set_style_text_letter_space(g_pet_ui.daily_summary, 1, 0);
    lv_obj_set_style_text_opa(g_pet_ui.daily_summary, LV_OPA_COVER, 0);
    lv_label_set_text(g_pet_ui.daily_summary, "Today's merit  0");
    lv_obj_add_flag(g_pet_ui.daily_summary, LV_OBJ_FLAG_HIDDEN);

    PET_CreateAttentionCue();
    PET_CreateQuestGarden();

    g_pet_ui.status_label = lv_label_create(g_pet_ui.root);
    lv_obj_set_width(g_pet_ui.status_label, LV_HOR_RES_MAX - 24);
    lv_obj_set_pos(g_pet_ui.status_label, 12, LV_VER_RES_MAX - 72);
    lv_obj_set_style_text_align(g_pet_ui.status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_ext_set_local_font(
        g_pet_ui.status_label,
        FONT_NORMAL,
        lv_color_hex(0xA7B0B5U));
    lv_obj_set_style_text_letter_space(g_pet_ui.status_label, 1, 0);
    lv_obj_set_style_text_opa(g_pet_ui.status_label, LV_OPA_COVER, 0);

    g_pet_ui.task_label = lv_label_create(g_pet_ui.root);
    lv_obj_set_width(g_pet_ui.task_label, LV_HOR_RES_MAX - 24);
    lv_obj_set_pos(g_pet_ui.task_label, 12, LV_VER_RES_MAX - 40);
    lv_obj_set_style_text_align(g_pet_ui.task_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_ext_set_local_font(
        g_pet_ui.task_label,
        FONT_NORMAL,
        lv_color_hex(0xD8F7EEU));
    lv_obj_set_style_text_letter_space(g_pet_ui.task_label, 1, 0);
    lv_obj_set_style_text_opa(g_pet_ui.task_label, LV_OPA_COVER, 0);

    g_pet_ui.image_progress_panel = lv_obj_create(g_pet_ui.root);
    lv_obj_set_size(g_pet_ui.image_progress_panel, 320, 100);
    lv_obj_align(g_pet_ui.image_progress_panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(
        g_pet_ui.image_progress_panel, lv_color_hex(0x10232BU), 0);
    lv_obj_set_style_bg_opa(g_pet_ui.image_progress_panel, LV_OPA_90, 0);
    lv_obj_set_style_border_width(g_pet_ui.image_progress_panel, 2, 0);
    lv_obj_set_style_border_color(
        g_pet_ui.image_progress_panel, lv_color_hex(0x7CC8FFU), 0);
    lv_obj_set_style_radius(g_pet_ui.image_progress_panel, 18, 0);
    lv_obj_set_style_pad_all(g_pet_ui.image_progress_panel, 14, 0);
    lv_obj_clear_flag(g_pet_ui.image_progress_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(g_pet_ui.image_progress_panel, LV_OBJ_FLAG_HIDDEN);

    g_pet_ui.image_progress_label = lv_label_create(
        g_pet_ui.image_progress_panel);
    lv_obj_set_width(g_pet_ui.image_progress_label, LV_PCT(100));
    lv_label_set_text(g_pet_ui.image_progress_label, "Receiving image  0%");
    lv_obj_set_style_text_align(
        g_pet_ui.image_progress_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(
        g_pet_ui.image_progress_label, lv_color_hex(0xD8F7EEU), 0);
    lv_obj_set_style_text_letter_space(g_pet_ui.image_progress_label, 1, 0);
    lv_obj_set_style_text_opa(g_pet_ui.image_progress_label, LV_OPA_COVER, 0);
    lv_obj_align(g_pet_ui.image_progress_label, LV_ALIGN_TOP_MID, 0, 0);

    g_pet_ui.image_progress_bar = lv_bar_create(
        g_pet_ui.image_progress_panel);
    lv_obj_set_size(g_pet_ui.image_progress_bar, LV_PCT(100), 16);
    lv_obj_align(g_pet_ui.image_progress_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_bar_set_range(g_pet_ui.image_progress_bar, 0, 100);
    lv_bar_set_value(g_pet_ui.image_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(
        g_pet_ui.image_progress_bar, lv_color_hex(0x183942U),
        LV_PART_MAIN);
    lv_obj_set_style_bg_color(
        g_pet_ui.image_progress_bar, lv_color_hex(0x65D69EU),
        LV_PART_INDICATOR);
    lv_obj_set_style_radius(
        g_pet_ui.image_progress_bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(
        g_pet_ui.image_progress_bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);

    PET_CreateMotionSwitch();

    PET_LoadMerit();
    PET_LoadQuestGarden();
    PET_UpdateQuestGarden();
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
    g_pet_ui.ulRenderedImageGeneration = 0xFFFFFFFFUL;
    g_pet_ui.ucRenderedImageProgress = 0xFFU;
    g_pet_ui.eRenderedImageState = AGENTPET_IMAGE_IDLE;
    g_pet_ui.bRenderedConnected = false;
    g_pet_ui.bRenderedCustomImage = false;
    g_pet_ui.status_timer = lv_timer_create(
        PET_RefreshStatus,
        PET_STATUS_REFRESH_MS,
        NULL);
    PET_ApplyStateAnimation(AGENTPET_STATE_IDLE);
    PET_RefreshStatus(g_pet_ui.status_timer);
}

static void pet_on_stop(void)
{
#if defined(AGENT_PET_USING_IMU) && !defined(BSP_USING_PC_SIMULATOR)
    PET_StopMotionDetection();
#endif
    if (g_pet_ui.motion_timer)
    {
        lv_timer_del(g_pet_ui.motion_timer);
        g_pet_ui.motion_timer = NULL;
    }
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
    if (NULL != g_pet_ui.pQuestPrefs)
    {
        rt_err_t tQuestCloseResult;

        PET_SaveQuestGarden();
        tQuestCloseResult = share_prefs_close(g_pet_ui.pQuestPrefs);
        if (RT_EOK != tQuestCloseResult)
        {
            rt_kprintf(
                "agent pet: close quest storage failed %d\n",
                tQuestCloseResult);
        }
        g_pet_ui.pQuestPrefs = NULL;
    }
#endif
    AGENTPETMERIT_Save();
    if (g_pet_ui.root)
    {
#if LV_USE_GIF
        PET_ReleaseCustomGif();
#endif
        if (NULL != g_pet_ui.mascot)
        {
            lv_img_set_src(g_pet_ui.mascot, &agent_pet_mascot);
        }
        PET_ReleaseCustomMascot();
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
