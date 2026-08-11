#include <rtthread.h>
#include <stdint.h>

#include "gui_app_fwk.h"
#include "littlevgl2rtt.h"
#include "lv_ext_resource_manager.h"
#include "recorder_service.h"
#include "tf_card_service.h"

#define APP_ID                               "recorder"
#define RECORDER_APP_REFRESH_MS              (125U)
#define RECORDER_APP_SCREEN_WIDTH            (390)
#define RECORDER_APP_SCREEN_HEIGHT           (450)
#define RECORDER_APP_ICON_SIZE               (74)
#define RECORDER_APP_SMALL_ICON_SIZE         (52)
#define RECORDER_APP_FORMAT_BUTTON_WIDTH     (82)
#define RECORDER_APP_FORMAT_BUTTON_HEIGHT    (36)
#define RECORDER_APP_WAVE_BAR_COUNT          (29U)
#define RECORDER_APP_WAVE_BAR_WIDTH          (5)
#define RECORDER_APP_WAVE_BAR_STEP           (11)
#define RECORDER_APP_WAVE_CARD_X             (24)
#define RECORDER_APP_WAVE_CARD_WIDTH         (342)
#define RECORDER_APP_WAVE_CARD_HEIGHT        (118)
#define RECORDER_APP_WAVE_BAR_START_X        (13)
#define RECORDER_APP_WAVE_CURSOR_START_X     \
    (RECORDER_APP_WAVE_CARD_X + RECORDER_APP_WAVE_BAR_START_X)
#define RECORDER_APP_WAVE_CURSOR_TRAVEL      \
    ((RECORDER_APP_WAVE_BAR_COUNT - 1U) * RECORDER_APP_WAVE_BAR_STEP)
#define RECORDER_APP_SEEK_SECONDS            (15)
#define RECORDER_APP_STATUS_HOLD_TICKS       (24U)
#define RECORDER_APP_START_COOLDOWN_TICKS    (8U)
#define RECORDER_APP_COLOR_BACKGROUND        (0x03050AU)
#define RECORDER_APP_COLOR_PANEL             (0x10131FU)
#define RECORDER_APP_COLOR_PANEL_LIGHT       (0x191D2BU)
#define RECORDER_APP_COLOR_PRIMARY           (0xFF3158U)
#define RECORDER_APP_COLOR_BLUE              (0x087BFFU)
#define RECORDER_APP_COLOR_TEXT              (0xF4F6FFU)
#define RECORDER_APP_COLOR_MUTED             (0x8D93A8U)
#define RECORDER_APP_COLOR_DISABLED          (0x34394AU)

LV_IMG_DECLARE(img_recorder);
LV_IMG_DECLARE(recorder_icon_back);
LV_IMG_DECLARE(recorder_icon_close);
LV_IMG_DECLARE(recorder_icon_record);
LV_IMG_DECLARE(recorder_icon_stop);
LV_IMG_DECLARE(recorder_icon_play);
LV_IMG_DECLARE(recorder_icon_pause);
LV_IMG_DECLARE(recorder_icon_rewind);
LV_IMG_DECLARE(recorder_icon_forward);
LV_IMG_DECLARE(recorder_icon_microphone);

typedef enum _RECORDER_APP_PAGE
{
    RECORDER_APP_PAGE_IDLE = 0,
    RECORDER_APP_PAGE_RECORD,
    RECORDER_APP_PAGE_PLAYBACK
} RECORDER_APP_PAGE;

/* RECORDER_APP_UI: recorder page objects and render state.
 * All members are accessed only by the LVGL host thread. The three panels are
 * created once and visibility is switched from service snapshots, preventing
 * page reconstruction while audio callbacks are active.
 */
typedef struct _RECORDER_APP_UI
{
    lv_obj_t *pRoot;
    lv_obj_t *pIdlePanel;
    lv_obj_t *pRecordPanel;
    lv_obj_t *pPlaybackPanel;
    lv_obj_t *pIdleStatusLabel;
    lv_obj_t *pIdleFileLabel;
    lv_obj_t *pIdleFileMetaLabel;
    lv_obj_t *pIdleFileCounterLabel;
    lv_obj_t *pIdleRecordButton;
    lv_obj_t *aFormatButtons[RECORDER_FORMAT_COUNT];
    lv_obj_t *pRecordHeaderLabel;
    lv_obj_t *pRecordTimeLabel;
    lv_obj_t *pRecordSizeLabel;
    lv_obj_t *pRecordPauseImage;
    lv_obj_t *aRecordWaveBars[RECORDER_APP_WAVE_BAR_COUNT];
    lv_obj_t *pPlaybackHeaderLabel;
    lv_obj_t *pPlaybackFileLabel;
    lv_obj_t *pPlaybackStateLabel;
    lv_obj_t *pPlaybackTimeLabel;
    lv_obj_t *pPlaybackCursor;
    lv_obj_t *pPlaybackActionImage;
    lv_obj_t *aPlaybackWaveBars[RECORDER_APP_WAVE_BAR_COUNT];
    lv_timer_t *pRefreshTimer;
    RECORDER_APP_PAGE ePage;
    RECORDER_FORMAT eSelectedFormat;
    uint16_t usSelectedFile;
    uint16_t usKnownFileCount;
    uint16_t usIdleStatusHoldTicks;
    uint16_t usStartCooldownTicks;
} RECORDER_APP_UI;

/* l_tRecorderUi: single recorder UI instance, valid only while the app page is active. */
static RECORDER_APP_UI l_tRecorderUi;

/* RecorderApp_FormatName: return a short display name for one encoding format. */
static const char *RecorderApp_FormatName(RECORDER_FORMAT eFormat)
{
    static const char *l_aFormatNames[RECORDER_FORMAT_COUNT] =
    {
        "MP3", "AAC", "OPUS"
    };

    if (RECORDER_FORMAT_COUNT <= eFormat)
    {
        return "AUDIO";
    }

    return l_aFormatNames[eFormat];
}

/* RecorderApp_RecordStateActive: report whether the recording workflow owns the page. */
static bool RecorderApp_RecordStateActive(RECORDER_RECORD_STATE eState)
{
    return ((RECORDER_RECORD_STATE_STARTING == eState) ||
            (RECORDER_RECORD_STATE_RECORDING == eState) ||
            (RECORDER_RECORD_STATE_PAUSED == eState) ||
            (RECORDER_RECORD_STATE_STOPPING == eState));
}

/* RecorderApp_PlaybackStateActive: report whether local playback owns the page. */
static bool RecorderApp_PlaybackStateActive(RECORDER_PLAYBACK_STATE eState)
{
    return ((RECORDER_PLAYBACK_STATE_IDLE != eState) &&
            (RECORDER_PLAYBACK_STATE_ERROR != eState));
}

/* RecorderApp_CreatePanel: create one full-screen transparent state panel. */
static lv_obj_t *RecorderApp_CreatePanel(lv_obj_t *pParent)
{
    lv_obj_t *pPanel;

    if (NULL == pParent)
    {
        return NULL;
    }
    pPanel = lv_obj_create(pParent);
    if (NULL == pPanel)
    {
        return NULL;
    }
    lv_obj_set_pos(pPanel, 0, 0);
    lv_obj_set_size(pPanel,
                    RECORDER_APP_SCREEN_WIDTH,
                    RECORDER_APP_SCREEN_HEIGHT);
    lv_obj_set_style_bg_opa(pPanel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pPanel, 0, 0);
    lv_obj_set_style_pad_all(pPanel, 0, 0);
    lv_obj_clear_flag(pPanel, LV_OBJ_FLAG_SCROLLABLE);

    return pPanel;
}

/* RecorderApp_CreateLabel: create a positioned label with a project font and color. */
static lv_obj_t *RecorderApp_CreateLabel(lv_obj_t *pParent,
                                         lv_coord_t lX,
                                         lv_coord_t lY,
                                         lv_coord_t lWidth,
                                         uint16_t usFont,
                                         uint32_t ulColor,
                                         lv_text_align_t eAlignment)
{
    lv_obj_t *pLabel;

    if (NULL == pParent)
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
    lv_obj_set_style_text_align(pLabel, eAlignment, 0);
    lv_ext_set_local_font(pLabel, usFont, lv_color_hex(ulColor));

    return pLabel;
}

/* RecorderApp_CreateImageButton: create a transparent hit target around one generated icon. */
static lv_obj_t *RecorderApp_CreateImageButton(lv_obj_t *pParent,
                                                lv_coord_t lX,
                                                lv_coord_t lY,
                                                lv_coord_t lSize,
                                                const void *pImageSource,
                                                lv_event_cb_t pCallback,
                                                void *pUserData,
                                                lv_obj_t **ppImage)
{
    lv_obj_t *pButton;
    lv_obj_t *pImage;
    uint16_t usZoom;

    if ((NULL == pParent) || (NULL == pImageSource) || (NULL == pCallback))
    {
        return NULL;
    }
    pButton = lv_btn_create(pParent);
    if (NULL == pButton)
    {
        return NULL;
    }
    lv_obj_set_pos(pButton, lX, lY);
    lv_obj_set_size(pButton, lSize, lSize);
    lv_obj_set_style_bg_opa(pButton, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pButton, 0, 0);
    lv_obj_set_style_shadow_width(pButton, 0, 0);
    lv_obj_set_style_pad_all(pButton, 0, 0);
    lv_obj_add_event_cb(pButton, pCallback, LV_EVENT_CLICKED, pUserData);

    pImage = lv_img_create(pButton);
    if (NULL == pImage)
    {
        lv_obj_del(pButton);
        return NULL;
    }
    lv_img_set_src(pImage, pImageSource);
    usZoom = (uint16_t)(((uint32_t)lSize * 256U) /
                        RECORDER_APP_ICON_SIZE);
    lv_img_set_zoom(pImage, usZoom);
    lv_obj_center(pImage);
    if (NULL != ppImage)
    {
        *ppImage = pImage;
    }

    return pButton;
}

/* RecorderApp_CreateTextButton: create a rounded text button for format or secondary actions. */
static lv_obj_t *RecorderApp_CreateTextButton(lv_obj_t *pParent,
                                               lv_coord_t lX,
                                               lv_coord_t lY,
                                               lv_coord_t lWidth,
                                               lv_coord_t lHeight,
                                               const char *pText,
                                               lv_event_cb_t pCallback,
                                               void *pUserData)
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
    lv_obj_set_pos(pButton, lX, lY);
    lv_obj_set_size(pButton, lWidth, lHeight);
    lv_obj_set_style_radius(pButton, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(
        pButton, lv_color_hex(RECORDER_APP_COLOR_PANEL_LIGHT), 0);
    lv_obj_set_style_shadow_width(pButton, 0, 0);
    lv_obj_set_style_border_width(pButton, 1, 0);
    lv_obj_set_style_border_color(
        pButton, lv_color_hex(RECORDER_APP_COLOR_DISABLED), 0);
    lv_obj_add_event_cb(pButton, pCallback, LV_EVENT_CLICKED, pUserData);

    pLabel = lv_label_create(pButton);
    if (NULL != pLabel)
    {
        lv_label_set_text(pLabel, pText);
        lv_obj_set_style_text_color(
            pLabel, lv_color_hex(RECORDER_APP_COLOR_TEXT), 0);
        lv_obj_center(pLabel);
    }

    return pButton;
}

/* RecorderApp_CreateWaveform: build a fixed set of bars inside a waveform card. */
static void RecorderApp_CreateWaveform(
    lv_obj_t *pParent,
    lv_coord_t lY,
    uint32_t ulColor,
    lv_obj_t *aBars[RECORDER_APP_WAVE_BAR_COUNT])
{
    lv_obj_t *pCard;
    lv_coord_t lHeight;
    lv_coord_t lX;
    uint8_t ucIndex;

    if ((NULL == pParent) || (NULL == aBars))
    {
        return;
    }
    pCard = lv_obj_create(pParent);
    if (NULL == pCard)
    {
        return;
    }
    lv_obj_set_pos(pCard, RECORDER_APP_WAVE_CARD_X, lY);
    lv_obj_set_size(pCard,
                    RECORDER_APP_WAVE_CARD_WIDTH,
                    RECORDER_APP_WAVE_CARD_HEIGHT);
    lv_obj_set_style_bg_color(
        pCard, lv_color_hex(RECORDER_APP_COLOR_PANEL), 0);
    lv_obj_set_style_bg_opa(pCard, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(pCard, 0, 0);
    lv_obj_set_style_radius(pCard, 16, 0);
    lv_obj_set_style_pad_all(pCard, 0, 0);
    lv_obj_clear_flag(pCard, LV_OBJ_FLAG_SCROLLABLE);

    for (ucIndex = 0U;
         ucIndex < RECORDER_APP_WAVE_BAR_COUNT;
         ucIndex++)
    {
        lHeight = (lv_coord_t)(10 + ((ucIndex * 17U) % 42U));
        lX = (lv_coord_t)(RECORDER_APP_WAVE_BAR_START_X +
                          (ucIndex * RECORDER_APP_WAVE_BAR_STEP));
        aBars[ucIndex] = lv_obj_create(pCard);
        if (NULL == aBars[ucIndex])
        {
            continue;
        }
        lv_obj_set_pos(aBars[ucIndex], lX,
                       (RECORDER_APP_WAVE_CARD_HEIGHT - lHeight) / 2);
        lv_obj_set_size(aBars[ucIndex], RECORDER_APP_WAVE_BAR_WIDTH, lHeight);
        lv_obj_set_style_bg_color(aBars[ucIndex], lv_color_hex(ulColor), 0);
        lv_obj_set_style_bg_opa(aBars[ucIndex], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(aBars[ucIndex], 0, 0);
        lv_obj_set_style_radius(aBars[ucIndex], LV_RADIUS_CIRCLE, 0);
        lv_obj_clear_flag(aBars[ucIndex], LV_OBJ_FLAG_SCROLLABLE);
    }

    return;
}

/* RecorderApp_SetPage: show exactly one page panel. */
static void RecorderApp_SetPage(RECORDER_APP_PAGE ePage)
{
    if ((NULL == l_tRecorderUi.pIdlePanel) ||
        (NULL == l_tRecorderUi.pRecordPanel) ||
        (NULL == l_tRecorderUi.pPlaybackPanel))
    {
        return;
    }
    lv_obj_add_flag(l_tRecorderUi.pIdlePanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(l_tRecorderUi.pRecordPanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(l_tRecorderUi.pPlaybackPanel, LV_OBJ_FLAG_HIDDEN);
    if (RECORDER_APP_PAGE_RECORD == ePage)
    {
        lv_obj_clear_flag(l_tRecorderUi.pRecordPanel, LV_OBJ_FLAG_HIDDEN);
    }
    else if (RECORDER_APP_PAGE_PLAYBACK == ePage)
    {
        lv_obj_clear_flag(l_tRecorderUi.pPlaybackPanel, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        ePage = RECORDER_APP_PAGE_IDLE;
        lv_obj_clear_flag(l_tRecorderUi.pIdlePanel, LV_OBJ_FLAG_HIDDEN);
    }
    l_tRecorderUi.ePage = ePage;

    return;
}

/* RecorderApp_ShowIdleError: display a bounded service error on the idle page. */
static void RecorderApp_ShowIdleError(const char *pOperation,
                                      rt_err_t tResult)
{
    if ((NULL != pOperation) && (NULL != l_tRecorderUi.pIdleStatusLabel))
    {
        lv_label_set_text_fmt(l_tRecorderUi.pIdleStatusLabel,
                              "%s error %ld",
                              pOperation,
                              (long)tResult);
        l_tRecorderUi.usIdleStatusHoldTicks =
            RECORDER_APP_STATUS_HOLD_TICKS;
        rt_kprintf("[REC_UI] %s failed: %ld\n",
                   pOperation,
                   (long)tResult);
    }

    return;
}

/* RecorderApp_FormatEvent: choose the encoder for the next recording. */
static void RecorderApp_FormatEvent(lv_event_t *pEvent)
{
    RECORDER_FORMAT eFormat;

    if ((NULL == pEvent) || (LV_EVENT_CLICKED != lv_event_get_code(pEvent)))
    {
        return;
    }
    eFormat = (RECORDER_FORMAT)(uintptr_t)lv_event_get_user_data(pEvent);
    if (RECORDER_FORMAT_COUNT > eFormat)
    {
        l_tRecorderUi.eSelectedFormat = eFormat;
        l_tRecorderUi.usIdleStatusHoldTicks = 0U;
    }

    return;
}

/* RecorderApp_StartEvent: start recording from the large idle-page control. */
static void RecorderApp_StartEvent(lv_event_t *pEvent)
{
    rt_err_t tResult;
    bool bCardInserted;
    bool bCardMounted;

    if ((NULL == pEvent) || (LV_EVENT_CLICKED != lv_event_get_code(pEvent)))
    {
        return;
    }
    if (0U < l_tRecorderUi.usStartCooldownTicks)
    {
        rt_kprintf("[REC_UI] start ignored cooldown=%u\n",
                   (unsigned int)l_tRecorderUi.usStartCooldownTicks);
        return;
    }
    l_tRecorderUi.usStartCooldownTicks =
        RECORDER_APP_START_COOLDOWN_TICKS;
    bCardMounted = TF_CARD_IsMounted();
    bCardInserted = TF_CARD_IsInserted();
    rt_kprintf("[REC_UI] start click format=%u inserted=%u mounted=%u\n",
               (unsigned int)l_tRecorderUi.eSelectedFormat,
               (unsigned int)bCardInserted,
               (unsigned int)bCardMounted);
    if ((false == bCardMounted) && (false == bCardInserted))
    {
        RecorderApp_ShowIdleError("TF card required", -RT_ERROR);
        return;
    }
    lv_label_set_text_fmt(l_tRecorderUi.pIdleStatusLabel,
                          "Starting %s...",
                          RecorderApp_FormatName(
                              l_tRecorderUi.eSelectedFormat));
    l_tRecorderUi.usIdleStatusHoldTicks =
        RECORDER_APP_STATUS_HOLD_TICKS;
    tResult = RECORDER_Start(l_tRecorderUi.eSelectedFormat);
    if (RT_EOK == tResult)
    {
        RecorderApp_SetPage(RECORDER_APP_PAGE_RECORD);
    }
    else
    {
        RecorderApp_ShowIdleError("Record", tResult);
    }

    return;
}

/* RecorderApp_PauseEvent: pause or resume the current recording. */
static void RecorderApp_PauseEvent(lv_event_t *pEvent)
{
    RECORDER_SNAPSHOT tSnapshot;
    rt_err_t tResult;

    if ((NULL == pEvent) || (LV_EVENT_CLICKED != lv_event_get_code(pEvent)) ||
        (RT_EOK != RECORDER_GetSnapshot(&tSnapshot)))
    {
        return;
    }
    if (RECORDER_RECORD_STATE_RECORDING == tSnapshot.eRecordState)
    {
        tResult = RECORDER_Pause();
    }
    else if (RECORDER_RECORD_STATE_PAUSED == tSnapshot.eRecordState)
    {
        tResult = RECORDER_Resume();
    }
    else
    {
        tResult = -RT_EBUSY;
    }
    if ((RT_EOK != tResult) && (NULL != l_tRecorderUi.pRecordHeaderLabel))
    {
        lv_label_set_text_fmt(l_tRecorderUi.pRecordHeaderLabel,
                              "Audio error %ld", (long)tResult);
    }

    return;
}

/* RecorderApp_StopRecordEvent: save the current file and return after finalization. */
static void RecorderApp_StopRecordEvent(lv_event_t *pEvent)
{
    rt_err_t tResult;

    if ((NULL == pEvent) || (LV_EVENT_CLICKED != lv_event_get_code(pEvent)))
    {
        return;
    }
    tResult = RECORDER_Stop();
    if ((RT_EOK != tResult) && (-RT_EINVAL != tResult) &&
        (NULL != l_tRecorderUi.pRecordHeaderLabel))
    {
        lv_label_set_text_fmt(l_tRecorderUi.pRecordHeaderLabel,
                              "Save error %ld", (long)tResult);
    }

    return;
}

/* RecorderApp_SelectPreviousEvent: select the newer adjacent recording. */
static void RecorderApp_SelectPreviousEvent(lv_event_t *pEvent)
{
    if ((NULL != pEvent) && (LV_EVENT_CLICKED == lv_event_get_code(pEvent)) &&
        (0U < l_tRecorderUi.usSelectedFile))
    {
        l_tRecorderUi.usSelectedFile--;
    }

    return;
}

/* RecorderApp_SelectNextEvent: select the older adjacent recording. */
static void RecorderApp_SelectNextEvent(lv_event_t *pEvent)
{
    if ((NULL != pEvent) && (LV_EVENT_CLICKED == lv_event_get_code(pEvent)) &&
        ((l_tRecorderUi.usSelectedFile + 1U) <
         l_tRecorderUi.usKnownFileCount))
    {
        l_tRecorderUi.usSelectedFile++;
    }

    return;
}

/* RecorderApp_OpenPlaybackEvent: open the selected file in the playback page. */
static void RecorderApp_OpenPlaybackEvent(lv_event_t *pEvent)
{
    RECORDER_FILE_INFO tFileInfo;
    rt_err_t tResult;

    if ((NULL == pEvent) || (LV_EVENT_CLICKED != lv_event_get_code(pEvent)))
    {
        return;
    }
    rt_kprintf("[REC_UI] play click index=%u files=%u\n",
               (unsigned int)l_tRecorderUi.usSelectedFile,
               (unsigned int)RECORDER_GetFileCount());
    if (RT_EOK != RECORDER_GetFile(l_tRecorderUi.usSelectedFile, &tFileInfo))
    {
        rt_kprintf("[REC_UI] play file lookup failed index=%u\n",
                   (unsigned int)l_tRecorderUi.usSelectedFile);
        RecorderApp_ShowIdleError("No recording", -RT_ERROR);
        return;
    }
    rt_kprintf("[REC_UI] play selected path=%s size=%lu format=%u\n",
               tFileInfo.aPath,
               (unsigned long)tFileInfo.ulSizeBytes,
               (unsigned int)tFileInfo.eFormat);
    tResult = RECORDER_Play(tFileInfo.aPath);
    rt_kprintf("[REC_UI] play request result=%d\n", (int)tResult);
    if (RT_EOK == tResult)
    {
        RecorderApp_SetPage(RECORDER_APP_PAGE_PLAYBACK);
    }
    else
    {
        RecorderApp_ShowIdleError("Play", tResult);
    }

    return;
}

/* RecorderApp_PlayActionEvent: toggle pause/resume or restart ended playback. */
static void RecorderApp_PlayActionEvent(lv_event_t *pEvent)
{
    RECORDER_SNAPSHOT tSnapshot;
    rt_err_t tResult;

    if ((NULL == pEvent) || (LV_EVENT_CLICKED != lv_event_get_code(pEvent)) ||
        (RT_EOK != RECORDER_GetSnapshot(&tSnapshot)))
    {
        return;
    }
    if (RECORDER_PLAYBACK_STATE_PLAYING == tSnapshot.ePlaybackState)
    {
        tResult = RECORDER_PlaybackPause();
    }
    else if (RECORDER_PLAYBACK_STATE_PAUSED == tSnapshot.ePlaybackState)
    {
        tResult = RECORDER_PlaybackResume();
    }
    else
    {
        tResult = RECORDER_PlaybackRestart();
    }
    if ((RT_EOK != tResult) && (NULL != l_tRecorderUi.pPlaybackStateLabel))
    {
        lv_label_set_text_fmt(l_tRecorderUi.pPlaybackStateLabel,
                              "Play error %ld", (long)tResult);
    }

    return;
}

/* RecorderApp_RewindEvent: seek backward fifteen seconds. */
static void RecorderApp_RewindEvent(lv_event_t *pEvent)
{
    if ((NULL != pEvent) && (LV_EVENT_CLICKED == lv_event_get_code(pEvent)))
    {
        (void)RECORDER_PlaybackSeekRelative(-RECORDER_APP_SEEK_SECONDS);
    }

    return;
}

/* RecorderApp_ForwardEvent: seek forward fifteen seconds. */
static void RecorderApp_ForwardEvent(lv_event_t *pEvent)
{
    if ((NULL != pEvent) && (LV_EVENT_CLICKED == lv_event_get_code(pEvent)))
    {
        (void)RECORDER_PlaybackSeekRelative(RECORDER_APP_SEEK_SECONDS);
    }

    return;
}

/* RecorderApp_ReplayEvent: restart playback from zero. */
static void RecorderApp_ReplayEvent(lv_event_t *pEvent)
{
    if ((NULL != pEvent) && (LV_EVENT_CLICKED == lv_event_get_code(pEvent)))
    {
        (void)RECORDER_PlaybackRestart();
    }

    return;
}

/* RecorderApp_EndPlaybackEvent: stop playback and return to the file card. */
static void RecorderApp_EndPlaybackEvent(lv_event_t *pEvent)
{
    if ((NULL != pEvent) && (LV_EVENT_CLICKED == lv_event_get_code(pEvent)))
    {
        (void)RECORDER_PlaybackStop();
        RecorderApp_SetPage(RECORDER_APP_PAGE_IDLE);
    }

    return;
}

/* RecorderApp_BackEvent: navigate within the recorder, then leave from its idle page. */
static void RecorderApp_BackEvent(lv_event_t *pEvent)
{
    if ((NULL == pEvent) || (LV_EVENT_CLICKED != lv_event_get_code(pEvent)))
    {
        return;
    }
    if (RECORDER_APP_PAGE_RECORD == l_tRecorderUi.ePage)
    {
        (void)RECORDER_Stop();
        return;
    }
    if (RECORDER_APP_PAGE_PLAYBACK == l_tRecorderUi.ePage)
    {
        (void)RECORDER_PlaybackStop();
        RecorderApp_SetPage(RECORDER_APP_PAGE_IDLE);
        return;
    }
    if (RT_EOK != gui_app_goback())
    {
        (void)gui_app_run("Main");
    }

    return;
}

/* RecorderApp_PlaybackStateText: map playback state to concise UI text. */
static const char *RecorderApp_PlaybackStateText(
    RECORDER_PLAYBACK_STATE eState)
{
    switch (eState)
    {
    case RECORDER_PLAYBACK_STATE_STARTING:
        return "Opening audio";
    case RECORDER_PLAYBACK_STATE_PLAYING:
        return "Playing";
    case RECORDER_PLAYBACK_STATE_PAUSED:
        return "Paused";
    case RECORDER_PLAYBACK_STATE_ENDED:
        return "Playback complete";
    case RECORDER_PLAYBACK_STATE_ERROR:
        return "Playback error";
    case RECORDER_PLAYBACK_STATE_IDLE:
    default:
        return "Ready";
    }
}

/* RecorderApp_UpdateRecordWaveform: animate a bounded synthetic level display. */
static void RecorderApp_UpdateRecordWaveform(bool bActive)
{
    uint32_t ulPhase;
    lv_coord_t lHeight;
    uint8_t ucIndex;

    ulPhase = rt_tick_get_millisecond() / RECORDER_APP_REFRESH_MS;
    for (ucIndex = 0U;
         ucIndex < RECORDER_APP_WAVE_BAR_COUNT;
         ucIndex++)
    {
        if (NULL == l_tRecorderUi.aRecordWaveBars[ucIndex])
        {
            continue;
        }
        lHeight = bActive ?
            (lv_coord_t)(10U + ((ucIndex * 19U + ulPhase * 11U) % 48U)) :
            (lv_coord_t)10;
        lv_obj_set_y(l_tRecorderUi.aRecordWaveBars[ucIndex],
                     (RECORDER_APP_WAVE_CARD_HEIGHT - lHeight) / 2);
        lv_obj_set_height(l_tRecorderUi.aRecordWaveBars[ucIndex], lHeight);
        lv_obj_set_style_bg_color(
            l_tRecorderUi.aRecordWaveBars[ucIndex],
            lv_color_hex(bActive ? RECORDER_APP_COLOR_PRIMARY :
                         RECORDER_APP_COLOR_DISABLED),
            0);
    }

    return;
}

/* RecorderApp_UpdatePlaybackWaveform: color bars and move the playback cursor. */
static void RecorderApp_UpdatePlaybackWaveform(
    const RECORDER_SNAPSHOT *pSnapshot)
{
    uint32_t ulProgress;
    lv_coord_t lCursorX;
    uint8_t ucIndex;

    if (NULL == pSnapshot)
    {
        return;
    }
    ulProgress = 0U;
    if (0U != pSnapshot->ulPlaybackDurationSeconds)
    {
        ulProgress = (pSnapshot->ulPlaybackSeconds *
                      RECORDER_APP_WAVE_BAR_COUNT) /
                     pSnapshot->ulPlaybackDurationSeconds;
        if (RECORDER_APP_WAVE_BAR_COUNT < ulProgress)
        {
            ulProgress = RECORDER_APP_WAVE_BAR_COUNT;
        }
    }
    for (ucIndex = 0U;
         ucIndex < RECORDER_APP_WAVE_BAR_COUNT;
         ucIndex++)
    {
        if (NULL != l_tRecorderUi.aPlaybackWaveBars[ucIndex])
        {
            lv_obj_set_style_bg_color(
                l_tRecorderUi.aPlaybackWaveBars[ucIndex],
                lv_color_hex((ucIndex < ulProgress) ?
                             RECORDER_APP_COLOR_PRIMARY :
                             RECORDER_APP_COLOR_TEXT),
                0);
        }
    }
    if (NULL != l_tRecorderUi.pPlaybackCursor)
    {
        lCursorX = (lv_coord_t)(RECORDER_APP_WAVE_CURSOR_START_X +
            ((ulProgress * RECORDER_APP_WAVE_CURSOR_TRAVEL) /
             RECORDER_APP_WAVE_BAR_COUNT));
        lv_obj_set_x(l_tRecorderUi.pPlaybackCursor, lCursorX);
    }

    return;
}

/* RecorderApp_Refresh: render service state without blocking audio or storage workers. */
static void RecorderApp_Refresh(void)
{
    RECORDER_SNAPSHOT tSnapshot;
    RECORDER_FILE_INFO tFileInfo;
    uint16_t usFileCount;
    uint8_t ucIndex;
    bool bCardReady;

    if ((NULL == l_tRecorderUi.pRoot) ||
        (RT_EOK != RECORDER_GetSnapshot(&tSnapshot)))
    {
        return;
    }

    if (RecorderApp_RecordStateActive(tSnapshot.eRecordState))
    {
        RecorderApp_SetPage(RECORDER_APP_PAGE_RECORD);
    }
    else if (RecorderApp_PlaybackStateActive(tSnapshot.ePlaybackState))
    {
        RecorderApp_SetPage(RECORDER_APP_PAGE_PLAYBACK);
    }
    else if (RECORDER_APP_PAGE_RECORD == l_tRecorderUi.ePage)
    {
        RecorderApp_SetPage(RECORDER_APP_PAGE_IDLE);
        if ((RECORDER_RECORD_STATE_ERROR == tSnapshot.eRecordState) &&
            (0 != tSnapshot.lLastError))
        {
            RecorderApp_ShowIdleError("Record", tSnapshot.lLastError);
        }
    }

    for (ucIndex = 0U; ucIndex < RECORDER_FORMAT_COUNT; ucIndex++)
    {
        if (NULL == l_tRecorderUi.aFormatButtons[ucIndex])
        {
            continue;
        }
        lv_obj_set_style_bg_color(
            l_tRecorderUi.aFormatButtons[ucIndex],
            lv_color_hex((ucIndex == l_tRecorderUi.eSelectedFormat) ?
                         RECORDER_APP_COLOR_PRIMARY :
                         RECORDER_APP_COLOR_PANEL_LIGHT),
            0);
        lv_obj_set_style_border_color(
            l_tRecorderUi.aFormatButtons[ucIndex],
            lv_color_hex((ucIndex == l_tRecorderUi.eSelectedFormat) ?
                         RECORDER_APP_COLOR_PRIMARY :
                         RECORDER_APP_COLOR_DISABLED),
            0);
    }

    bCardReady = TF_CARD_IsMounted() || TF_CARD_IsInserted();
    if (0U < l_tRecorderUi.usStartCooldownTicks)
    {
        l_tRecorderUi.usStartCooldownTicks--;
    }
    if ((false == bCardReady) ||
            (0U < l_tRecorderUi.usStartCooldownTicks) ||
            (RECORDER_RECORD_STATE_STARTING == tSnapshot.eRecordState))
    {
        lv_obj_add_state(l_tRecorderUi.pIdleRecordButton, LV_STATE_DISABLED);
    }
    else
    {
        lv_obj_clear_state(l_tRecorderUi.pIdleRecordButton,
                           LV_STATE_DISABLED);
    }
    if (0U < l_tRecorderUi.usIdleStatusHoldTicks)
    {
        l_tRecorderUi.usIdleStatusHoldTicks--;
    }
    else if (bCardReady)
    {
        lv_label_set_text_fmt(l_tRecorderUi.pIdleStatusLabel,
                              "TF card ready  |  %s",
                              RecorderApp_FormatName(
                                  l_tRecorderUi.eSelectedFormat));
    }
    else
    {
        lv_label_set_text(l_tRecorderUi.pIdleStatusLabel,
                          "Insert TF card to record");
    }

    usFileCount = RECORDER_GetFileCount();
    if (usFileCount != l_tRecorderUi.usKnownFileCount)
    {
        l_tRecorderUi.usKnownFileCount = usFileCount;
        l_tRecorderUi.usSelectedFile = 0U;
    }
    if ((0U < usFileCount) &&
        (RT_EOK == RECORDER_GetFile(l_tRecorderUi.usSelectedFile,
                                    &tFileInfo)))
    {
        lv_label_set_text(l_tRecorderUi.pIdleFileLabel, tFileInfo.aName);
        lv_label_set_text_fmt(l_tRecorderUi.pIdleFileMetaLabel,
                              "%s  |  %lu KB",
                              RecorderApp_FormatName(tFileInfo.eFormat),
                              (unsigned long)(tFileInfo.ulSizeBytes / 1024U));
        lv_label_set_text_fmt(l_tRecorderUi.pIdleFileCounterLabel,
                              "%u / %u",
                              (unsigned int)(l_tRecorderUi.usSelectedFile + 1U),
                              (unsigned int)usFileCount);
        lv_label_set_text(l_tRecorderUi.pPlaybackFileLabel, tFileInfo.aName);
        lv_label_set_text_fmt(l_tRecorderUi.pPlaybackHeaderLabel,
                              "VOICE %u  |  %s",
                              (unsigned int)(l_tRecorderUi.usSelectedFile + 1U),
                              RecorderApp_FormatName(tFileInfo.eFormat));
    }
    else
    {
        lv_label_set_text(l_tRecorderUi.pIdleFileLabel,
                          bCardReady ? "No recordings yet" : "TF card unavailable");
        lv_label_set_text(l_tRecorderUi.pIdleFileMetaLabel,
                          "Tap the red button to create one");
        lv_label_set_text(l_tRecorderUi.pIdleFileCounterLabel, "0 / 0");
        lv_label_set_text(l_tRecorderUi.pPlaybackFileLabel, "Recording");
        lv_label_set_text(l_tRecorderUi.pPlaybackHeaderLabel, "LOCAL PLAYBACK");
    }

    if (NULL != l_tRecorderUi.pRecordHeaderLabel)
    {
        lv_label_set_text_fmt(
            l_tRecorderUi.pRecordHeaderLabel,
            "%s  |  %s",
            (RECORDER_RECORD_STATE_PAUSED == tSnapshot.eRecordState) ?
                "PAUSED" :
                ((RECORDER_RECORD_STATE_STOPPING == tSnapshot.eRecordState) ?
                    "SAVING" : "RECORDING"),
            RecorderApp_FormatName(tSnapshot.eRecordFormat));
    }
    lv_label_set_text_fmt(l_tRecorderUi.pRecordTimeLabel,
                          "%02lu:%02lu",
                          (unsigned long)(tSnapshot.ulRecordSeconds / 60U),
                          (unsigned long)(tSnapshot.ulRecordSeconds % 60U));
    lv_label_set_text_fmt(l_tRecorderUi.pRecordSizeLabel,
                          "%lu KB saved",
                          (unsigned long)(tSnapshot.ulFileSizeBytes / 1024U));
    if (NULL != l_tRecorderUi.pRecordPauseImage)
    {
        lv_img_set_src(
            l_tRecorderUi.pRecordPauseImage,
            (RECORDER_RECORD_STATE_PAUSED == tSnapshot.eRecordState) ?
                LV_EXT_IMG_GET(recorder_icon_play) :
                LV_EXT_IMG_GET(recorder_icon_pause));
    }
    RecorderApp_UpdateRecordWaveform(
        RECORDER_RECORD_STATE_RECORDING == tSnapshot.eRecordState);

    lv_label_set_text(l_tRecorderUi.pPlaybackStateLabel,
                      RecorderApp_PlaybackStateText(
                          tSnapshot.ePlaybackState));
    lv_label_set_text_fmt(l_tRecorderUi.pPlaybackTimeLabel,
                          "%02lu:%02lu   /   %02lu:%02lu",
                          (unsigned long)(tSnapshot.ulPlaybackSeconds / 60U),
                          (unsigned long)(tSnapshot.ulPlaybackSeconds % 60U),
                          (unsigned long)(
                              tSnapshot.ulPlaybackDurationSeconds / 60U),
                          (unsigned long)(
                              tSnapshot.ulPlaybackDurationSeconds % 60U));
    if (NULL != l_tRecorderUi.pPlaybackActionImage)
    {
        lv_img_set_src(
            l_tRecorderUi.pPlaybackActionImage,
            (RECORDER_PLAYBACK_STATE_PLAYING ==
             tSnapshot.ePlaybackState) ?
                LV_EXT_IMG_GET(recorder_icon_pause) :
                LV_EXT_IMG_GET(recorder_icon_play));
    }
    RecorderApp_UpdatePlaybackWaveform(&tSnapshot);

    return;
}

/* RecorderApp_TimerCallback: refresh page state at a bounded rate. */
static void RecorderApp_TimerCallback(lv_timer_t *pTimer)
{
    (void)pTimer;
    RecorderApp_Refresh();

    return;
}

/* RecorderApp_CreateIdlePage: build format selection, record control, and file card. */
static void RecorderApp_CreateIdlePage(void)
{
    lv_obj_t *pCard;
    lv_obj_t *pLabel;
    uint8_t ucIndex;

    (void)RecorderApp_CreateImageButton(
        l_tRecorderUi.pIdlePanel, 18, 18,
        RECORDER_APP_SMALL_ICON_SIZE,
        LV_EXT_IMG_GET(recorder_icon_back),
        RecorderApp_BackEvent, NULL, NULL);
    pLabel = RecorderApp_CreateLabel(
        l_tRecorderUi.pIdlePanel, 150, 22, 220,
        FONT_SUBTITLE, RECORDER_APP_COLOR_TEXT, LV_TEXT_ALIGN_RIGHT);
    lv_label_set_text(pLabel, "VOICE MEMOS");

    for (ucIndex = 0U; ucIndex < RECORDER_FORMAT_COUNT; ucIndex++)
    {
        l_tRecorderUi.aFormatButtons[ucIndex] = RecorderApp_CreateTextButton(
            l_tRecorderUi.pIdlePanel,
            (lv_coord_t)(63 + (ucIndex * 91)), 72,
            RECORDER_APP_FORMAT_BUTTON_WIDTH,
            RECORDER_APP_FORMAT_BUTTON_HEIGHT,
            RecorderApp_FormatName((RECORDER_FORMAT)ucIndex),
            RecorderApp_FormatEvent,
            (void *)(uintptr_t)ucIndex);
    }

    l_tRecorderUi.pIdleRecordButton = RecorderApp_CreateImageButton(
        l_tRecorderUi.pIdlePanel, 142, 120,
        106, LV_EXT_IMG_GET(recorder_icon_record),
        RecorderApp_StartEvent, NULL, NULL);
    l_tRecorderUi.pIdleStatusLabel = RecorderApp_CreateLabel(
        l_tRecorderUi.pIdlePanel, 24, 238, 342,
        FONT_NORMAL, RECORDER_APP_COLOR_MUTED, LV_TEXT_ALIGN_CENTER);

    pCard = lv_obj_create(l_tRecorderUi.pIdlePanel);
    lv_obj_set_pos(pCard, 18, 282);
    lv_obj_set_size(pCard, 354, 140);
    lv_obj_set_style_bg_color(
        pCard, lv_color_hex(RECORDER_APP_COLOR_PANEL_LIGHT), 0);
    lv_obj_set_style_bg_opa(pCard, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(pCard, 0, 0);
    lv_obj_set_style_radius(pCard, 24, 0);
    lv_obj_set_style_pad_all(pCard, 0, 0);
    lv_obj_clear_flag(pCard, LV_OBJ_FLAG_SCROLLABLE);

    l_tRecorderUi.pIdleFileLabel = RecorderApp_CreateLabel(
        pCard, 64, 18, 166,
        FONT_NORMAL, RECORDER_APP_COLOR_TEXT, LV_TEXT_ALIGN_LEFT);
    lv_label_set_long_mode(l_tRecorderUi.pIdleFileLabel, LV_LABEL_LONG_DOT);
    l_tRecorderUi.pIdleFileMetaLabel = RecorderApp_CreateLabel(
        pCard, 64, 54, 166,
        FONT_SMALL, RECORDER_APP_COLOR_MUTED, LV_TEXT_ALIGN_LEFT);
    l_tRecorderUi.pIdleFileCounterLabel = RecorderApp_CreateLabel(
        pCard, 64, 94, 166,
        FONT_SMALL, RECORDER_APP_COLOR_BLUE, LV_TEXT_ALIGN_LEFT);
    (void)RecorderApp_CreateImageButton(
        pCard, 6, 44, RECORDER_APP_SMALL_ICON_SIZE,
        LV_EXT_IMG_GET(recorder_icon_rewind),
        RecorderApp_SelectPreviousEvent, NULL, NULL);
    (void)RecorderApp_CreateImageButton(
        pCard, 234, 44, RECORDER_APP_SMALL_ICON_SIZE,
        LV_EXT_IMG_GET(recorder_icon_forward),
        RecorderApp_SelectNextEvent, NULL, NULL);
    (void)RecorderApp_CreateImageButton(
        pCard, 294, 44, RECORDER_APP_SMALL_ICON_SIZE,
        LV_EXT_IMG_GET(recorder_icon_play),
        RecorderApp_OpenPlaybackEvent, NULL, NULL);

    return;
}

/* RecorderApp_CreateRecordPage: build recording status, waveform, and pause/stop controls. */
static void RecorderApp_CreateRecordPage(void)
{
    lv_obj_t *pLabel;

    (void)RecorderApp_CreateImageButton(
        l_tRecorderUi.pRecordPanel, 18, 18,
        RECORDER_APP_SMALL_ICON_SIZE,
        LV_EXT_IMG_GET(recorder_icon_close),
        RecorderApp_StopRecordEvent, NULL, NULL);
    pLabel = RecorderApp_CreateLabel(
        l_tRecorderUi.pRecordPanel, 150, 18, 220,
        FONT_SUBTITLE, RECORDER_APP_COLOR_TEXT, LV_TEXT_ALIGN_RIGHT);
    lv_label_set_text(pLabel, "VOICE MEMO");
    l_tRecorderUi.pRecordHeaderLabel = RecorderApp_CreateLabel(
        l_tRecorderUi.pRecordPanel, 130, 51, 240,
        FONT_NORMAL, RECORDER_APP_COLOR_PRIMARY, LV_TEXT_ALIGN_RIGHT);

    RecorderApp_CreateWaveform(
        l_tRecorderUi.pRecordPanel, 105,
        RECORDER_APP_COLOR_PRIMARY,
        l_tRecorderUi.aRecordWaveBars);
    l_tRecorderUi.pRecordTimeLabel = RecorderApp_CreateLabel(
        l_tRecorderUi.pRecordPanel, 24, 240, 342,
        FONT_BIGL, RECORDER_APP_COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
    l_tRecorderUi.pRecordSizeLabel = RecorderApp_CreateLabel(
        l_tRecorderUi.pRecordPanel, 24, 288, 342,
        FONT_SMALL, RECORDER_APP_COLOR_MUTED, LV_TEXT_ALIGN_CENTER);

    (void)RecorderApp_CreateImageButton(
        l_tRecorderUi.pRecordPanel, 82, 338,
        RECORDER_APP_ICON_SIZE,
        LV_EXT_IMG_GET(recorder_icon_pause),
        RecorderApp_PauseEvent, NULL,
        &l_tRecorderUi.pRecordPauseImage);
    (void)RecorderApp_CreateImageButton(
        l_tRecorderUi.pRecordPanel, 234, 338,
        RECORDER_APP_ICON_SIZE,
        LV_EXT_IMG_GET(recorder_icon_stop),
        RecorderApp_StopRecordEvent, NULL, NULL);

    return;
}

/* RecorderApp_CreatePlaybackPage: build local playback scrubber and all required controls. */
static void RecorderApp_CreatePlaybackPage(void)
{
    lv_obj_t *pLabel;

    (void)RecorderApp_CreateImageButton(
        l_tRecorderUi.pPlaybackPanel, 18, 18,
        RECORDER_APP_SMALL_ICON_SIZE,
        LV_EXT_IMG_GET(recorder_icon_back),
        RecorderApp_BackEvent, NULL, NULL);
    l_tRecorderUi.pPlaybackHeaderLabel = RecorderApp_CreateLabel(
        l_tRecorderUi.pPlaybackPanel, 140, 18, 230,
        FONT_NORMAL, RECORDER_APP_COLOR_PRIMARY, LV_TEXT_ALIGN_RIGHT);
    l_tRecorderUi.pPlaybackFileLabel = RecorderApp_CreateLabel(
        l_tRecorderUi.pPlaybackPanel, 40, 62, 310,
        FONT_NORMAL, RECORDER_APP_COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
    lv_label_set_long_mode(l_tRecorderUi.pPlaybackFileLabel, LV_LABEL_LONG_DOT);

    RecorderApp_CreateWaveform(
        l_tRecorderUi.pPlaybackPanel, 96,
        RECORDER_APP_COLOR_TEXT,
        l_tRecorderUi.aPlaybackWaveBars);
    l_tRecorderUi.pPlaybackCursor = lv_obj_create(
        l_tRecorderUi.pPlaybackPanel);
    lv_obj_set_pos(l_tRecorderUi.pPlaybackCursor,
                   RECORDER_APP_WAVE_CURSOR_START_X, 102);
    lv_obj_set_size(l_tRecorderUi.pPlaybackCursor, 4, 106);
    lv_obj_set_style_bg_color(
        l_tRecorderUi.pPlaybackCursor,
        lv_color_hex(RECORDER_APP_COLOR_BLUE), 0);
    lv_obj_set_style_bg_opa(
        l_tRecorderUi.pPlaybackCursor, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(l_tRecorderUi.pPlaybackCursor, 0, 0);
    lv_obj_set_style_radius(
        l_tRecorderUi.pPlaybackCursor, LV_RADIUS_CIRCLE, 0);

    l_tRecorderUi.pPlaybackTimeLabel = RecorderApp_CreateLabel(
        l_tRecorderUi.pPlaybackPanel, 24, 226, 342,
        FONT_SUBTITLE, RECORDER_APP_COLOR_TEXT, LV_TEXT_ALIGN_CENTER);
    l_tRecorderUi.pPlaybackStateLabel = RecorderApp_CreateLabel(
        l_tRecorderUi.pPlaybackPanel, 24, 260, 342,
        FONT_SMALL, RECORDER_APP_COLOR_MUTED, LV_TEXT_ALIGN_CENTER);

    (void)RecorderApp_CreateImageButton(
        l_tRecorderUi.pPlaybackPanel, 42, 286,
        RECORDER_APP_ICON_SIZE,
        LV_EXT_IMG_GET(recorder_icon_rewind),
        RecorderApp_RewindEvent, NULL, NULL);
    (void)RecorderApp_CreateImageButton(
        l_tRecorderUi.pPlaybackPanel, 158, 286,
        RECORDER_APP_ICON_SIZE,
        LV_EXT_IMG_GET(recorder_icon_play),
        RecorderApp_PlayActionEvent, NULL,
        &l_tRecorderUi.pPlaybackActionImage);
    (void)RecorderApp_CreateImageButton(
        l_tRecorderUi.pPlaybackPanel, 274, 286,
        RECORDER_APP_ICON_SIZE,
        LV_EXT_IMG_GET(recorder_icon_forward),
        RecorderApp_ForwardEvent, NULL, NULL);

    (void)RecorderApp_CreateImageButton(
        l_tRecorderUi.pPlaybackPanel, 78, 366,
        RECORDER_APP_SMALL_ICON_SIZE,
        LV_EXT_IMG_GET(recorder_icon_rewind),
        RecorderApp_ReplayEvent, NULL, NULL);
    pLabel = RecorderApp_CreateLabel(
        l_tRecorderUi.pPlaybackPanel, 38, 420, 132,
        FONT_SMALL, RECORDER_APP_COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(pLabel, "REPLAY");
    (void)RecorderApp_CreateImageButton(
        l_tRecorderUi.pPlaybackPanel, 260, 366,
        RECORDER_APP_SMALL_ICON_SIZE,
        LV_EXT_IMG_GET(recorder_icon_stop),
        RecorderApp_EndPlaybackEvent, NULL, NULL);
    pLabel = RecorderApp_CreateLabel(
        l_tRecorderUi.pPlaybackPanel, 220, 420, 132,
        FONT_SMALL, RECORDER_APP_COLOR_MUTED, LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(pLabel, "END");

    return;
}

/* RecorderApp_OnStart: initialize the three state panels and scan the TF recording folder. */
static void RecorderApp_OnStart(void)
{
    rt_err_t tResult;

    rt_memset(&l_tRecorderUi, 0, sizeof(l_tRecorderUi));
    l_tRecorderUi.eSelectedFormat = RECORDER_FORMAT_OPUS;
    l_tRecorderUi.ePage = RECORDER_APP_PAGE_IDLE;
    l_tRecorderUi.pRoot = lv_obj_create(lv_scr_act());
    if (NULL == l_tRecorderUi.pRoot)
    {
        return;
    }
    lv_obj_set_pos(l_tRecorderUi.pRoot, 0, 0);
    lv_obj_set_size(l_tRecorderUi.pRoot,
                    RECORDER_APP_SCREEN_WIDTH,
                    RECORDER_APP_SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(
        l_tRecorderUi.pRoot,
        lv_color_hex(RECORDER_APP_COLOR_BACKGROUND), 0);
    lv_obj_set_style_bg_opa(l_tRecorderUi.pRoot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(l_tRecorderUi.pRoot, 0, 0);
    lv_obj_set_style_pad_all(l_tRecorderUi.pRoot, 0, 0);
    lv_obj_clear_flag(l_tRecorderUi.pRoot, LV_OBJ_FLAG_SCROLLABLE);

    l_tRecorderUi.pIdlePanel = RecorderApp_CreatePanel(l_tRecorderUi.pRoot);
    l_tRecorderUi.pRecordPanel = RecorderApp_CreatePanel(l_tRecorderUi.pRoot);
    l_tRecorderUi.pPlaybackPanel = RecorderApp_CreatePanel(l_tRecorderUi.pRoot);
    if ((NULL == l_tRecorderUi.pIdlePanel) ||
        (NULL == l_tRecorderUi.pRecordPanel) ||
        (NULL == l_tRecorderUi.pPlaybackPanel))
    {
        return;
    }

    RecorderApp_CreateIdlePage();
    RecorderApp_CreateRecordPage();
    RecorderApp_CreatePlaybackPage();
    RecorderApp_SetPage(RECORDER_APP_PAGE_IDLE);

    tResult = RECORDER_RefreshFiles();
    if (RT_EOK != tResult)
    {
        RecorderApp_ShowIdleError("TF card", tResult);
    }
    RecorderApp_Refresh();
    l_tRecorderUi.pRefreshTimer = lv_timer_create(
        RecorderApp_TimerCallback,
        RECORDER_APP_REFRESH_MS,
        NULL);

    return;
}

/* RecorderApp_OnStop: stop active audio operations and release all LVGL objects. */
static void RecorderApp_OnStop(void)
{
    RECORDER_SNAPSHOT tSnapshot;

    if (NULL != l_tRecorderUi.pRefreshTimer)
    {
        lv_timer_del(l_tRecorderUi.pRefreshTimer);
        l_tRecorderUi.pRefreshTimer = NULL;
    }
    if ((RT_EOK == RECORDER_GetSnapshot(&tSnapshot)) &&
        RecorderApp_RecordStateActive(tSnapshot.eRecordState))
    {
        (void)RECORDER_Stop();
    }
    (void)RECORDER_PlaybackStop();
    if (NULL != l_tRecorderUi.pRoot)
    {
        lv_obj_del(l_tRecorderUi.pRoot);
    }
    rt_memset(&l_tRecorderUi, 0, sizeof(l_tRecorderUi));

    return;
}

/* RecorderApp_MessageHandler: process recorder app lifecycle messages. */
static void RecorderApp_MessageHandler(gui_app_msg_type_t eMessage,
                                       void *pParameter)
{
    (void)pParameter;
    if (GUI_APP_MSG_ONSTART == eMessage)
    {
        RecorderApp_OnStart();
    }
    else if (GUI_APP_MSG_ONSTOP == eMessage)
    {
        RecorderApp_OnStop();
    }

    return;
}

/* RecorderApp_Main: register the recorder lifecycle handler. */
static int RecorderApp_Main(intent_t tIntent)
{
    (void)tIntent;
    gui_app_regist_msg_handler(APP_ID, RecorderApp_MessageHandler);

    return 0;
}

BUILTIN_APP_EXPORT(
    LV_EXT_STR_ID(recorder),
    LV_EXT_IMG_GET(img_recorder),
    APP_ID,
    RecorderApp_Main);
