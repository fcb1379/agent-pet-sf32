#include <rtthread.h>
#include <stdint.h>
#include <time.h>

#include "littlevgl2rtt.h"
#include "lv_ext_resource_manager.h"
#include "gui_app_fwk.h"
#include "noise_monitor.h"

#define APP_ID                                  "noise"
#define NOISEAPP_REFRESH_MS                     (1000U)
#define NOISEAPP_REFERENCE_WIDTH                (410U)
#define NOISEAPP_REFERENCE_HEIGHT               (502U)
#define NOISEAPP_WAVE_WIDTH                     (357U)
#define NOISEAPP_WAVE_HEIGHT                    (124U)
#define NOISEAPP_WAVE_MIN_DB                    (30U)
#define NOISEAPP_WAVE_MAX_DB                    (120U)
#define NOISEAPP_LOUD_THRESHOLD_DB              (80U)
#define NOISEAPP_MODE_IDLE                      (0U)
#define NOISEAPP_MODE_NORMAL                    (1U)
#define NOISEAPP_MODE_LOUD                      (2U)
#define NOISEAPP_MODE_INVALID                   (0xFFU)
#define NOISEAPP_SCALE_Y(y)                     \
    ((lv_coord_t)(((uint32_t)(y) * LV_VER_RES_MAX) / NOISEAPP_REFERENCE_HEIGHT))

LV_IMG_DECLARE(noise_bg_idle);
LV_IMG_DECLARE(noise_bg_normal);
LV_IMG_DECLARE(noise_bg_loud);
LV_IMG_DECLARE(noise_wave_idle);
LV_IMG_DECLARE(noise_wave_normal_10);
LV_IMG_DECLARE(noise_wave_loud_10);
LV_IMG_DECLARE(noise_status_normal);
LV_IMG_DECLARE(noise_status_loud);
LV_IMG_DECLARE(noise_close);

/* NOISE_APP_UI: 噪声检测页面对象集合。
 * 成员说明：
 *   - pRoot: 应用根对象。
 *   - pMainPage: 分贝测量主页面。
 *   - pInfoPage: 听力风险说明页面。
 *   - pBackground: 随测量等级切换的原始切图背景。
 *   - pWaveClip: 按分贝值裁剪彩色波形的容器。
 *   - pActiveWave: 按噪声等级切换的彩色波形图。
 *   - pStatusIcon: 正常或强状态切图。
 *   - pStatusLabel: 当前噪声等级文字。
 *   - pValueLabel: 当前整数分贝值。
 *   - pUnitLabel: 分贝单位文字。
 *   - pTimeLabel: 当前时分标签。
 *   - pRefreshTimer: 一秒刷新定时器。
 *   - ucMode: 当前页面颜色模式。
 */
typedef struct _NOISE_APP_UI
{
    lv_obj_t *pRoot;
    lv_obj_t *pMainPage;
    lv_obj_t *pInfoPage;
    lv_obj_t *pBackground;
    lv_obj_t *pWaveClip;
    lv_obj_t *pActiveWave;
    lv_obj_t *pStatusIcon;
    lv_obj_t *pStatusLabel;
    lv_obj_t *pValueLabel;
    lv_obj_t *pUnitLabel;
    lv_obj_t *pTimeLabel;
    lv_timer_t *pRefreshTimer;
    uint8_t ucMode;
} NOISE_APP_UI;

/* l_tNoiseUi: 噪声应用唯一 UI 上下文，仅 LVGL 线程访问，页面退出后整体清零。 */
static NOISE_APP_UI l_tNoiseUi;
/***************************
 * NoiseApp_PrepareContainer: 初始化无边框、无滚动的透明页面容器。
 * 参数：
 *   - pContainer: 待设置的 LVGL 对象，不得为 NULL。
 * 返回值：无。
 ***************************/
static void NoiseApp_PrepareContainer(lv_obj_t *pContainer)
{
    if (NULL == pContainer)
    {
        return;
    }

    lv_obj_set_size(pContainer, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_set_pos(pContainer, 0, 0);
    lv_obj_set_style_bg_opa(pContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pContainer, 0, 0);
    lv_obj_set_style_pad_all(pContainer, 0, 0);
    lv_obj_clear_flag(pContainer, LV_OBJ_FLAG_SCROLLABLE);

    return;
}

/***************************
 * NoiseApp_RefreshTime: 更新右上角时分显示。
 * 参数：无。
 * 返回值：无。
 ***************************/
static void NoiseApp_RefreshTime(void)
{
    time_t tNow;
    struct tm *pLocalTime;
    char aTimeText[8];

    if (NULL == l_tNoiseUi.pTimeLabel)
    {
        return;
    }

    tNow = time(NULL);
    pLocalTime = localtime(&tNow);
    if (NULL == pLocalTime)
    {
        lv_label_set_text(l_tNoiseUi.pTimeLabel, "--:--");
        return;
    }

    (void)rt_snprintf(
        aTimeText,
        sizeof(aTimeText),
        "%02d:%02d",
        pLocalTime->tm_hour,
        pLocalTime->tm_min);
    lv_label_set_text(l_tNoiseUi.pTimeLabel, aTimeText);

    return;
}

/***************************
 * NoiseApp_SetMode: 切换空闲、正常或强噪声的背景和状态切图。
 * 参数：
 *   - ucMode: NOISEAPP_MODE_IDLE/NORMAL/LOUD。
 * 返回值：无。
 ***************************/
static void NoiseApp_SetMode(uint8_t ucMode)
{
    if ((NULL == l_tNoiseUi.pBackground) ||
            (ucMode == l_tNoiseUi.ucMode))
    {
        return;
    }

    l_tNoiseUi.ucMode = ucMode;
    if (NOISEAPP_MODE_LOUD == ucMode)
    {
        lv_img_set_src(l_tNoiseUi.pBackground, LV_EXT_IMG_GET(noise_bg_loud));
        lv_img_set_src(l_tNoiseUi.pStatusIcon, LV_EXT_IMG_GET(noise_status_loud));
        lv_img_set_src(l_tNoiseUi.pActiveWave, LV_EXT_IMG_GET(noise_wave_loud_10));
        lv_label_set_text(l_tNoiseUi.pStatusLabel, "强");
        lv_obj_set_style_text_color(
            l_tNoiseUi.pStatusLabel,
            lv_color_hex(0xFFDA16),
            0);
    }
    else if (NOISEAPP_MODE_NORMAL == ucMode)
    {
        lv_img_set_src(l_tNoiseUi.pBackground, LV_EXT_IMG_GET(noise_bg_normal));
        lv_img_set_src(l_tNoiseUi.pStatusIcon, LV_EXT_IMG_GET(noise_status_normal));
        lv_img_set_src(l_tNoiseUi.pActiveWave, LV_EXT_IMG_GET(noise_wave_normal_10));
        lv_label_set_text(l_tNoiseUi.pStatusLabel, "正常");
        lv_obj_set_style_text_color(
            l_tNoiseUi.pStatusLabel,
            lv_color_hex(0x00DA47),
            0);
    }
    else
    {
        lv_img_set_src(l_tNoiseUi.pBackground, LV_EXT_IMG_GET(noise_bg_idle));
        lv_obj_add_flag(l_tNoiseUi.pStatusIcon, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(l_tNoiseUi.pStatusLabel, "");
    }

    if (NOISEAPP_MODE_IDLE != ucMode)
    {
        lv_obj_clear_flag(l_tNoiseUi.pStatusIcon, LV_OBJ_FLAG_HIDDEN);
    }

    return;
}

/***************************
 * NoiseApp_RefreshMeasurement: 从采集服务读取快照并刷新页面。
 * 参数：无。
 * 返回值：无。
 ***************************/
static void NoiseApp_RefreshMeasurement(void)
{
    NOISE_MONITOR_SNAPSHOT tSnapshot;
    rt_err_t tResult;
    uint8_t ucMode;
    uint32_t ulClipWidth;
    char aValueText[8];

    NoiseApp_RefreshTime();
    tResult = NOISEMONITOR_GetSnapshot(&tSnapshot);
    if ((RT_EOK != tResult) || (false == tSnapshot.bValid))
    {
        NoiseApp_SetMode(NOISEAPP_MODE_IDLE);
        lv_label_set_text(l_tNoiseUi.pValueLabel, "--");
        lv_obj_add_flag(l_tNoiseUi.pWaveClip, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    ucMode = (NOISEAPP_LOUD_THRESHOLD_DB <= tSnapshot.ucDb) ?
        NOISEAPP_MODE_LOUD : NOISEAPP_MODE_NORMAL;
    NoiseApp_SetMode(ucMode);
    (void)rt_snprintf(
        aValueText,
        sizeof(aValueText),
        "%u",
        (unsigned int)tSnapshot.ucDb);
    lv_label_set_text(l_tNoiseUi.pValueLabel, aValueText);

    if (NOISEAPP_WAVE_MIN_DB >= tSnapshot.ucDb)
    {
        ulClipWidth = 1U;
    }
    else
    {
        ulClipWidth =
            ((uint32_t)(tSnapshot.ucDb - NOISEAPP_WAVE_MIN_DB) *
             NOISEAPP_WAVE_WIDTH) /
            (NOISEAPP_WAVE_MAX_DB - NOISEAPP_WAVE_MIN_DB);
        if (NOISEAPP_WAVE_WIDTH < ulClipWidth)
        {
            ulClipWidth = NOISEAPP_WAVE_WIDTH;
        }
    }
    lv_obj_set_width(l_tNoiseUi.pWaveClip, (lv_coord_t)ulClipWidth);
    lv_obj_clear_flag(l_tNoiseUi.pWaveClip, LV_OBJ_FLAG_HIDDEN);

    return;
}

/***************************
 * NoiseApp_InfoEvent: 打开听力风险说明页。
 * 参数：
 *   - pEvent: LVGL 事件对象。
 * 返回值：无。
 ***************************/
static void NoiseApp_InfoEvent(lv_event_t *pEvent)
{
    if ((NULL != pEvent) &&
            (LV_EVENT_SHORT_CLICKED == lv_event_get_code(pEvent)))
    {
        lv_obj_add_flag(l_tNoiseUi.pMainPage, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(l_tNoiseUi.pInfoPage, LV_OBJ_FLAG_HIDDEN);
    }

    return;
}

/***************************
 * NoiseApp_CloseInfoEvent: 关闭听力风险说明页并返回测量页。
 * 参数：
 *   - pEvent: LVGL 事件对象。
 * 返回值：无。
 ***************************/
static void NoiseApp_CloseInfoEvent(lv_event_t *pEvent)
{
    if ((NULL != pEvent) &&
            (LV_EVENT_SHORT_CLICKED == lv_event_get_code(pEvent)))
    {
        lv_obj_add_flag(l_tNoiseUi.pInfoPage, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(l_tNoiseUi.pMainPage, LV_OBJ_FLAG_HIDDEN);
        NoiseApp_RefreshMeasurement();
    }

    return;
}

/***************************
 * NoiseApp_CreateInfoButton: 创建测量页左上角信息按钮。
 * 参数：
 *   - pParent: 父对象，不得为 NULL。
 * 返回值：无。
 ***************************/
static void NoiseApp_CreateInfoButton(lv_obj_t *pParent)
{
    lv_obj_t *pButton;
    lv_obj_t *pLabel;

    if (NULL == pParent)
    {
        return;
    }

    pButton = lv_obj_create(pParent);
    if (NULL == pButton)
    {
        return;
    }
    lv_obj_set_size(pButton, 60, 60);
    lv_obj_set_pos(pButton, 25, NOISEAPP_SCALE_Y(26U));
    lv_obj_set_style_radius(pButton, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(pButton, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(pButton, LV_OPA_20, 0);
    lv_obj_set_style_border_width(pButton, 0, 0);
    lv_obj_set_style_pad_all(pButton, 0, 0);
    lv_obj_clear_flag(pButton, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(pButton, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        pButton,
        NoiseApp_InfoEvent,
        LV_EVENT_SHORT_CLICKED,
        NULL);

    pLabel = lv_label_create(pButton);
    if (NULL != pLabel)
    {
        lv_label_set_text(pLabel, "i");
        lv_obj_set_style_text_color(pLabel, lv_color_white(), 0);
        lv_obj_center(pLabel);
    }

    return;
}

/***************************
 * NoiseApp_CreateInfoPage: 按流程图创建听力风险说明页。
 * 参数：
 *   - pParent: 父对象，不得为 NULL。
 * 返回值：创建成功返回页面对象，失败返回 NULL。
 ***************************/
static lv_obj_t *NoiseApp_CreateInfoPage(lv_obj_t *pParent)
{
    lv_obj_t *pPage;
    lv_obj_t *pClose;
    lv_obj_t *pTitle;
    lv_obj_t *pNormalIcon;
    lv_obj_t *pNormalText;
    lv_obj_t *pLoudIcon;
    lv_obj_t *pLoudText;

    if (NULL == pParent)
    {
        return NULL;
    }

    pPage = lv_obj_create(pParent);
    if (NULL == pPage)
    {
        return NULL;
    }
    NoiseApp_PrepareContainer(pPage);
    lv_obj_set_style_bg_color(pPage, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(pPage, LV_OPA_COVER, 0);

    pClose = lv_img_create(pPage);
    if (NULL != pClose)
    {
        lv_img_set_src(pClose, LV_EXT_IMG_GET(noise_close));
        lv_obj_set_pos(pClose, 4, 4);
        lv_obj_add_flag(pClose, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(
            pClose,
            NoiseApp_CloseInfoEvent,
            LV_EVENT_SHORT_CLICKED,
            NULL);
    }

    pTitle = lv_label_create(pPage);
    if (NULL != pTitle)
    {
        lv_label_set_text(pTitle, "音量");
        lv_obj_set_style_text_color(pTitle, lv_color_white(), 0);
        lv_obj_align(pTitle, LV_ALIGN_TOP_MID, 0, 22);
    }

    pNormalIcon = lv_img_create(pPage);
    if (NULL != pNormalIcon)
    {
        lv_img_set_src(pNormalIcon, LV_EXT_IMG_GET(noise_status_normal));
        lv_obj_set_pos(pNormalIcon, 24, 112);
    }
    pNormalText = lv_label_create(pPage);
    if (NULL != pNormalText)
    {
        lv_label_set_text(
            pNormalText,
            "正常\n暴露于该级别的声音不会影响听力。");
        lv_label_set_long_mode(pNormalText, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(pNormalText, LV_HOR_RES_MAX - 92);
        lv_obj_set_pos(pNormalText, 72, 108);
        lv_obj_set_style_text_color(pNormalText, lv_color_white(), 0);
    }

    pLoudIcon = lv_img_create(pPage);
    if (NULL != pLoudIcon)
    {
        lv_img_set_src(pLoudIcon, LV_EXT_IMG_GET(noise_status_loud));
        lv_obj_set_pos(pLoudIcon, 24, 252);
    }
    pLoudText = lv_label_create(pPage);
    if (NULL != pLoudText)
    {
        lv_label_set_text(
            pLoudText,
            "强\n长时间暴露在这个音量下可能会导致听力受损。");
        lv_label_set_long_mode(pLoudText, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(pLoudText, LV_HOR_RES_MAX - 92);
        lv_obj_set_pos(pLoudText, 72, 248);
        lv_obj_set_style_text_color(pLoudText, lv_color_white(), 0);
    }

    lv_obj_add_flag(pPage, LV_OBJ_FLAG_HIDDEN);
    return pPage;
}

/***************************
 * NoiseApp_CreateMainPage: 使用噪声切图创建实时测量主页面。
 * 参数：
 *   - pParent: 父对象，不得为 NULL。
 * 返回值：创建成功返回页面对象，失败返回 NULL。
 ***************************/
static lv_obj_t *NoiseApp_CreateMainPage(lv_obj_t *pParent)
{
    lv_obj_t *pPage;
    lv_obj_t *pIdleWave;
    uint32_t ulZoomWidth;
    uint32_t ulZoomHeight;
    uint32_t ulZoom;
    lv_coord_t tWaveX;
    lv_coord_t tWaveY;

    if (NULL == pParent)
    {
        return NULL;
    }

    pPage = lv_obj_create(pParent);
    if (NULL == pPage)
    {
        return NULL;
    }
    NoiseApp_PrepareContainer(pPage);

    l_tNoiseUi.pBackground = lv_img_create(pPage);
    if (NULL == l_tNoiseUi.pBackground)
    {
        lv_obj_del(pPage);
        return NULL;
    }
    lv_img_set_src(l_tNoiseUi.pBackground, LV_EXT_IMG_GET(noise_bg_idle));
    ulZoomWidth = ((uint32_t)LV_HOR_RES_MAX * LV_IMG_ZOOM_NONE) /
                  NOISEAPP_REFERENCE_WIDTH;
    ulZoomHeight = ((uint32_t)LV_VER_RES_MAX * LV_IMG_ZOOM_NONE) /
                   NOISEAPP_REFERENCE_HEIGHT;
    ulZoom = LV_MAX(ulZoomWidth, ulZoomHeight);
    lv_img_set_zoom(l_tNoiseUi.pBackground, (uint16_t)ulZoom);
    lv_obj_center(l_tNoiseUi.pBackground);

    NoiseApp_CreateInfoButton(pPage);
    l_tNoiseUi.pTimeLabel = lv_label_create(pPage);
    if (NULL != l_tNoiseUi.pTimeLabel)
    {
        lv_label_set_text(l_tNoiseUi.pTimeLabel, "--:--");
        lv_ext_set_local_font(
            l_tNoiseUi.pTimeLabel,
            FONT_BIGL,
            lv_color_white());
        lv_obj_align(l_tNoiseUi.pTimeLabel, LV_ALIGN_TOP_RIGHT, -24, 22);
    }

    tWaveX = (LV_HOR_RES_MAX - NOISEAPP_WAVE_WIDTH) / 2;
    tWaveY = NOISEAPP_SCALE_Y(151U);
    pIdleWave = lv_img_create(pPage);
    if (NULL != pIdleWave)
    {
        lv_img_set_src(pIdleWave, LV_EXT_IMG_GET(noise_wave_idle));
        lv_obj_set_pos(pIdleWave, tWaveX, tWaveY);
    }

    l_tNoiseUi.pWaveClip = lv_obj_create(pPage);
    if (NULL != l_tNoiseUi.pWaveClip)
    {
        lv_obj_set_pos(l_tNoiseUi.pWaveClip, tWaveX, tWaveY);
        lv_obj_set_size(
            l_tNoiseUi.pWaveClip,
            1,
            NOISEAPP_WAVE_HEIGHT);
        lv_obj_set_style_bg_opa(l_tNoiseUi.pWaveClip, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(l_tNoiseUi.pWaveClip, 0, 0);
        lv_obj_set_style_pad_all(l_tNoiseUi.pWaveClip, 0, 0);
        lv_obj_clear_flag(l_tNoiseUi.pWaveClip, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(l_tNoiseUi.pWaveClip, LV_OBJ_FLAG_HIDDEN);
        l_tNoiseUi.pActiveWave = lv_img_create(l_tNoiseUi.pWaveClip);
        if (NULL != l_tNoiseUi.pActiveWave)
        {
            lv_img_set_src(
                l_tNoiseUi.pActiveWave,
                LV_EXT_IMG_GET(noise_wave_normal_10));
            lv_obj_set_pos(l_tNoiseUi.pActiveWave, 0, 0);
        }
    }

    l_tNoiseUi.pStatusIcon = lv_img_create(pPage);
    if (NULL != l_tNoiseUi.pStatusIcon)
    {
        lv_img_set_src(
            l_tNoiseUi.pStatusIcon,
            LV_EXT_IMG_GET(noise_status_normal));
        lv_obj_set_pos(l_tNoiseUi.pStatusIcon, 24, NOISEAPP_SCALE_Y(328U));
        lv_obj_add_flag(l_tNoiseUi.pStatusIcon, LV_OBJ_FLAG_HIDDEN);
    }
    l_tNoiseUi.pStatusLabel = lv_label_create(pPage);
    if (NULL != l_tNoiseUi.pStatusLabel)
    {
        lv_label_set_text(l_tNoiseUi.pStatusLabel, "");
        lv_obj_set_pos(l_tNoiseUi.pStatusLabel, 68, NOISEAPP_SCALE_Y(330U));
    }

    l_tNoiseUi.pValueLabel = lv_label_create(pPage);
    if (NULL != l_tNoiseUi.pValueLabel)
    {
        lv_label_set_text(l_tNoiseUi.pValueLabel, "--");
        lv_ext_set_local_font(
            l_tNoiseUi.pValueLabel,
            FONT_HUGE,
            lv_color_white());
        lv_obj_set_pos(l_tNoiseUi.pValueLabel, 26, NOISEAPP_SCALE_Y(378U));
    }
    l_tNoiseUi.pUnitLabel = lv_label_create(pPage);
    if (NULL != l_tNoiseUi.pUnitLabel)
    {
        lv_label_set_text(l_tNoiseUi.pUnitLabel, "分贝");
        lv_obj_set_style_text_color(l_tNoiseUi.pUnitLabel, lv_color_white(), 0);
        lv_obj_set_pos(l_tNoiseUi.pUnitLabel, 112, NOISEAPP_SCALE_Y(403U));
    }

    if ((NULL == pIdleWave) ||
            (NULL == l_tNoiseUi.pTimeLabel) ||
            (NULL == l_tNoiseUi.pWaveClip) ||
            (NULL == l_tNoiseUi.pActiveWave) ||
            (NULL == l_tNoiseUi.pStatusIcon) ||
            (NULL == l_tNoiseUi.pStatusLabel) ||
            (NULL == l_tNoiseUi.pValueLabel) ||
            (NULL == l_tNoiseUi.pUnitLabel))
    {
        lv_obj_del(pPage);
        l_tNoiseUi.pBackground = NULL;
        l_tNoiseUi.pWaveClip = NULL;
        l_tNoiseUi.pActiveWave = NULL;
        l_tNoiseUi.pStatusIcon = NULL;
        l_tNoiseUi.pStatusLabel = NULL;
        l_tNoiseUi.pValueLabel = NULL;
        l_tNoiseUi.pUnitLabel = NULL;
        l_tNoiseUi.pTimeLabel = NULL;
        return NULL;
    }

    return pPage;
}

/***************************
 * NoiseApp_TimerCallback: 一秒定时器回调，刷新分贝与时间。
 * 参数：
 *   - pTimer: LVGL 定时器对象。
 * 返回值：无。
 ***************************/
static void NoiseApp_TimerCallback(lv_timer_t *pTimer)
{
    (void)pTimer;
    NoiseApp_RefreshMeasurement();

    return;
}

/***************************
 * NoiseApp_OnStart: 创建页面并请求启动麦克风采集。
 * 参数：无。
 * 返回值：无。
 ***************************/
static void NoiseApp_OnStart(void)
{
    rt_err_t tResult;

    rt_memset(&l_tNoiseUi, 0, sizeof(l_tNoiseUi));
    l_tNoiseUi.ucMode = NOISEAPP_MODE_INVALID;
    l_tNoiseUi.pRoot = lv_obj_create(lv_scr_act());
    if (NULL == l_tNoiseUi.pRoot)
    {
        return;
    }
    NoiseApp_PrepareContainer(l_tNoiseUi.pRoot);
    lv_obj_set_style_bg_color(l_tNoiseUi.pRoot, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(l_tNoiseUi.pRoot, LV_OPA_COVER, 0);

    l_tNoiseUi.pMainPage = NoiseApp_CreateMainPage(l_tNoiseUi.pRoot);
    l_tNoiseUi.pInfoPage = NoiseApp_CreateInfoPage(l_tNoiseUi.pRoot);
    if ((NULL == l_tNoiseUi.pMainPage) || (NULL == l_tNoiseUi.pInfoPage))
    {
        lv_obj_del(l_tNoiseUi.pRoot);
        rt_memset(&l_tNoiseUi, 0, sizeof(l_tNoiseUi));
        return;
    }

    tResult = NOISEMONITOR_Start();
    if (RT_EOK != tResult)
    {
        rt_kprintf("noise app: microphone start failed %ld\n", (long)tResult);
    }
    NoiseApp_RefreshMeasurement();
    l_tNoiseUi.pRefreshTimer = lv_timer_create(
        NoiseApp_TimerCallback,
        NOISEAPP_REFRESH_MS,
        NULL);
    if (NULL == l_tNoiseUi.pRefreshTimer)
    {
        (void)NOISEMONITOR_Stop();
        lv_obj_del(l_tNoiseUi.pRoot);
        rt_memset(&l_tNoiseUi, 0, sizeof(l_tNoiseUi));
    }

    return;
}

/***************************
 * NoiseApp_OnStop: 停止采集并释放全部 LVGL 对象。
 * 参数：无。
 * 返回值：无。
 ***************************/
static void NoiseApp_OnStop(void)
{
    rt_err_t tResult;

    if (NULL != l_tNoiseUi.pRefreshTimer)
    {
        lv_timer_del(l_tNoiseUi.pRefreshTimer);
    }
    tResult = NOISEMONITOR_Stop();
    if (RT_EOK != tResult)
    {
        rt_kprintf("noise app: microphone stop failed %ld\n", (long)tResult);
    }
    if (NULL != l_tNoiseUi.pRoot)
    {
        lv_obj_del(l_tNoiseUi.pRoot);
    }
    rt_memset(&l_tNoiseUi, 0, sizeof(l_tNoiseUi));

    return;
}

/***************************
 * NoiseApp_MessageHandler: 处理 GUI 应用生命周期消息。
 * 参数：
 *   - eMessage: GUI 应用消息类型。
 *   - pParameter: 未使用的消息参数。
 * 返回值：无。
 ***************************/
static void NoiseApp_MessageHandler(gui_app_msg_type_t eMessage, void *pParameter)
{
    (void)pParameter;
    switch (eMessage)
    {
    case GUI_APP_MSG_ONSTART:
        NoiseApp_OnStart();
        break;
    case GUI_APP_MSG_ONSTOP:
        NoiseApp_OnStop();
        break;
    default:
        break;
    }

    return;
}

/***************************
 * NoiseApp_Main: 注册噪声检测应用消息处理器。
 * 参数：
 *   - tIntent: GUI 启动意图，当前未使用。
 * 返回值：始终返回 0。
 ***************************/
static int NoiseApp_Main(intent_t tIntent)
{
    (void)tIntent;
    gui_app_regist_msg_handler(APP_ID, NoiseApp_MessageHandler);
    return 0;
}

BUILTIN_APP_EXPORT(
    LV_EXT_STR_ID(noise),
    LV_EXT_IMG_GET(noise_status_normal),
    APP_ID,
    NoiseApp_Main);
