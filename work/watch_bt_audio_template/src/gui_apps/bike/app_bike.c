#include <rtthread.h>
#include <string.h>

#include "bike_service.h"
#include "gui_app_fwk.h"
#include "littlevgl2rtt.h"
#include "lv_ext_resource_manager.h"
#include "lvgl.h"

#define APP_ID "Bike"
#define BIKE_UI_REFRESH_PERIOD_MS (500U)
#define BIKE_UI_SIDE_MARGIN (16)
#define BIKE_UI_PANEL_RADIUS (16)

LV_IMG_DECLARE(img_workout);

/* BIKE_UI_CONTEXT: 码表主页面的静态 LVGL 对象引用。
 * 成员说明：
 *   - pRoot: 页面根对象
 *   - pGpsLabel: GNSS 状态标签
 *   - pSpeedLabel: 当前速度标签
 *   - pDistanceLabel: 本次里程标签
 *   - pAverageLabel: 平均速度标签
 *   - pTimeLabel: 移动时间标签
 *   - pAltitudeLabel: 海拔标签
 *   - pStartLabel: 开始/暂停按钮文字
 *   - pTimer: 500 ms UI 刷新定时器
 */
typedef struct _BIKE_UI_CONTEXT
{
    lv_obj_t *pRoot;
    lv_obj_t *pGpsLabel;
    lv_obj_t *pSpeedLabel;
    lv_obj_t *pDistanceLabel;
    lv_obj_t *pAverageLabel;
    lv_obj_t *pTimeLabel;
    lv_obj_t *pAltitudeLabel;
    lv_obj_t *pStartLabel;
    lv_timer_t *pTimer;
} BIKE_UI_CONTEXT;

/* l_tBikeUi: 仅由 LVGL GUI 线程访问的码表页面上下文。 */
static BIKE_UI_CONTEXT l_tBikeUi;

/* BikeUi_SetPanelStyle: 设置深色高对比数据卡片样式。
 * 参数：
 *   - pObject: LVGL 对象
 * 返回值：无
 */
static void BikeUi_SetPanelStyle(lv_obj_t *pObject)
{
    if (NULL != pObject)
    {
        lv_obj_set_style_bg_color(pObject, lv_color_hex(0x17212B), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(pObject, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(pObject, 0, LV_PART_MAIN);
        lv_obj_set_style_radius(pObject, BIKE_UI_PANEL_RADIUS, LV_PART_MAIN);
        lv_obj_set_style_pad_all(pObject, 10, LV_PART_MAIN);
        lv_obj_clear_flag(pObject, LV_OBJ_FLAG_SCROLLABLE);
    }

    return;
}

/* BikeUi_CreateMetric: 创建一张指标卡片。
 * 参数：
 *   - pParent: 父对象
 *   - pTitle: 指标标题
 *   - lX/lY/lWidth/lHeight: 卡片位置和尺寸
 * 返回值：数值标签，失败返回 NULL
 */
static lv_obj_t *BikeUi_CreateMetric(lv_obj_t *pParent, const char *pTitle,
                                    lv_coord_t lX, lv_coord_t lY,
                                    lv_coord_t lWidth, lv_coord_t lHeight)
{
    lv_obj_t *pPanel;
    lv_obj_t *pTitleLabel;
    lv_obj_t *pValueLabel;

    if ((NULL == pParent) || (NULL == pTitle))
    {
        return NULL;
    }

    pPanel = lv_obj_create(pParent);
    if (NULL == pPanel)
    {
        return NULL;
    }
    lv_obj_set_pos(pPanel, lX, lY);
    lv_obj_set_size(pPanel, lWidth, lHeight);
    BikeUi_SetPanelStyle(pPanel);

    pTitleLabel = lv_label_create(pPanel);
    pValueLabel = lv_label_create(pPanel);
    if ((NULL == pTitleLabel) || (NULL == pValueLabel))
    {
        return NULL;
    }

    lv_label_set_text(pTitleLabel, pTitle);
    lv_obj_set_style_text_color(pTitleLabel, lv_color_hex(0x8FA3B8), LV_PART_MAIN);
    lv_obj_align(pTitleLabel, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_text(pValueLabel, "--");
    lv_obj_set_style_text_color(pValueLabel, lv_color_hex(0xF5F7FA), LV_PART_MAIN);
    lv_obj_set_style_text_font(pValueLabel, &lv_font_montserrat_20, LV_PART_MAIN);
    lv_obj_align(pValueLabel, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    return pValueLabel;
}

/* BikeUi_FormatTime: 更新移动时间标签。
 * 参数：
 *   - pLabel: LVGL 标签
 *   - ulTimeMs: 时间毫秒数
 * 返回值：无
 */
static void BikeUi_FormatTime(lv_obj_t *pLabel, uint32_t ulTimeMs)
{
    uint32_t ulTotalSeconds;
    uint32_t ulHours;
    uint32_t ulMinutes;
    uint32_t ulSeconds;

    if (NULL == pLabel)
    {
        return;
    }

    ulTotalSeconds = ulTimeMs / 1000U;
    ulHours = ulTotalSeconds / 3600U;
    ulMinutes = (ulTotalSeconds / 60U) % 60U;
    ulSeconds = ulTotalSeconds % 60U;
    lv_label_set_text_fmt(pLabel, "%02lu:%02lu:%02lu",
                          (unsigned long)ulHours,
                          (unsigned long)ulMinutes,
                          (unsigned long)ulSeconds);

    return;
}

/* BikeUi_Update: 从线程安全快照刷新现有对象，不在每帧重建控件。
 * 返回值：无
 */
static void BikeUi_Update(void)
{
    BIKE_SERVICE_SNAPSHOT tSnapshot;
    uint32_t ulDistanceCentiKm;
    uint32_t ulAgeMs;
    uint32_t ulAltitudeAbsoluteCm;
    const char *pAltitudeSign;
    const char *pGpsState;
    const char *pStartText;

    if (!BIKE_SERVICE_GetSnapshot(&tSnapshot))
    {
        return;
    }

    if (BIKE_GNSS_PORT_ERROR == tSnapshot.ePortStatus)
    {
        pGpsState = "UART2 ERROR";
    }
    else if (tSnapshot.tGnss.bFixValid)
    {
        pGpsState = "GNSS FIX";
    }
    else
    {
        pGpsState = "SEARCHING";
    }

    ulAgeMs = 0U;
    if (0U != tSnapshot.ulLastUpdateMs)
    {
        ulAgeMs = (uint32_t)rt_tick_get_millisecond() - tSnapshot.ulLastUpdateMs;
    }
    lv_label_set_text_fmt(l_tBikeUi.pGpsLabel, "%s  SAT %u  AGE %lus",
                          pGpsState,
                          (unsigned int)tSnapshot.tGnss.ucSatellites,
                          (unsigned long)(ulAgeMs / 1000U));

    lv_label_set_text_fmt(l_tBikeUi.pSpeedLabel, "%u.%02u",
                          (unsigned int)(tSnapshot.tRide.usSpeedCentiKph / 100U),
                          (unsigned int)(tSnapshot.tRide.usSpeedCentiKph % 100U));

    ulDistanceCentiKm = tSnapshot.tRide.ulDistanceMm / 10000U;
    lv_label_set_text_fmt(l_tBikeUi.pDistanceLabel, "%lu.%02lu km",
                          (unsigned long)(ulDistanceCentiKm / 100U),
                          (unsigned long)(ulDistanceCentiKm % 100U));
    lv_label_set_text_fmt(l_tBikeUi.pAverageLabel, "%u.%02u km/h",
                          (unsigned int)(tSnapshot.tRide.usAverageSpeedCentiKph / 100U),
                          (unsigned int)(tSnapshot.tRide.usAverageSpeedCentiKph % 100U));
    BikeUi_FormatTime(l_tBikeUi.pTimeLabel, tSnapshot.tRide.ulMovingTimeMs);
    pAltitudeSign = (0 > tSnapshot.tRide.lAltitudeCm) ? "-" : "";
    ulAltitudeAbsoluteCm = (uint32_t)((0 > tSnapshot.tRide.lAltitudeCm) ?
                                      -(int64_t)tSnapshot.tRide.lAltitudeCm :
                                      (int64_t)tSnapshot.tRide.lAltitudeCm);
    lv_label_set_text_fmt(l_tBikeUi.pAltitudeLabel, "%s%lu.%01lu m",
                          pAltitudeSign,
                          (unsigned long)(ulAltitudeAbsoluteCm / 100U),
                          (unsigned long)((ulAltitudeAbsoluteCm % 100U) / 10U));

    pStartText = (BIKE_RIDE_MODE_RUNNING == tSnapshot.tRide.eMode) ? "PAUSE" : "START";
    lv_label_set_text(l_tBikeUi.pStartLabel, pStartText);

    return;
}

/* BikeUi_TimerCallback: LVGL 定时刷新回调。
 * 参数：
 *   - pTimer: LVGL 定时器
 * 返回值：无
 */
static void BikeUi_TimerCallback(lv_timer_t *pTimer)
{
    (void)pTimer;
    BikeUi_Update();

    return;
}

/* BikeUi_StartEvent: 处理开始、继续和暂停操作。
 * 参数：
 *   - pEvent: LVGL 事件
 * 返回值：无
 */
static void BikeUi_StartEvent(lv_event_t *pEvent)
{
    BIKE_SERVICE_SNAPSHOT tSnapshot;

    if ((NULL == pEvent) || (LV_EVENT_CLICKED != lv_event_get_code(pEvent)))
    {
        return;
    }

    if (BIKE_SERVICE_GetSnapshot(&tSnapshot))
    {
        if (BIKE_RIDE_MODE_RUNNING == tSnapshot.tRide.eMode)
        {
            (void)BIKE_SERVICE_PauseRide();
        }
        else
        {
            (void)BIKE_SERVICE_StartRide();
        }
        BikeUi_Update();
    }

    return;
}

/* BikeUi_StopEvent: 处理结束骑行操作。
 * 参数：
 *   - pEvent: LVGL 事件
 * 返回值：无
 */
static void BikeUi_StopEvent(lv_event_t *pEvent)
{
    if ((NULL != pEvent) && (LV_EVENT_CLICKED == lv_event_get_code(pEvent)))
    {
        (void)BIKE_SERVICE_StopRide();
        BikeUi_Update();
    }

    return;
}

/* BikeUi_CreateButton: 创建底部大触控按钮。
 * 参数：
 *   - pParent: 父对象
 *   - pText: 按钮文字
 *   - lX: X 坐标
 *   - tColor: 背景色
 *   - pCallback: 点击回调
 * 返回值：按钮文字标签，失败返回 NULL
 */
static lv_obj_t *BikeUi_CreateButton(lv_obj_t *pParent, const char *pText, lv_coord_t lX,
                                    lv_color_t tColor, lv_event_cb_t pCallback)
{
    lv_obj_t *pButton;
    lv_obj_t *pLabel;

    if ((NULL == pParent) || (NULL == pText) || (NULL == pCallback))
    {
        return NULL;
    }

    pButton = lv_btn_create(pParent);
    if (NULL == pButton)
    {
        return NULL;
    }
    lv_obj_set_pos(pButton, lX, 368);
    lv_obj_set_size(pButton, 170, 58);
    lv_obj_set_style_bg_color(pButton, tColor, LV_PART_MAIN);
    lv_obj_set_style_radius(pButton, BIKE_UI_PANEL_RADIUS, LV_PART_MAIN);
    lv_obj_add_event_cb(pButton, pCallback, LV_EVENT_CLICKED, NULL);

    pLabel = lv_label_create(pButton);
    if (NULL != pLabel)
    {
        lv_label_set_text(pLabel, pText);
        lv_obj_set_style_text_font(pLabel, &lv_font_montserrat_20, LV_PART_MAIN);
        lv_obj_center(pLabel);
    }

    return pLabel;
}

/* BikeUi_OnStart: 创建 390 x 450 码表主页面。
 * 返回值：无
 */
static void BikeUi_OnStart(void)
{
    lv_obj_t *pSpeedUnit;

    (void)memset(&l_tBikeUi, 0, sizeof(l_tBikeUi));
    l_tBikeUi.pRoot = lv_obj_create(lv_scr_act());
    RT_ASSERT(NULL != l_tBikeUi.pRoot);
    lv_obj_set_size(l_tBikeUi.pRoot, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(l_tBikeUi.pRoot, 0, 0);
    lv_obj_set_style_bg_color(l_tBikeUi.pRoot, lv_color_hex(0x091017), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(l_tBikeUi.pRoot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(l_tBikeUi.pRoot, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(l_tBikeUi.pRoot, 0, LV_PART_MAIN);
    lv_obj_clear_flag(l_tBikeUi.pRoot, LV_OBJ_FLAG_SCROLLABLE);

    l_tBikeUi.pGpsLabel = lv_label_create(l_tBikeUi.pRoot);
    lv_label_set_text(l_tBikeUi.pGpsLabel, "SEARCHING  SAT 0  AGE 0s");
    lv_obj_set_style_text_color(l_tBikeUi.pGpsLabel, lv_color_hex(0x45D483), LV_PART_MAIN);
    lv_obj_align(l_tBikeUi.pGpsLabel, LV_ALIGN_TOP_MID, 0, 14);

    l_tBikeUi.pSpeedLabel = lv_label_create(l_tBikeUi.pRoot);
    lv_label_set_text(l_tBikeUi.pSpeedLabel, "0.00");
    lv_obj_set_style_text_color(l_tBikeUi.pSpeedLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    lv_obj_set_style_text_font(l_tBikeUi.pSpeedLabel, &lv_font_montserrat_36, LV_PART_MAIN);
    lv_obj_align(l_tBikeUi.pSpeedLabel, LV_ALIGN_TOP_MID, -18, 54);

    pSpeedUnit = lv_label_create(l_tBikeUi.pRoot);
    lv_label_set_text(pSpeedUnit, "km/h");
    lv_obj_set_style_text_color(pSpeedUnit, lv_color_hex(0x8FA3B8), LV_PART_MAIN);
    lv_obj_align_to(pSpeedUnit, l_tBikeUi.pSpeedLabel, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, -4);

    l_tBikeUi.pDistanceLabel = BikeUi_CreateMetric(l_tBikeUi.pRoot, "DISTANCE",
                                                   BIKE_UI_SIDE_MARGIN, 126, 171, 96);
    l_tBikeUi.pAverageLabel = BikeUi_CreateMetric(l_tBikeUi.pRoot, "AVERAGE",
                                                  203, 126, 171, 96);
    l_tBikeUi.pTimeLabel = BikeUi_CreateMetric(l_tBikeUi.pRoot, "MOVING TIME",
                                               BIKE_UI_SIDE_MARGIN, 238, 171, 96);
    l_tBikeUi.pAltitudeLabel = BikeUi_CreateMetric(l_tBikeUi.pRoot, "ALTITUDE",
                                                   203, 238, 171, 96);
    RT_ASSERT((NULL != l_tBikeUi.pDistanceLabel) && (NULL != l_tBikeUi.pAverageLabel) &&
              (NULL != l_tBikeUi.pTimeLabel) && (NULL != l_tBikeUi.pAltitudeLabel));

    l_tBikeUi.pStartLabel = BikeUi_CreateButton(l_tBikeUi.pRoot, "START", 16,
                                                lv_color_hex(0x168B4D), BikeUi_StartEvent);
    (void)BikeUi_CreateButton(l_tBikeUi.pRoot, "STOP", 204,
                              lv_color_hex(0xA9323A), BikeUi_StopEvent);
    RT_ASSERT(NULL != l_tBikeUi.pStartLabel);

    l_tBikeUi.pTimer = lv_timer_create(BikeUi_TimerCallback, BIKE_UI_REFRESH_PERIOD_MS, NULL);
    RT_ASSERT(NULL != l_tBikeUi.pTimer);
    BikeUi_Update();

    return;
}

/* BikeUi_OnStop: 删除 UI 自有定时器并清理页面引用。
 * 返回值：无
 */
static void BikeUi_OnStop(void)
{
    if (NULL != l_tBikeUi.pTimer)
    {
        lv_timer_del(l_tBikeUi.pTimer);
        l_tBikeUi.pTimer = NULL;
    }
    if (NULL != l_tBikeUi.pRoot)
    {
        lv_obj_del(l_tBikeUi.pRoot);
    }
    (void)memset(&l_tBikeUi, 0, sizeof(l_tBikeUi));

    return;
}

/* BikeUi_MessageHandler: 处理 GUI app 生命周期消息。
 * 参数：
 *   - eMessage: 生命周期消息
 *   - pParameter: 未使用
 * 返回值：无
 */
static void BikeUi_MessageHandler(gui_app_msg_type_t eMessage, void *pParameter)
{
    (void)pParameter;
    switch (eMessage)
    {
    case GUI_APP_MSG_ONSTART:
        BikeUi_OnStart();
        break;

    case GUI_APP_MSG_ONRESUME:
        BikeUi_Update();
        break;

    case GUI_APP_MSG_ONPAUSE:
        break;

    case GUI_APP_MSG_ONSTOP:
        BikeUi_OnStop();
        break;

    default:
        break;
    }

    return;
}

/* BikeUi_AppMain: 注册码表应用消息处理器。
 * 参数：
 *   - tIntent: GUI app 启动参数
 * 返回值：成功返回 0
 */
static int BikeUi_AppMain(intent_t tIntent)
{
    (void)tIntent;
    gui_app_regist_msg_handler(APP_ID, BikeUi_MessageHandler);

    return 0;
}

BUILTIN_APP_EXPORT(LV_EXT_STR_ID(bike_computer), LV_EXT_IMG_GET(img_workout), APP_ID, BikeUi_AppMain);
