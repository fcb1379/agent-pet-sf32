#include <rtthread.h>
#include <stdbool.h>
#include <stdint.h>

#include "littlevgl2rtt.h"
#include "lv_ext_resource_manager.h"
#include "gui_app_fwk.h"
#include "watch_alarm_service.h"

#define APP_ID                          "timer"
#define TIMER_REFRESH_MS                (250U)
#define TIMER_DEFAULT_SECONDS           (60U)
#define TIMER_MAX_SECONDS               (24U * 60U * 60U)
#define TIMER_FONT_CAPTION              (20U)
#define TIMER_FONT_BODY                 (24U)
#define TIMER_FONT_SUBTITLE             (28U)
#define TIMER_FONT_TITLE                (36U)
#define TIMER_FONT_VALUE                (56U)
#define TIMER_FONT_DONE                 (72U)

LV_IMG_DECLARE(timer_bg_done);
LV_IMG_DECLARE(timer_bg_row);
LV_IMG_DECLARE(timer_ring);
LV_IMG_DECLARE(timer_ring_done);
LV_IMG_DECLARE(timer_minutes);
LV_IMG_DECLARE(timer_icon_preset_add);
LV_IMG_DECLARE(timer_icon_preset);
LV_IMG_DECLARE(timer_button_start);
LV_IMG_DECLARE(timer_button_start_pressed);
LV_IMG_DECLARE(timer_icon_add);
LV_IMG_DECLARE(timer_picker_off);
LV_IMG_DECLARE(timer_picker_on);
LV_IMG_DECLARE(timer_icon_cancel_done);
LV_IMG_DECLARE(timer_icon_cancel);
LV_IMG_DECLARE(timer_icon_start_big);
LV_IMG_DECLARE(timer_icon_start_small);
LV_IMG_DECLARE(timer_icon_pause_big);
LV_IMG_DECLARE(timer_icon_pause_small);
LV_IMG_DECLARE(timer_icon_retry_done);
LV_IMG_DECLARE(timer_delete_row);
LV_IMG_DECLARE(timer_icon_remove_badge);

typedef enum _TIMER_PAGE
{
    TIMER_PAGE_HOME = 0,
    TIMER_PAGE_SET,
    TIMER_PAGE_RUNNING,
    TIMER_PAGE_LIST,
    TIMER_PAGE_DONE
} TIMER_PAGE;

typedef enum _TIMER_ACTION
{
    TIMER_ACTION_ADD = 1,
    TIMER_ACTION_PRESET_ONE,
    TIMER_ACTION_PRESET_THREE,
    TIMER_ACTION_PRESET_FOUR,
    TIMER_ACTION_PRESET_FIVE,
    TIMER_ACTION_EDIT,
    TIMER_ACTION_DELETE_HISTORY,
    TIMER_ACTION_HOUR_UP,
    TIMER_ACTION_HOUR_DOWN,
    TIMER_ACTION_MINUTE_UP,
    TIMER_ACTION_MINUTE_DOWN,
    TIMER_ACTION_SECOND_UP,
    TIMER_ACTION_SECOND_DOWN,
    TIMER_ACTION_START,
    TIMER_ACTION_TOGGLE,
    TIMER_ACTION_CANCEL,
    TIMER_ACTION_RETRY,
    TIMER_ACTION_OPEN_LIST,
    TIMER_ACTION_BACK
} TIMER_ACTION;

/* TIMER_UI: bounded countdown page state.
 * Members:
 *   - pRoot: application page parent and gesture target.
 *   - pValue/pState: dynamic countdown labels.
 *   - pControl: play/pause image updated without rebuilding the ring.
 *   - pRefreshTimer: polls the RTOS-backed countdown state four times/second.
 *   - ePage: currently rendered flowchart page.
 *   - ulConfiguredSeconds: most recently selected/retry duration.
 *   - ucHour/ucMinute/ucSecond: temporary duration picker values.
 *   - bHistory/bEditHistory: recent-use tile visibility and edit mode.
 *   - tRendered: last backend snapshot for state transition detection.
 */
typedef struct _TIMER_UI
{
    lv_obj_t *pRoot;
    lv_obj_t *pValue;
    lv_obj_t *pState;
    lv_obj_t *pControl;
    lv_timer_t *pRefreshTimer;
    TIMER_PAGE ePage;
    uint32_t ulConfiguredSeconds;
    uint8_t ucHour;
    uint8_t ucMinute;
    uint8_t ucSecond;
    bool bHistory;
    bool bEditHistory;
    watch_alarm_snapshot_t tRendered;
} TIMER_UI;

/* Module-local timer UI state; accessed only from the LVGL task. */
static TIMER_UI l_tTimerUi;

static void TIMER_RenderPage(TIMER_PAGE ePage);
static void TIMER_GoBack(void);

static const lv_font_t *TIMER_GetFont(uint8_t ucSize)
{
    if (TIMER_FONT_DONE <= ucSize)
    {
        return LV_EXT_FONT_GET(TIMER_FONT_DONE);
    }
    if (TIMER_FONT_VALUE <= ucSize)
    {
        return LV_EXT_FONT_GET(TIMER_FONT_VALUE);
    }
    if (TIMER_FONT_TITLE <= ucSize)
    {
        return lv_theme_get_font_bigl(NULL);
    }
    if (TIMER_FONT_SUBTITLE <= ucSize)
    {
        return lv_theme_get_font_title(NULL);
    }
    if (TIMER_FONT_BODY <= ucSize)
    {
        return lv_theme_get_font_subtitle(NULL);
    }
    return lv_theme_get_font_normal(NULL);
}

static lv_obj_t *TIMER_CreateLabel(lv_obj_t *pParent, const char *pText,
                                   lv_coord_t lX, lv_coord_t lY,
                                   lv_coord_t lWidth, lv_color_t tColor,
                                   lv_text_align_t eAlign, uint8_t ucSize)
{
    lv_obj_t *pLabel;

    if ((NULL == pParent) || (NULL == pText))
    {
        return NULL;
    }
    pLabel = lv_label_create(pParent);
    if (NULL == pLabel)
    {
        return NULL;
    }
    lv_obj_set_pos(pLabel, lX, lY);
    lv_obj_set_width(pLabel, lWidth);
    lv_label_set_long_mode(pLabel, LV_LABEL_LONG_DOT);
    lv_label_set_text(pLabel, pText);
    lv_obj_set_style_text_color(pLabel, tColor, 0);
    lv_obj_set_style_text_align(pLabel, eAlign, 0);
    lv_obj_set_style_text_font(pLabel, TIMER_GetFont(ucSize), 0);
    return pLabel;
}

static lv_obj_t *TIMER_CreateImage(lv_obj_t *pParent,
                                   const void *pImage,
                                   lv_coord_t lX, lv_coord_t lY)
{
    lv_obj_t *pObject;

    if ((NULL == pParent) || (NULL == pImage))
    {
        return NULL;
    }
    pObject = lv_img_create(pParent);
    if (NULL == pObject)
    {
        return NULL;
    }
    lv_img_set_src(pObject, pImage);
    lv_obj_set_pos(pObject, lX, lY);
    return pObject;
}

static void TIMER_ActionEvent(lv_event_t *pEvent);

static lv_obj_t *TIMER_CreateImageButton(lv_obj_t *pParent,
                                         const void *pImage,
                                         lv_coord_t lX, lv_coord_t lY,
                                         TIMER_ACTION eAction)
{
    lv_obj_t *pObject;

    pObject = TIMER_CreateImage(pParent, pImage, lX, lY);
    if (NULL == pObject)
    {
        return NULL;
    }
    lv_obj_add_flag(pObject, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(pObject, TIMER_ActionEvent, LV_EVENT_SHORT_CLICKED,
                        (void *)(uintptr_t)eAction);
    return pObject;
}

static lv_obj_t *TIMER_CreatePressedButton(lv_obj_t *pParent,
                                           const void *pNormal,
                                           const void *pPressed,
                                           lv_coord_t lX, lv_coord_t lY,
                                           TIMER_ACTION eAction)
{
    lv_obj_t *pButton;

    if ((NULL == pParent) || (NULL == pNormal) || (NULL == pPressed))
    {
        return NULL;
    }
    pButton = lv_imgbtn_create(pParent);
    if (NULL == pButton)
    {
        return NULL;
    }
    lv_imgbtn_set_src(pButton, LV_IMGBTN_STATE_RELEASED, pNormal, NULL, NULL);
    lv_imgbtn_set_src(pButton, LV_IMGBTN_STATE_PRESSED, pPressed, NULL, NULL);
    lv_obj_set_pos(pButton, lX, lY);
    lv_obj_add_event_cb(pButton, TIMER_ActionEvent, LV_EVENT_SHORT_CLICKED,
                        (void *)(uintptr_t)eAction);
    return pButton;
}

static lv_obj_t *TIMER_CreateHitButton(lv_obj_t *pParent,
                                       lv_coord_t lX, lv_coord_t lY,
                                       lv_coord_t lWidth, lv_coord_t lHeight,
                                       TIMER_ACTION eAction)
{
    lv_obj_t *pButton;

    pButton = lv_obj_create(pParent);
    if (NULL == pButton)
    {
        return NULL;
    }
    lv_obj_set_pos(pButton, lX, lY);
    lv_obj_set_size(pButton, lWidth, lHeight);
    lv_obj_set_style_bg_opa(pButton, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pButton, 0, 0);
    lv_obj_set_style_pad_all(pButton, 0, 0);
    lv_obj_clear_flag(pButton, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(pButton, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(pButton, TIMER_ActionEvent, LV_EVENT_SHORT_CLICKED,
                        (void *)(uintptr_t)eAction);
    return pButton;
}

static void TIMER_ClearPage(void)
{
    if (NULL != l_tTimerUi.pRoot)
    {
        lv_obj_clean(l_tTimerUi.pRoot);
    }
    l_tTimerUi.pValue = NULL;
    l_tTimerUi.pState = NULL;
    l_tTimerUi.pControl = NULL;
}

static void TIMER_FormatTime(char *pBuffer, size_t ulLength,
                             uint32_t ulSeconds)
{
    if ((NULL == pBuffer) || (0U == ulLength))
    {
        return;
    }
    (void)rt_snprintf(pBuffer, ulLength, "%02lu:%02lu:%02lu",
                      (unsigned long)(ulSeconds / 3600U),
                      (unsigned long)((ulSeconds / 60U) % 60U),
                      (unsigned long)(ulSeconds % 60U));
}

static void TIMER_SetPicker(uint32_t ulSeconds)
{
    if (TIMER_MAX_SECONDS < ulSeconds)
    {
        ulSeconds = TIMER_MAX_SECONDS;
    }
    l_tTimerUi.ulConfiguredSeconds = ulSeconds;
    l_tTimerUi.ucHour = (uint8_t)(ulSeconds / 3600U);
    l_tTimerUi.ucMinute = (uint8_t)((ulSeconds / 60U) % 60U);
    l_tTimerUi.ucSecond = (uint8_t)(ulSeconds % 60U);
}

static uint32_t TIMER_GetPickerSeconds(void)
{
    return ((uint32_t)l_tTimerUi.ucHour * 3600U) +
           ((uint32_t)l_tTimerUi.ucMinute * 60U) +
           l_tTimerUi.ucSecond;
}

static void TIMER_RenderPreset(lv_coord_t lX, lv_coord_t lY,
                               const char *pText, TIMER_ACTION eAction)
{
    (void)TIMER_CreateImageButton(l_tTimerUi.pRoot, LV_EXT_IMG_GET(timer_icon_preset),
                                  lX, lY, eAction);
    (void)TIMER_CreateLabel(l_tTimerUi.pRoot, pText, lX + 20, lY + 65,
                            136, lv_color_hex(0x242124U),
                            LV_TEXT_ALIGN_CENTER, TIMER_FONT_BODY);
}

static void TIMER_RenderHome(void)
{
    lv_obj_add_flag(l_tTimerUi.pRoot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(l_tTimerUi.pRoot, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(l_tTimerUi.pRoot, LV_SCROLLBAR_MODE_OFF);
    lv_obj_scroll_to_y(l_tTimerUi.pRoot, 0, LV_ANIM_OFF);
    (void)TIMER_CreateLabel(l_tTimerUi.pRoot, "计时器", 18, 14, 230,
                            lv_color_hex(0xFF9500U), LV_TEXT_ALIGN_LEFT,
                            TIMER_FONT_SUBTITLE);
    (void)TIMER_CreateImageButton(l_tTimerUi.pRoot, LV_EXT_IMG_GET(timer_icon_add),
                                  LV_HOR_RES_MAX - 74, 0,
                                  TIMER_ACTION_ADD);
    if (l_tTimerUi.bHistory)
    {
        (void)TIMER_CreateLabel(l_tTimerUi.pRoot, "最近使用", 18, 70, 200,
                                lv_color_white(), LV_TEXT_ALIGN_LEFT,
                                TIMER_FONT_CAPTION);
        TIMER_RenderPreset(18, 92, "01:00", TIMER_ACTION_PRESET_ONE);
        if (l_tTimerUi.bEditHistory)
        {
            (void)TIMER_CreateImageButton(l_tTimerUi.pRoot,
                                          LV_EXT_IMG_GET(timer_icon_remove_badge),
                                          18, 92,
                                          TIMER_ACTION_DELETE_HISTORY);
        }
    }
    else
    {
        (void)TIMER_CreateLabel(l_tTimerUi.pRoot, "无活跃计时器", 18, 124,
                                LV_HOR_RES_MAX - 36,
                                lv_color_hex(0x8E8E93U),
                                LV_TEXT_ALIGN_CENTER, TIMER_FONT_BODY);
    }

    (void)TIMER_CreateLabel(l_tTimerUi.pRoot, "所有计时器", 18, 282, 200,
                            lv_color_white(), LV_TEXT_ALIGN_LEFT,
                            TIMER_FONT_CAPTION);
    (void)TIMER_CreateImageButton(l_tTimerUi.pRoot, LV_EXT_IMG_GET(timer_icon_preset_add),
                                  18, 304, TIMER_ACTION_ADD);
    TIMER_RenderPreset(216, 304, "01:00", TIMER_ACTION_PRESET_ONE);
    TIMER_RenderPreset(18, 494, "03:00", TIMER_ACTION_PRESET_THREE);
    TIMER_RenderPreset(216, 494, "04:00", TIMER_ACTION_PRESET_FOUR);
    TIMER_RenderPreset(18, 684, "05:00", TIMER_ACTION_PRESET_FIVE);
    (void)TIMER_CreateLabel(l_tTimerUi.pRoot,
                            l_tTimerUi.bEditHistory ? "完成" : "编辑",
                            18, 874,
                            LV_HOR_RES_MAX - 36,
                            lv_color_hex(0xC7C7CCU), LV_TEXT_ALIGN_CENTER,
                            TIMER_FONT_BODY);
    (void)TIMER_CreateHitButton(l_tTimerUi.pRoot, 18, 856,
                                LV_HOR_RES_MAX - 36, 52,
                                TIMER_ACTION_EDIT);
}

static void TIMER_RenderPickerColumn(lv_coord_t lX, const char *pValue,
                                     const char *pUnit,
                                     TIMER_ACTION eUp, TIMER_ACTION eDown,
                                     bool bSelected)
{
    (void)TIMER_CreateImage(l_tTimerUi.pRoot,
                            bSelected ? LV_EXT_IMG_GET(timer_picker_on) : LV_EXT_IMG_GET(timer_picker_off),
                            lX, 130);
    (void)TIMER_CreateLabel(l_tTimerUi.pRoot, "+", lX + 32, 142, 45,
                            lv_color_hex(0x8E8E93U), LV_TEXT_ALIGN_CENTER,
                            TIMER_FONT_TITLE);
    (void)TIMER_CreateLabel(l_tTimerUi.pRoot, pValue, lX + 10, 184, 89,
                            lv_color_white(), LV_TEXT_ALIGN_CENTER,
                            TIMER_FONT_VALUE);
    (void)TIMER_CreateLabel(l_tTimerUi.pRoot, pUnit, lX + 10, 250, 89,
                            lv_color_hex(0x8E8E93U), LV_TEXT_ALIGN_CENTER,
                            TIMER_FONT_CAPTION);
    (void)TIMER_CreateLabel(l_tTimerUi.pRoot, "−", lX + 32, 274, 45,
                            lv_color_hex(0x8E8E93U), LV_TEXT_ALIGN_CENTER,
                            TIMER_FONT_TITLE);
    (void)TIMER_CreateHitButton(l_tTimerUi.pRoot, lX, 130, 109, 75, eUp);
    (void)TIMER_CreateHitButton(l_tTimerUi.pRoot, lX, 245, 109, 64, eDown);
}

static void TIMER_RenderSet(void)
{
    char aHour[4];
    char aMinute[4];
    char aSecond[4];

    (void)TIMER_CreateImageButton(l_tTimerUi.pRoot, LV_EXT_IMG_GET(timer_icon_cancel),
                                  0, 0, TIMER_ACTION_BACK);
    (void)TIMER_CreateLabel(l_tTimerUi.pRoot, "计时器", 74, 20, 220,
                            lv_color_hex(0xFF9500U), LV_TEXT_ALIGN_LEFT,
                            TIMER_FONT_SUBTITLE);
    (void)rt_snprintf(aHour, sizeof(aHour), "%02u", l_tTimerUi.ucHour);
    (void)rt_snprintf(aMinute, sizeof(aMinute), "%02u", l_tTimerUi.ucMinute);
    (void)rt_snprintf(aSecond, sizeof(aSecond), "%02u", l_tTimerUi.ucSecond);
    TIMER_RenderPickerColumn(24, aHour, "小时", TIMER_ACTION_HOUR_UP,
                             TIMER_ACTION_HOUR_DOWN, false);
    TIMER_RenderPickerColumn(150, aMinute, "分钟", TIMER_ACTION_MINUTE_UP,
                             TIMER_ACTION_MINUTE_DOWN, true);
    TIMER_RenderPickerColumn(276, aSecond, "秒", TIMER_ACTION_SECOND_UP,
                             TIMER_ACTION_SECOND_DOWN, false);
    (void)TIMER_CreatePressedButton(l_tTimerUi.pRoot, LV_EXT_IMG_GET(timer_button_start),
                                    LV_EXT_IMG_GET(timer_button_start_pressed), 34, 378,
                                    TIMER_ACTION_START);
}

static void TIMER_RenderRunning(const watch_alarm_snapshot_t *pSnapshot)
{
    char aTime[16];

    (void)TIMER_CreateImageButton(l_tTimerUi.pRoot, LV_EXT_IMG_GET(timer_icon_add),
                                  LV_HOR_RES_MAX - 74, 0,
                                  TIMER_ACTION_OPEN_LIST);
    (void)TIMER_CreateImage(l_tTimerUi.pRoot, LV_EXT_IMG_GET(timer_ring), 35, 78);
    TIMER_FormatTime(aTime, sizeof(aTime), pSnapshot->timer_remaining_seconds);
    l_tTimerUi.pValue = TIMER_CreateLabel(l_tTimerUi.pRoot, aTime, 45, 188,
                                          320, lv_color_white(),
                                          LV_TEXT_ALIGN_CENTER,
                                          TIMER_FONT_VALUE);
    l_tTimerUi.pState = TIMER_CreateLabel(
        l_tTimerUi.pRoot,
        pSnapshot->timer_running ? "倒计时" : "已暂停",
        55, 270, 300, lv_color_hex(0x8E8E93U), LV_TEXT_ALIGN_CENTER,
        TIMER_FONT_CAPTION);
    (void)TIMER_CreateImageButton(l_tTimerUi.pRoot, LV_EXT_IMG_GET(timer_icon_cancel),
                                  24, 414, TIMER_ACTION_CANCEL);
    l_tTimerUi.pControl = TIMER_CreateImageButton(
        l_tTimerUi.pRoot,
        pSnapshot->timer_running ? LV_EXT_IMG_GET(timer_icon_pause_big) :
                                   LV_EXT_IMG_GET(timer_icon_start_big),
        LV_HOR_RES_MAX - 98, 414, TIMER_ACTION_TOGGLE);
}

static void TIMER_RenderList(const watch_alarm_snapshot_t *pSnapshot)
{
    char aTime[16];

    (void)TIMER_CreateImageButton(l_tTimerUi.pRoot, LV_EXT_IMG_GET(timer_icon_cancel),
                                  0, 0, TIMER_ACTION_BACK);
    (void)TIMER_CreateLabel(l_tTimerUi.pRoot, "计时器", 74, 20, 220,
                            lv_color_hex(0xFF9500U), LV_TEXT_ALIGN_LEFT,
                            TIMER_FONT_SUBTITLE);
    (void)TIMER_CreateImageButton(l_tTimerUi.pRoot, LV_EXT_IMG_GET(timer_icon_add),
                                  LV_HOR_RES_MAX - 74, 0,
                                  TIMER_ACTION_ADD);
    (void)TIMER_CreateImage(l_tTimerUi.pRoot, LV_EXT_IMG_GET(timer_bg_row), 11, 92);
    TIMER_FormatTime(aTime, sizeof(aTime), pSnapshot->timer_remaining_seconds);
    l_tTimerUi.pValue = TIMER_CreateLabel(l_tTimerUi.pRoot, aTime, 30, 110,
                                          230, lv_color_white(),
                                          LV_TEXT_ALIGN_LEFT,
                                          TIMER_FONT_BODY);
    l_tTimerUi.pState = TIMER_CreateLabel(
        l_tTimerUi.pRoot,
        pSnapshot->timer_running ? "倒计时运行中" : "倒计时已暂停",
        30, 148, 230, lv_color_hex(0x8E8E93U), LV_TEXT_ALIGN_LEFT,
        TIMER_FONT_CAPTION);
    l_tTimerUi.pControl = TIMER_CreateImageButton(
        l_tTimerUi.pRoot,
        pSnapshot->timer_running ? LV_EXT_IMG_GET(timer_icon_pause_small) :
                                   LV_EXT_IMG_GET(timer_icon_start_small),
        320, 106, TIMER_ACTION_TOGGLE);
}

static void TIMER_RenderDone(void)
{
    char aConfigured[16];

    (void)TIMER_CreateImage(l_tTimerUi.pRoot, LV_EXT_IMG_GET(timer_bg_done), 0, 0);
    (void)TIMER_CreateImage(l_tTimerUi.pRoot, LV_EXT_IMG_GET(timer_ring_done), 35, 78);
    (void)TIMER_CreateLabel(l_tTimerUi.pRoot, "结束", 55, 170, 300,
                            lv_color_white(), LV_TEXT_ALIGN_CENTER,
                            TIMER_FONT_DONE);
    TIMER_FormatTime(aConfigured, sizeof(aConfigured),
                     l_tTimerUi.ulConfiguredSeconds);
    (void)TIMER_CreateLabel(l_tTimerUi.pRoot, aConfigured, 55, 275, 300,
                            lv_color_white(), LV_TEXT_ALIGN_CENTER,
                            TIMER_FONT_CAPTION);
    (void)TIMER_CreateImageButton(l_tTimerUi.pRoot, LV_EXT_IMG_GET(timer_icon_cancel_done),
                                  24, 414, TIMER_ACTION_CANCEL);
    (void)TIMER_CreateImageButton(l_tTimerUi.pRoot, LV_EXT_IMG_GET(timer_icon_retry_done),
                                  LV_HOR_RES_MAX - 98, 414,
                                  TIMER_ACTION_RETRY);
}

static void TIMER_RenderPage(TIMER_PAGE ePage)
{
    watch_alarm_snapshot_t tSnapshot;

    if (NULL == l_tTimerUi.pRoot)
    {
        return;
    }
    (void)rt_memset(&tSnapshot, 0, sizeof(tSnapshot));
    (void)watch_alarm_get_snapshot(&tSnapshot);
    TIMER_ClearPage();
    lv_obj_clear_flag(l_tTimerUi.pRoot, LV_OBJ_FLAG_SCROLLABLE);
    l_tTimerUi.ePage = ePage;
    l_tTimerUi.tRendered = tSnapshot;
    rt_kprintf("timer: render page=%u\n", (unsigned int)ePage);

    switch (ePage)
    {
    case TIMER_PAGE_HOME:
        TIMER_RenderHome();
        break;
    case TIMER_PAGE_SET:
        TIMER_RenderSet();
        break;
    case TIMER_PAGE_RUNNING:
        TIMER_RenderRunning(&tSnapshot);
        break;
    case TIMER_PAGE_LIST:
        TIMER_RenderList(&tSnapshot);
        break;
    case TIMER_PAGE_DONE:
        TIMER_RenderDone();
        break;
    default:
        l_tTimerUi.ePage = TIMER_PAGE_HOME;
        TIMER_RenderHome();
        break;
    }
}

static void TIMER_GoBack(void)
{
    watch_alarm_snapshot_t tSnapshot;

    (void)watch_alarm_get_snapshot(&tSnapshot);
    if (TIMER_PAGE_HOME == l_tTimerUi.ePage)
    {
        if (RT_EOK != gui_app_goback())
        {
            (void)gui_app_run("Main");
        }
    }
    else if ((TIMER_PAGE_LIST == l_tTimerUi.ePage) &&
             ((0U != tSnapshot.timer_running) ||
              (0U != tSnapshot.timer_remaining_seconds)))
    {
        TIMER_RenderPage(TIMER_PAGE_RUNNING);
    }
    else
    {
        TIMER_RenderPage(TIMER_PAGE_HOME);
    }
}

static void TIMER_ActionEvent(lv_event_t *pEvent)
{
    TIMER_ACTION eAction;
    watch_alarm_snapshot_t tSnapshot;
    uint32_t ulSeconds;

    if ((NULL == pEvent) ||
        (LV_EVENT_SHORT_CLICKED != lv_event_get_code(pEvent)))
    {
        return;
    }
    eAction = (TIMER_ACTION)(uintptr_t)lv_event_get_user_data(pEvent);
    (void)rt_memset(&tSnapshot, 0, sizeof(tSnapshot));
    (void)watch_alarm_get_snapshot(&tSnapshot);

    if (TIMER_ACTION_ADD == eAction)
    {
        TIMER_SetPicker(TIMER_DEFAULT_SECONDS);
        TIMER_RenderPage(TIMER_PAGE_SET);
    }
    else if ((TIMER_ACTION_PRESET_ONE == eAction) ||
             (TIMER_ACTION_PRESET_THREE == eAction) ||
             (TIMER_ACTION_PRESET_FOUR == eAction) ||
             (TIMER_ACTION_PRESET_FIVE == eAction))
    {
        if (TIMER_ACTION_PRESET_ONE == eAction)
        {
            TIMER_SetPicker(60U);
        }
        else if (TIMER_ACTION_PRESET_THREE == eAction)
        {
            TIMER_SetPicker(180U);
        }
        else if (TIMER_ACTION_PRESET_FOUR == eAction)
        {
            TIMER_SetPicker(240U);
        }
        else
        {
            TIMER_SetPicker(300U);
        }
        TIMER_RenderPage(TIMER_PAGE_SET);
    }
    else if (TIMER_ACTION_EDIT == eAction)
    {
        l_tTimerUi.bEditHistory = !l_tTimerUi.bEditHistory;
        TIMER_RenderPage(TIMER_PAGE_HOME);
    }
    else if (TIMER_ACTION_DELETE_HISTORY == eAction)
    {
        l_tTimerUi.bHistory = false;
        l_tTimerUi.bEditHistory = false;
        TIMER_RenderPage(TIMER_PAGE_HOME);
    }
    else if (TIMER_ACTION_HOUR_UP == eAction)
    {
        l_tTimerUi.ucHour = (23U == l_tTimerUi.ucHour) ? 0U :
                            l_tTimerUi.ucHour + 1U;
        TIMER_RenderPage(TIMER_PAGE_SET);
    }
    else if (TIMER_ACTION_HOUR_DOWN == eAction)
    {
        l_tTimerUi.ucHour = (0U == l_tTimerUi.ucHour) ? 23U :
                            l_tTimerUi.ucHour - 1U;
        TIMER_RenderPage(TIMER_PAGE_SET);
    }
    else if (TIMER_ACTION_MINUTE_UP == eAction)
    {
        l_tTimerUi.ucMinute = (59U == l_tTimerUi.ucMinute) ? 0U :
                              l_tTimerUi.ucMinute + 1U;
        TIMER_RenderPage(TIMER_PAGE_SET);
    }
    else if (TIMER_ACTION_MINUTE_DOWN == eAction)
    {
        l_tTimerUi.ucMinute = (0U == l_tTimerUi.ucMinute) ? 59U :
                              l_tTimerUi.ucMinute - 1U;
        TIMER_RenderPage(TIMER_PAGE_SET);
    }
    else if (TIMER_ACTION_SECOND_UP == eAction)
    {
        l_tTimerUi.ucSecond = (59U == l_tTimerUi.ucSecond) ? 0U :
                              l_tTimerUi.ucSecond + 1U;
        TIMER_RenderPage(TIMER_PAGE_SET);
    }
    else if (TIMER_ACTION_SECOND_DOWN == eAction)
    {
        l_tTimerUi.ucSecond = (0U == l_tTimerUi.ucSecond) ? 59U :
                              l_tTimerUi.ucSecond - 1U;
        TIMER_RenderPage(TIMER_PAGE_SET);
    }
    else if (TIMER_ACTION_START == eAction)
    {
        ulSeconds = TIMER_GetPickerSeconds();
        if (0U == ulSeconds)
        {
            ulSeconds = TIMER_DEFAULT_SECONDS;
        }
        l_tTimerUi.ulConfiguredSeconds = ulSeconds;
        l_tTimerUi.bHistory = true;
        (void)watch_timer_start(ulSeconds);
        TIMER_RenderPage(TIMER_PAGE_RUNNING);
    }
    else if (TIMER_ACTION_TOGGLE == eAction)
    {
        if (0U != tSnapshot.timer_running)
        {
            (void)watch_timer_pause();
        }
        else
        {
            ulSeconds = tSnapshot.timer_remaining_seconds;
            if (0U == ulSeconds)
            {
                ulSeconds = l_tTimerUi.ulConfiguredSeconds;
            }
            (void)watch_timer_start(ulSeconds);
        }
        TIMER_RenderPage(l_tTimerUi.ePage);
    }
    else if (TIMER_ACTION_CANCEL == eAction)
    {
        (void)watch_alarm_dismiss();
        (void)watch_timer_reset();
        TIMER_RenderPage(TIMER_PAGE_HOME);
    }
    else if (TIMER_ACTION_RETRY == eAction)
    {
        (void)watch_alarm_dismiss();
        ulSeconds = (0U == l_tTimerUi.ulConfiguredSeconds) ?
                    TIMER_DEFAULT_SECONDS : l_tTimerUi.ulConfiguredSeconds;
        (void)watch_timer_start(ulSeconds);
        TIMER_RenderPage(TIMER_PAGE_RUNNING);
    }
    else if (TIMER_ACTION_OPEN_LIST == eAction)
    {
        TIMER_RenderPage(TIMER_PAGE_LIST);
    }
    else if (TIMER_ACTION_BACK == eAction)
    {
        TIMER_GoBack();
    }
}

static void TIMER_GestureEvent(lv_event_t *pEvent)
{
    lv_indev_t *pInput;

    if ((NULL == pEvent) || (LV_EVENT_GESTURE != lv_event_get_code(pEvent)))
    {
        return;
    }
    pInput = lv_indev_get_act();
    if ((NULL != pInput) && (LV_DIR_LEFT == lv_indev_get_gesture_dir(pInput)))
    {
        TIMER_GoBack();
    }
}

static void TIMER_RefreshTimer(lv_timer_t *pTimer)
{
    watch_alarm_snapshot_t tSnapshot;
    char aTime[16];

    (void)pTimer;
    if (RT_EOK != watch_alarm_get_snapshot(&tSnapshot))
    {
        return;
    }
    if ((0U != tSnapshot.timer_ringing) &&
        (TIMER_PAGE_DONE != l_tTimerUi.ePage))
    {
        TIMER_RenderPage(TIMER_PAGE_DONE);
        return;
    }
    if (((TIMER_PAGE_RUNNING == l_tTimerUi.ePage) ||
         (TIMER_PAGE_LIST == l_tTimerUi.ePage)) &&
        (0U == tSnapshot.timer_ringing))
    {
        TIMER_FormatTime(aTime, sizeof(aTime),
                         tSnapshot.timer_remaining_seconds);
        if (NULL != l_tTimerUi.pValue)
        {
            lv_label_set_text(l_tTimerUi.pValue, aTime);
        }
        if (NULL != l_tTimerUi.pState)
        {
            lv_label_set_text(l_tTimerUi.pState,
                              tSnapshot.timer_running ? "倒计时" : "已暂停");
        }
        if ((NULL != l_tTimerUi.pControl) &&
            (tSnapshot.timer_running != l_tTimerUi.tRendered.timer_running))
        {
            lv_img_set_src(
                l_tTimerUi.pControl,
                tSnapshot.timer_running ?
                    ((TIMER_PAGE_LIST == l_tTimerUi.ePage) ?
                        LV_EXT_IMG_GET(timer_icon_pause_small) : LV_EXT_IMG_GET(timer_icon_pause_big)) :
                    ((TIMER_PAGE_LIST == l_tTimerUi.ePage) ?
                        LV_EXT_IMG_GET(timer_icon_start_small) : LV_EXT_IMG_GET(timer_icon_start_big)));
        }
        l_tTimerUi.tRendered = tSnapshot;
    }
}

static void TIMER_OnStart(void)
{
    watch_alarm_snapshot_t tSnapshot;

    rt_kprintf("timer: start begin\n");
    (void)rt_memset(&l_tTimerUi, 0, sizeof(l_tTimerUi));
    l_tTimerUi.bHistory = true;
    TIMER_SetPicker(TIMER_DEFAULT_SECONDS);
    (void)watch_alarm_get_snapshot(&tSnapshot);
    if ((0U != tSnapshot.timer_running) ||
        (0U != tSnapshot.timer_remaining_seconds))
    {
        l_tTimerUi.ulConfiguredSeconds = tSnapshot.timer_remaining_seconds;
    }

    l_tTimerUi.pRoot = lv_obj_create(lv_scr_act());
    if (NULL == l_tTimerUi.pRoot)
    {
        rt_kprintf("timer: root allocation failed\n");
        return;
    }
    lv_obj_set_size(l_tTimerUi.pRoot, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_set_style_bg_color(l_tTimerUi.pRoot, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(l_tTimerUi.pRoot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(l_tTimerUi.pRoot, 0, 0);
    lv_obj_set_style_pad_all(l_tTimerUi.pRoot, 0, 0);
    lv_obj_clear_flag(l_tTimerUi.pRoot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(l_tTimerUi.pRoot, TIMER_GestureEvent,
                        LV_EVENT_GESTURE, NULL);

    if (0U != tSnapshot.timer_ringing)
    {
        TIMER_RenderPage(TIMER_PAGE_DONE);
    }
    else if ((0U != tSnapshot.timer_running) ||
             (0U != tSnapshot.timer_remaining_seconds))
    {
        TIMER_RenderPage(TIMER_PAGE_RUNNING);
    }
    else
    {
        TIMER_RenderPage(TIMER_PAGE_HOME);
    }
    l_tTimerUi.pRefreshTimer = lv_timer_create(TIMER_RefreshTimer,
                                               TIMER_REFRESH_MS, NULL);
    rt_kprintf("timer: start complete\n");
}

static void TIMER_OnStop(void)
{
    if (NULL != l_tTimerUi.pRefreshTimer)
    {
        lv_timer_del(l_tTimerUi.pRefreshTimer);
        l_tTimerUi.pRefreshTimer = NULL;
    }
    if (NULL != l_tTimerUi.pRoot)
    {
        lv_obj_del(l_tTimerUi.pRoot);
        l_tTimerUi.pRoot = NULL;
    }
    (void)rt_memset(&l_tTimerUi, 0, sizeof(l_tTimerUi));
}

static void TIMER_MessageHandler(gui_app_msg_type_t eMessage, void *pParameter)
{
    (void)pParameter;
    if (GUI_APP_MSG_ONSTART == eMessage)
    {
        TIMER_OnStart();
    }
    else if (GUI_APP_MSG_ONSTOP == eMessage)
    {
        TIMER_OnStop();
    }
}

static int TIMER_AppMain(intent_t tIntent)
{
    (void)tIntent;
    gui_app_regist_msg_handler(APP_ID, TIMER_MessageHandler);
    return 0;
}

LV_IMG_DECLARE(img_timer);
BUILTIN_APP_EXPORT(LV_EXT_STR_ID(timer), LV_EXT_IMG_GET(img_timer), APP_ID,
                   TIMER_AppMain);
