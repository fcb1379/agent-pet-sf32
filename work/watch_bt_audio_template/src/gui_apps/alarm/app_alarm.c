#include <rtthread.h>
#include <stdbool.h>
#include <stdint.h>

#include "littlevgl2rtt.h"
#include "lv_ext_resource_manager.h"
#include "gui_app_fwk.h"
#include "watch_alarm_service.h"

#define APP_ID                          "alarm"
#define ALARM_REFRESH_MS                (300U)
#define ALARM_SNOOZE_SECONDS            (9U * 60U)
#define ALARM_REPEAT_ALL                (0x7FU)
#define ALARM_REPEAT_WORKDAYS           (0x3EU)
#define ALARM_REPEAT_WEEKEND            (0x41U)
#define ALARM_FONT_CAPTION              (20U)
#define ALARM_FONT_BODY                 (24U)
#define ALARM_FONT_SUBTITLE             (28U)
#define ALARM_FONT_TITLE                (36U)
#define ALARM_FONT_TIME                 (72U)

LV_IMG_DECLARE(alarm_bg_main);
LV_IMG_DECLARE(alarm_bg_edit_row);
LV_IMG_DECLARE(alarm_icon_back);
LV_IMG_DECLARE(alarm_icon_cancel);
LV_IMG_DECLARE(alarm_icon_confirm);
LV_IMG_DECLARE(alarm_icon_close);
LV_IMG_DECLARE(alarm_icon_edit);
LV_IMG_DECLARE(alarm_dial);
LV_IMG_DECLARE(alarm_wakeup_banner);
LV_IMG_DECLARE(alarm_toggle_off);
LV_IMG_DECLARE(alarm_toggle_on);
LV_IMG_DECLARE(alarm_repeat_check);
LV_IMG_DECLARE(alarm_button_delete);
LV_IMG_DECLARE(alarm_button_delete_pressed);
LV_IMG_DECLARE(alarm_button_snooze);
LV_IMG_DECLARE(alarm_button_snooze_pressed);
LV_IMG_DECLARE(alarm_button_stop);
LV_IMG_DECLARE(alarm_button_stop_pressed);

typedef enum _ALARM_PAGE
{
    ALARM_PAGE_LIST = 0,
    ALARM_PAGE_EDIT,
    ALARM_PAGE_TIME,
    ALARM_PAGE_REPEAT,
    ALARM_PAGE_DELETE_CONFIRM,
    ALARM_PAGE_RINGING
} ALARM_PAGE;

typedef enum _ALARM_ACTION
{
    ALARM_ACTION_ADD = 1,
    ALARM_ACTION_EDIT,
    ALARM_ACTION_TOGGLE,
    ALARM_ACTION_OPEN_TIME,
    ALARM_ACTION_OPEN_REPEAT,
    ALARM_ACTION_TOGGLE_SNOOZE,
    ALARM_ACTION_DELETE_REQUEST,
    ALARM_ACTION_DELETE_CONFIRM,
    ALARM_ACTION_TIME_CONFIRM,
    ALARM_ACTION_HOUR_UP,
    ALARM_ACTION_HOUR_DOWN,
    ALARM_ACTION_MINUTE_UP,
    ALARM_ACTION_MINUTE_DOWN,
    ALARM_ACTION_REPEAT_SUNDAY,
    ALARM_ACTION_REPEAT_MONDAY,
    ALARM_ACTION_REPEAT_TUESDAY,
    ALARM_ACTION_REPEAT_WEDNESDAY,
    ALARM_ACTION_REPEAT_THURSDAY,
    ALARM_ACTION_REPEAT_FRIDAY,
    ALARM_ACTION_REPEAT_SATURDAY,
    ALARM_ACTION_SNOOZE,
    ALARM_ACTION_STOP,
    ALARM_ACTION_BACK
} ALARM_ACTION;

/* ALARM_UI: bounded page state for the alarm application.
 * Members:
 *   - pRoot: application gesture target and page parent.
 *   - pRefreshTimer: checks backend ringing and schedule changes.
 *   - ePage: currently rendered flowchart page.
 *   - ucEditHour/ucEditMinute: temporary time picker value.
 *   - ucRepeatMask: bit0 Sunday through bit6 Saturday.
 *   - bSnoozeEnabled: whether the ringing page offers the 9-minute action.
 *   - tRendered: backend snapshot used to avoid unnecessary page rebuilds.
 */
typedef struct _ALARM_UI
{
    lv_obj_t *pRoot;
    lv_timer_t *pRefreshTimer;
    ALARM_PAGE ePage;
    uint8_t ucEditHour;
    uint8_t ucEditMinute;
    uint8_t ucRepeatMask;
    bool bSnoozeEnabled;
    watch_alarm_snapshot_t tRendered;
} ALARM_UI;

/* Module-local alarm UI state; accessed only by the LVGL task. */
static ALARM_UI l_tAlarmUi;

static void ALARM_RenderPage(ALARM_PAGE ePage);
static void ALARM_GoBack(void);

/* ALARM_GetFont: map design pixel sizes to already registered theme fonts.
 * Parameter: ucSize requested pixel size.
 * Return value: matching theme font.
 */
static const lv_font_t *ALARM_GetFont(uint8_t ucSize)
{
    if (ALARM_FONT_TIME <= ucSize)
    {
        return LV_EXT_FONT_GET(ALARM_FONT_TIME);
    }
    if (ALARM_FONT_TITLE <= ucSize)
    {
        return lv_theme_get_font_bigl(NULL);
    }
    if (ALARM_FONT_SUBTITLE <= ucSize)
    {
        return lv_theme_get_font_title(NULL);
    }
    if (ALARM_FONT_BODY <= ucSize)
    {
        return lv_theme_get_font_subtitle(NULL);
    }
    return lv_theme_get_font_normal(NULL);
}

static lv_obj_t *ALARM_CreateLabel(lv_obj_t *pParent, const char *pText,
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
    lv_obj_set_style_text_font(pLabel, ALARM_GetFont(ucSize), 0);
    return pLabel;
}

static lv_obj_t *ALARM_CreateImage(lv_obj_t *pParent,
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

static void ALARM_ActionEvent(lv_event_t *pEvent);

static lv_obj_t *ALARM_CreateImageButton(lv_obj_t *pParent,
                                         const void *pImage,
                                         lv_coord_t lX, lv_coord_t lY,
                                         ALARM_ACTION eAction)
{
    lv_obj_t *pObject;

    pObject = ALARM_CreateImage(pParent, pImage, lX, lY);
    if (NULL == pObject)
    {
        return NULL;
    }
    lv_obj_add_flag(pObject, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(pObject, ALARM_ActionEvent, LV_EVENT_SHORT_CLICKED,
                        (void *)(uintptr_t)eAction);
    return pObject;
}

static lv_obj_t *ALARM_CreatePressedButton(lv_obj_t *pParent,
                                           const void *pNormal,
                                           const void *pPressed,
                                           lv_coord_t lX, lv_coord_t lY,
                                           ALARM_ACTION eAction)
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
    lv_obj_add_event_cb(pButton, ALARM_ActionEvent, LV_EVENT_SHORT_CLICKED,
                        (void *)(uintptr_t)eAction);
    return pButton;
}

static lv_obj_t *ALARM_CreateHitButton(lv_obj_t *pParent,
                                       lv_coord_t lX, lv_coord_t lY,
                                       lv_coord_t lWidth, lv_coord_t lHeight,
                                       ALARM_ACTION eAction)
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
    lv_obj_add_event_cb(pButton, ALARM_ActionEvent, LV_EVENT_SHORT_CLICKED,
                        (void *)(uintptr_t)eAction);
    return pButton;
}

static void ALARM_ClearPage(void)
{
    if (NULL != l_tAlarmUi.pRoot)
    {
        lv_obj_clean(l_tAlarmUi.pRoot);
    }
    return;
}

static void ALARM_FormatTime(char *pBuffer, size_t ulLength,
                             uint8_t ucHour, uint8_t ucMinute)
{
    if ((NULL == pBuffer) || (0U == ulLength))
    {
        return;
    }
    (void)rt_snprintf(pBuffer, ulLength, "%02u:%02u", ucHour, ucMinute);
    return;
}

static const char *ALARM_RepeatText(uint8_t ucMask)
{
    ucMask &= ALARM_REPEAT_ALL;
    if (ALARM_REPEAT_ALL == ucMask)
    {
        return "每天";
    }
    if (ALARM_REPEAT_WORKDAYS == ucMask)
    {
        return "工作日";
    }
    if (ALARM_REPEAT_WEEKEND == ucMask)
    {
        return "周末";
    }
    if (0U == ucMask)
    {
        return "永不";
    }
    return "自定义";
}

static void ALARM_CreateBackground(void)
{
    (void)ALARM_CreateImage(l_tAlarmUi.pRoot, LV_EXT_IMG_GET(alarm_bg_main), 0, 0);
    return;
}

static void ALARM_RenderList(const watch_alarm_snapshot_t *pSnapshot)
{
    char aTime[12];

    ALARM_CreateBackground();
    (void)ALARM_CreateLabel(l_tAlarmUi.pRoot, "闹钟", 18, 14, 200,
                            lv_color_hex(0xFF9500U), LV_TEXT_ALIGN_LEFT,
                            ALARM_FONT_SUBTITLE);
    (void)ALARM_CreateImageButton(l_tAlarmUi.pRoot, LV_EXT_IMG_GET(alarm_icon_edit),
                                  LV_HOR_RES_MAX - 90, 0, ALARM_ACTION_ADD);
    if ((NULL == pSnapshot) || (0U == pSnapshot->alarm_present))
    {
        (void)ALARM_CreateLabel(l_tAlarmUi.pRoot, "无闹钟", 18, 210,
                                LV_HOR_RES_MAX - 36,
                                lv_color_hex(0x8E8E93U),
                                LV_TEXT_ALIGN_CENTER, ALARM_FONT_BODY);
        return;
    }

    ALARM_FormatTime(aTime, sizeof(aTime), pSnapshot->alarm_hour,
                     pSnapshot->alarm_minute);
    (void)ALARM_CreateImage(l_tAlarmUi.pRoot, LV_EXT_IMG_GET(alarm_wakeup_banner), 23, 74);
    (void)ALARM_CreateLabel(l_tAlarmUi.pRoot, "起床", 38, 88, 120,
                            lv_color_hex(0xFF9500U), LV_TEXT_ALIGN_LEFT,
                            ALARM_FONT_CAPTION);
    (void)ALARM_CreateLabel(l_tAlarmUi.pRoot, aTime, 38, 118, 180,
                            pSnapshot->alarm_enabled ? lv_color_white() :
                            lv_color_hex(0x77777EU),
                            LV_TEXT_ALIGN_LEFT, ALARM_FONT_TITLE);
    (void)ALARM_CreateLabel(l_tAlarmUi.pRoot,
                            ALARM_RepeatText(pSnapshot->alarm_repeat_mask),
                            38, 169, 180, lv_color_hex(0x8E8E93U),
                            LV_TEXT_ALIGN_LEFT, ALARM_FONT_CAPTION);
    (void)ALARM_CreateImageButton(
        l_tAlarmUi.pRoot,
        pSnapshot->alarm_enabled ? LV_EXT_IMG_GET(alarm_toggle_on) : LV_EXT_IMG_GET(alarm_toggle_off),
        310, 116, ALARM_ACTION_TOGGLE);
    (void)ALARM_CreateHitButton(l_tAlarmUi.pRoot, 23, 74, 270, 134,
                                ALARM_ACTION_EDIT);
}

static void ALARM_RenderEdit(const watch_alarm_snapshot_t *pSnapshot)
{
    char aTime[12];

    ALARM_CreateBackground();
    (void)ALARM_CreateImageButton(l_tAlarmUi.pRoot, LV_EXT_IMG_GET(alarm_icon_close),
                                  0, 0, ALARM_ACTION_BACK);
    (void)ALARM_CreateLabel(l_tAlarmUi.pRoot, "编辑闹钟", 74, 20, 250,
                            lv_color_hex(0xFF9500U), LV_TEXT_ALIGN_LEFT,
                            ALARM_FONT_SUBTITLE);

    ALARM_FormatTime(aTime, sizeof(aTime), l_tAlarmUi.ucEditHour,
                     l_tAlarmUi.ucEditMinute);
    (void)ALARM_CreateImage(l_tAlarmUi.pRoot, LV_EXT_IMG_GET(alarm_bg_edit_row), 13, 78);
    (void)ALARM_CreateLabel(l_tAlarmUi.pRoot, "更改时间", 28, 92, 190,
                            lv_color_white(), LV_TEXT_ALIGN_LEFT,
                            ALARM_FONT_BODY);
    (void)ALARM_CreateLabel(l_tAlarmUi.pRoot, aTime, 250, 92, 125,
                            lv_color_hex(0x8E8E93U), LV_TEXT_ALIGN_RIGHT,
                            ALARM_FONT_BODY);
    (void)ALARM_CreateHitButton(l_tAlarmUi.pRoot, 13, 78, 384, 102,
                                ALARM_ACTION_OPEN_TIME);

    (void)ALARM_CreateImage(l_tAlarmUi.pRoot, LV_EXT_IMG_GET(alarm_bg_edit_row), 13, 188);
    (void)ALARM_CreateLabel(l_tAlarmUi.pRoot, "重复", 28, 202, 180,
                            lv_color_white(), LV_TEXT_ALIGN_LEFT,
                            ALARM_FONT_BODY);
    (void)ALARM_CreateLabel(l_tAlarmUi.pRoot,
                            ALARM_RepeatText(l_tAlarmUi.ucRepeatMask),
                            230, 202, 145, lv_color_hex(0x8E8E93U),
                            LV_TEXT_ALIGN_RIGHT, ALARM_FONT_BODY);
    (void)ALARM_CreateHitButton(l_tAlarmUi.pRoot, 13, 188, 384, 102,
                                ALARM_ACTION_OPEN_REPEAT);

    (void)ALARM_CreateImage(l_tAlarmUi.pRoot, LV_EXT_IMG_GET(alarm_bg_edit_row), 13, 298);
    (void)ALARM_CreateLabel(l_tAlarmUi.pRoot, "稍后提醒", 28, 312, 220,
                            lv_color_white(), LV_TEXT_ALIGN_LEFT,
                            ALARM_FONT_BODY);
    (void)ALARM_CreateImageButton(
        l_tAlarmUi.pRoot,
        l_tAlarmUi.bSnoozeEnabled ? LV_EXT_IMG_GET(alarm_toggle_on) : LV_EXT_IMG_GET(alarm_toggle_off),
        310, 312, ALARM_ACTION_TOGGLE_SNOOZE);

    if ((NULL != pSnapshot) && (0U != pSnapshot->alarm_present))
    {
        (void)ALARM_CreatePressedButton(
            l_tAlarmUi.pRoot, LV_EXT_IMG_GET(alarm_button_delete),
            LV_EXT_IMG_GET(alarm_button_delete_pressed), 23, 387,
            ALARM_ACTION_DELETE_REQUEST);
    }
}

static void ALARM_RenderTime(void)
{
    char aHour[4];
    char aMinute[4];

    ALARM_CreateBackground();
    (void)ALARM_CreateImageButton(l_tAlarmUi.pRoot, LV_EXT_IMG_GET(alarm_icon_cancel),
                                  0, 0, ALARM_ACTION_BACK);
    (void)ALARM_CreateImageButton(l_tAlarmUi.pRoot, LV_EXT_IMG_GET(alarm_icon_confirm),
                                  LV_HOR_RES_MAX - 74, 0,
                                  ALARM_ACTION_TIME_CONFIRM);
    (void)ALARM_CreateImage(l_tAlarmUi.pRoot, LV_EXT_IMG_GET(alarm_dial), 31, 82);
    (void)rt_snprintf(aHour, sizeof(aHour), "%02u", l_tAlarmUi.ucEditHour);
    (void)rt_snprintf(aMinute, sizeof(aMinute), "%02u",
                      l_tAlarmUi.ucEditMinute);
    (void)ALARM_CreateLabel(l_tAlarmUi.pRoot, aHour, 72, 190, 110,
                            lv_color_hex(0x30D98BU), LV_TEXT_ALIGN_CENTER,
                            ALARM_FONT_TIME);
    (void)ALARM_CreateLabel(l_tAlarmUi.pRoot, ":", 185, 190, 40,
                            lv_color_white(), LV_TEXT_ALIGN_CENTER,
                            ALARM_FONT_TIME);
    (void)ALARM_CreateLabel(l_tAlarmUi.pRoot, aMinute, 228, 190, 110,
                            lv_color_white(), LV_TEXT_ALIGN_CENTER,
                            ALARM_FONT_TIME);
    (void)ALARM_CreateLabel(l_tAlarmUi.pRoot, "+", 105, 112, 44,
                            lv_color_hex(0x8E8E93U), LV_TEXT_ALIGN_CENTER,
                            ALARM_FONT_TITLE);
    (void)ALARM_CreateLabel(l_tAlarmUi.pRoot, "+", 260, 112, 44,
                            lv_color_hex(0x8E8E93U), LV_TEXT_ALIGN_CENTER,
                            ALARM_FONT_TITLE);
    (void)ALARM_CreateLabel(l_tAlarmUi.pRoot, "−", 105, 350, 44,
                            lv_color_hex(0x8E8E93U), LV_TEXT_ALIGN_CENTER,
                            ALARM_FONT_TITLE);
    (void)ALARM_CreateLabel(l_tAlarmUi.pRoot, "−", 260, 350, 44,
                            lv_color_hex(0x8E8E93U), LV_TEXT_ALIGN_CENTER,
                            ALARM_FONT_TITLE);
    (void)ALARM_CreateHitButton(l_tAlarmUi.pRoot, 70, 95, 120, 100,
                                ALARM_ACTION_HOUR_UP);
    (void)ALARM_CreateHitButton(l_tAlarmUi.pRoot, 220, 95, 120, 100,
                                ALARM_ACTION_MINUTE_UP);
    (void)ALARM_CreateHitButton(l_tAlarmUi.pRoot, 70, 318, 120, 100,
                                ALARM_ACTION_HOUR_DOWN);
    (void)ALARM_CreateHitButton(l_tAlarmUi.pRoot, 220, 318, 120, 100,
                                ALARM_ACTION_MINUTE_DOWN);
}

static void ALARM_RenderRepeat(void)
{
    static const char *pDays[7] =
    {
        "星期日", "星期一", "星期二", "星期三",
        "星期四", "星期五", "星期六"
    };
    uint8_t ucDay;
    lv_coord_t lY;

    ALARM_CreateBackground();
    (void)ALARM_CreateImageButton(l_tAlarmUi.pRoot, LV_EXT_IMG_GET(alarm_icon_back),
                                  0, 0, ALARM_ACTION_BACK);
    (void)ALARM_CreateLabel(l_tAlarmUi.pRoot, "重复", 74, 20, 250,
                            lv_color_hex(0xFF9500U), LV_TEXT_ALIGN_LEFT,
                            ALARM_FONT_SUBTITLE);
    for (ucDay = 0U; ucDay < 7U; ucDay++)
    {
        lY = 78 + ((lv_coord_t)ucDay * 56);
        (void)ALARM_CreateLabel(l_tAlarmUi.pRoot, pDays[ucDay], 28, lY,
                                250, lv_color_white(), LV_TEXT_ALIGN_LEFT,
                                ALARM_FONT_BODY);
        if (0U != (l_tAlarmUi.ucRepeatMask & (1U << ucDay)))
        {
            (void)ALARM_CreateImage(l_tAlarmUi.pRoot, LV_EXT_IMG_GET(alarm_repeat_check),
                                    342, lY - 4);
        }
        (void)ALARM_CreateHitButton(
            l_tAlarmUi.pRoot, 18, lY - 8, 374, 48,
            (ALARM_ACTION)(ALARM_ACTION_REPEAT_SUNDAY + ucDay));
    }
}

static void ALARM_RenderDeleteConfirm(void)
{
    ALARM_CreateBackground();
    (void)ALARM_CreateImageButton(l_tAlarmUi.pRoot, LV_EXT_IMG_GET(alarm_icon_close),
                                  0, 0, ALARM_ACTION_BACK);
    (void)ALARM_CreateLabel(l_tAlarmUi.pRoot, "删除闹钟？", 30, 185,
                            LV_HOR_RES_MAX - 60, lv_color_white(),
                            LV_TEXT_ALIGN_CENTER, ALARM_FONT_TITLE);
    (void)ALARM_CreatePressedButton(l_tAlarmUi.pRoot, LV_EXT_IMG_GET(alarm_button_delete),
                                    LV_EXT_IMG_GET(alarm_button_delete_pressed), 23, 350,
                                    ALARM_ACTION_DELETE_CONFIRM);
}

static void ALARM_RenderRinging(const watch_alarm_snapshot_t *pSnapshot)
{
    char aTime[12];

    ALARM_CreateBackground();
    ALARM_FormatTime(aTime, sizeof(aTime), pSnapshot->alarm_hour,
                     pSnapshot->alarm_minute);
    (void)ALARM_CreateLabel(l_tAlarmUi.pRoot, aTime, 20, 42,
                            LV_HOR_RES_MAX - 40, lv_color_white(),
                            LV_TEXT_ALIGN_CENTER, ALARM_FONT_TIME);
    (void)ALARM_CreateLabel(l_tAlarmUi.pRoot, "闹钟", 20, 150,
                            LV_HOR_RES_MAX - 40,
                            lv_color_hex(0x8E8E93U), LV_TEXT_ALIGN_CENTER,
                            ALARM_FONT_BODY);
    if (l_tAlarmUi.bSnoozeEnabled)
    {
        (void)ALARM_CreatePressedButton(
            l_tAlarmUi.pRoot, LV_EXT_IMG_GET(alarm_button_snooze),
            LV_EXT_IMG_GET(alarm_button_snooze_pressed), 38, 270, ALARM_ACTION_SNOOZE);
    }
    (void)ALARM_CreatePressedButton(l_tAlarmUi.pRoot, LV_EXT_IMG_GET(alarm_button_stop),
                                    LV_EXT_IMG_GET(alarm_button_stop_pressed), 38, 382,
                                    ALARM_ACTION_STOP);
}

static void ALARM_RenderPage(ALARM_PAGE ePage)
{
    watch_alarm_snapshot_t tSnapshot;

    if (NULL == l_tAlarmUi.pRoot)
    {
        return;
    }
    (void)rt_memset(&tSnapshot, 0, sizeof(tSnapshot));
    (void)watch_alarm_get_snapshot(&tSnapshot);
    ALARM_ClearPage();
    l_tAlarmUi.ePage = ePage;
    l_tAlarmUi.tRendered = tSnapshot;
    rt_kprintf("alarm: render page=%u\n", (unsigned int)ePage);

    switch (ePage)
    {
    case ALARM_PAGE_LIST:
        ALARM_RenderList(&tSnapshot);
        break;
    case ALARM_PAGE_EDIT:
        ALARM_RenderEdit(&tSnapshot);
        break;
    case ALARM_PAGE_TIME:
        ALARM_RenderTime();
        break;
    case ALARM_PAGE_REPEAT:
        ALARM_RenderRepeat();
        break;
    case ALARM_PAGE_DELETE_CONFIRM:
        ALARM_RenderDeleteConfirm();
        break;
    case ALARM_PAGE_RINGING:
        ALARM_RenderRinging(&tSnapshot);
        break;
    default:
        ALARM_RenderList(&tSnapshot);
        l_tAlarmUi.ePage = ALARM_PAGE_LIST;
        break;
    }
}

static void ALARM_GoBack(void)
{
    switch (l_tAlarmUi.ePage)
    {
    case ALARM_PAGE_LIST:
        if (RT_EOK != gui_app_goback())
        {
            (void)gui_app_run("Main");
        }
        break;
    case ALARM_PAGE_TIME:
    case ALARM_PAGE_REPEAT:
    case ALARM_PAGE_DELETE_CONFIRM:
        ALARM_RenderPage(ALARM_PAGE_EDIT);
        break;
    case ALARM_PAGE_RINGING:
        (void)watch_alarm_dismiss();
        ALARM_RenderPage(ALARM_PAGE_LIST);
        break;
    default:
        ALARM_RenderPage(ALARM_PAGE_LIST);
        break;
    }
}

static void ALARM_ActionEvent(lv_event_t *pEvent)
{
    ALARM_ACTION eAction;
    watch_alarm_snapshot_t tSnapshot;
    uint8_t ucDay;

    if ((NULL == pEvent) ||
        (LV_EVENT_SHORT_CLICKED != lv_event_get_code(pEvent)))
    {
        return;
    }
    eAction = (ALARM_ACTION)(uintptr_t)lv_event_get_user_data(pEvent);
    (void)rt_memset(&tSnapshot, 0, sizeof(tSnapshot));
    (void)watch_alarm_get_snapshot(&tSnapshot);

    if ((ALARM_ACTION_ADD == eAction) || (ALARM_ACTION_EDIT == eAction))
    {
        l_tAlarmUi.ucEditHour = tSnapshot.alarm_hour;
        l_tAlarmUi.ucEditMinute = tSnapshot.alarm_minute;
        l_tAlarmUi.ucRepeatMask = tSnapshot.alarm_repeat_mask;
        if (ALARM_ACTION_ADD == eAction)
        {
            (void)watch_alarm_set_present(1U);
        }
        ALARM_RenderPage(ALARM_PAGE_EDIT);
    }
    else if (ALARM_ACTION_TOGGLE == eAction)
    {
        (void)watch_alarm_set((0U == tSnapshot.alarm_enabled) ? 1U : 0U,
                              tSnapshot.alarm_hour,
                              tSnapshot.alarm_minute);
        ALARM_RenderPage(ALARM_PAGE_LIST);
    }
    else if (ALARM_ACTION_OPEN_TIME == eAction)
    {
        ALARM_RenderPage(ALARM_PAGE_TIME);
    }
    else if (ALARM_ACTION_OPEN_REPEAT == eAction)
    {
        ALARM_RenderPage(ALARM_PAGE_REPEAT);
    }
    else if (ALARM_ACTION_TOGGLE_SNOOZE == eAction)
    {
        l_tAlarmUi.bSnoozeEnabled = !l_tAlarmUi.bSnoozeEnabled;
        ALARM_RenderPage(ALARM_PAGE_EDIT);
    }
    else if (ALARM_ACTION_DELETE_REQUEST == eAction)
    {
        ALARM_RenderPage(ALARM_PAGE_DELETE_CONFIRM);
    }
    else if (ALARM_ACTION_DELETE_CONFIRM == eAction)
    {
        (void)watch_alarm_set(0U, tSnapshot.alarm_hour,
                              tSnapshot.alarm_minute);
        (void)watch_alarm_set_present(0U);
        ALARM_RenderPage(ALARM_PAGE_LIST);
    }
    else if (ALARM_ACTION_TIME_CONFIRM == eAction)
    {
        (void)watch_alarm_set_present(1U);
        (void)watch_alarm_set_repeat(l_tAlarmUi.ucRepeatMask);
        (void)watch_alarm_set(1U, l_tAlarmUi.ucEditHour,
                              l_tAlarmUi.ucEditMinute);
        ALARM_RenderPage(ALARM_PAGE_EDIT);
    }
    else if (ALARM_ACTION_HOUR_UP == eAction)
    {
        l_tAlarmUi.ucEditHour = (23U == l_tAlarmUi.ucEditHour) ?
                                0U : l_tAlarmUi.ucEditHour + 1U;
        ALARM_RenderPage(ALARM_PAGE_TIME);
    }
    else if (ALARM_ACTION_HOUR_DOWN == eAction)
    {
        l_tAlarmUi.ucEditHour = (0U == l_tAlarmUi.ucEditHour) ?
                                23U : l_tAlarmUi.ucEditHour - 1U;
        ALARM_RenderPage(ALARM_PAGE_TIME);
    }
    else if (ALARM_ACTION_MINUTE_UP == eAction)
    {
        l_tAlarmUi.ucEditMinute = (59U == l_tAlarmUi.ucEditMinute) ?
                                  0U : l_tAlarmUi.ucEditMinute + 1U;
        ALARM_RenderPage(ALARM_PAGE_TIME);
    }
    else if (ALARM_ACTION_MINUTE_DOWN == eAction)
    {
        l_tAlarmUi.ucEditMinute = (0U == l_tAlarmUi.ucEditMinute) ?
                                  59U : l_tAlarmUi.ucEditMinute - 1U;
        ALARM_RenderPage(ALARM_PAGE_TIME);
    }
    else if ((ALARM_ACTION_REPEAT_SUNDAY <= eAction) &&
             (ALARM_ACTION_REPEAT_SATURDAY >= eAction))
    {
        ucDay = (uint8_t)(eAction - ALARM_ACTION_REPEAT_SUNDAY);
        l_tAlarmUi.ucRepeatMask ^= (uint8_t)(1U << ucDay);
        (void)watch_alarm_set_repeat(l_tAlarmUi.ucRepeatMask);
        ALARM_RenderPage(ALARM_PAGE_REPEAT);
    }
    else if (ALARM_ACTION_SNOOZE == eAction)
    {
        (void)watch_alarm_snooze(ALARM_SNOOZE_SECONDS);
        ALARM_RenderPage(ALARM_PAGE_LIST);
    }
    else if (ALARM_ACTION_STOP == eAction)
    {
        (void)watch_alarm_dismiss();
        ALARM_RenderPage(ALARM_PAGE_LIST);
    }
    else if (ALARM_ACTION_BACK == eAction)
    {
        ALARM_GoBack();
    }
}

static void ALARM_GestureEvent(lv_event_t *pEvent)
{
    lv_indev_t *pInput;

    if ((NULL == pEvent) || (LV_EVENT_GESTURE != lv_event_get_code(pEvent)))
    {
        return;
    }
    pInput = lv_indev_get_act();
    if ((NULL != pInput) && (LV_DIR_LEFT == lv_indev_get_gesture_dir(pInput)))
    {
        ALARM_GoBack();
    }
}

static void ALARM_RefreshTimer(lv_timer_t *pTimer)
{
    watch_alarm_snapshot_t tSnapshot;

    (void)pTimer;
    if (RT_EOK != watch_alarm_get_snapshot(&tSnapshot))
    {
        return;
    }
    if ((0U != tSnapshot.alarm_ringing) &&
        (ALARM_PAGE_RINGING != l_tAlarmUi.ePage))
    {
        ALARM_RenderPage(ALARM_PAGE_RINGING);
    }
    else if ((0U == tSnapshot.alarm_ringing) &&
             (ALARM_PAGE_RINGING == l_tAlarmUi.ePage))
    {
        ALARM_RenderPage(ALARM_PAGE_LIST);
    }
    else if ((ALARM_PAGE_LIST == l_tAlarmUi.ePage) &&
             (0 != rt_memcmp(&tSnapshot, &l_tAlarmUi.tRendered,
                             sizeof(tSnapshot))))
    {
        ALARM_RenderPage(ALARM_PAGE_LIST);
    }
}

static void ALARM_OnStart(void)
{
    watch_alarm_snapshot_t tSnapshot;

    rt_kprintf("alarm: start begin\n");
    (void)rt_memset(&l_tAlarmUi, 0, sizeof(l_tAlarmUi));
    l_tAlarmUi.bSnoozeEnabled = true;
    (void)watch_alarm_get_snapshot(&tSnapshot);
    l_tAlarmUi.ucEditHour = tSnapshot.alarm_hour;
    l_tAlarmUi.ucEditMinute = tSnapshot.alarm_minute;
    l_tAlarmUi.ucRepeatMask = tSnapshot.alarm_repeat_mask;

    l_tAlarmUi.pRoot = lv_obj_create(lv_scr_act());
    if (NULL == l_tAlarmUi.pRoot)
    {
        rt_kprintf("alarm: root allocation failed\n");
        return;
    }
    lv_obj_set_size(l_tAlarmUi.pRoot, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_set_style_bg_color(l_tAlarmUi.pRoot, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(l_tAlarmUi.pRoot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(l_tAlarmUi.pRoot, 0, 0);
    lv_obj_set_style_pad_all(l_tAlarmUi.pRoot, 0, 0);
    lv_obj_clear_flag(l_tAlarmUi.pRoot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(l_tAlarmUi.pRoot, ALARM_GestureEvent,
                        LV_EVENT_GESTURE, NULL);
    ALARM_RenderPage((0U != tSnapshot.alarm_ringing) ?
                     ALARM_PAGE_RINGING : ALARM_PAGE_LIST);
    l_tAlarmUi.pRefreshTimer = lv_timer_create(ALARM_RefreshTimer,
                                               ALARM_REFRESH_MS, NULL);
    rt_kprintf("alarm: start complete\n");
}

static void ALARM_OnStop(void)
{
    if (NULL != l_tAlarmUi.pRefreshTimer)
    {
        lv_timer_del(l_tAlarmUi.pRefreshTimer);
        l_tAlarmUi.pRefreshTimer = NULL;
    }
    if (NULL != l_tAlarmUi.pRoot)
    {
        lv_obj_del(l_tAlarmUi.pRoot);
        l_tAlarmUi.pRoot = NULL;
    }
    (void)rt_memset(&l_tAlarmUi, 0, sizeof(l_tAlarmUi));
}

static void ALARM_MessageHandler(gui_app_msg_type_t eMessage, void *pParameter)
{
    (void)pParameter;
    if (GUI_APP_MSG_ONSTART == eMessage)
    {
        ALARM_OnStart();
    }
    else if (GUI_APP_MSG_ONSTOP == eMessage)
    {
        ALARM_OnStop();
    }
}

static int ALARM_AppMain(intent_t tIntent)
{
    (void)tIntent;
    gui_app_regist_msg_handler(APP_ID, ALARM_MessageHandler);
    return 0;
}

LV_IMG_DECLARE(img_alarm);
BUILTIN_APP_EXPORT(LV_EXT_STR_ID(alarm), LV_EXT_IMG_GET(img_alarm), APP_ID,
                   ALARM_AppMain);
