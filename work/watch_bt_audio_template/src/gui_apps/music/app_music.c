#include <rtthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "littlevgl2rtt.h"
#include "lv_ext_resource_manager.h"
#include "gui_app_fwk.h"
#ifdef BSP_USING_PC_SIMULATOR
typedef enum _PCSIM_AMS_COMMAND
{
    BLE_AMS_CMD_TOGGLE_PLAY_PAUSE = 2,
    BLE_AMS_CMD_NEXT = 3,
    BLE_AMS_CMD_PREV = 4,
    BLE_AMS_CMD_VOL_UP = 5,
    BLE_AMS_CMD_VOL_DOWN = 6
} PCSIM_AMS_COMMAND;

uint8_t audio_server_get_max_volume(void);
#else
    #include "bf0_ble_ams.h"
    #include "audio_server.h"
#endif /* BSP_USING_PC_SIMULATOR */
#include "ble_ios_services.h"
#include "local_music_player.h"
#include "watch_settings.h"

#define APP_ID                          "music"
#define MUSIC_REFRESH_MS                (400U)
#define MUSIC_PAGE_MARGIN               (18)
#define MUSIC_PROGRESS_RANGE            (1000)
#define MUSIC_PHONE_VOLUME_MAX          (100U)
#define MUSIC_INVALID_GENERATION        (0xFFFFFFFFUL)
#define MUSIC_FONT_CAPTION              (16U)
#define MUSIC_FONT_BODY                 (20U)
#define MUSIC_FONT_SUBTITLE             (24U)
#define MUSIC_FONT_TITLE                (28U)
#define MUSIC_CONTROL_ICON_WIDTH         (76)
#define MUSIC_PLAY_ICON_WIDTH            (96)
#define MUSIC_VOLUME_ICON_WIDTH          (48)

LV_IMG_DECLARE(music_bg_now_listening);
LV_IMG_DECLARE(music_bg_row_dark);
LV_IMG_DECLARE(music_bg_blur_full);
LV_IMG_DECLARE(music_icon_cancel);
LV_IMG_DECLARE(music_icon_previous);
LV_IMG_DECLARE(music_icon_next);
LV_IMG_DECLARE(music_icon_play);
LV_IMG_DECLARE(music_icon_pause);
LV_IMG_DECLARE(music_icon_not_playing);
LV_IMG_DECLARE(music_icon_playing);
LV_IMG_DECLARE(music_icon_more);
LV_IMG_DECLARE(music_icon_back);
LV_IMG_DECLARE(music_icon_now_listening);
LV_IMG_DECLARE(music_icon_library);
LV_IMG_DECLARE(music_icon_phone_music);
LV_IMG_DECLARE(music_icon_playlist);
LV_IMG_DECLARE(music_icon_artist);
LV_IMG_DECLARE(music_icon_album);
LV_IMG_DECLARE(music_icon_song);
LV_IMG_DECLARE(music_icon_list_play);
LV_IMG_DECLARE(music_icon_remove);
LV_IMG_DECLARE(music_icon_shuffle);
LV_IMG_DECLARE(music_icon_repeat);
LV_IMG_DECLARE(music_icon_repeat_one);
LV_IMG_DECLARE(music_icon_go_artist);
LV_IMG_DECLARE(music_icon_go_album);
LV_IMG_DECLARE(music_icon_phone_play);
LV_IMG_DECLARE(music_icon_watch_play);
LV_IMG_DECLARE(music_icon_volume_down);
LV_IMG_DECLARE(music_icon_volume_up);
LV_IMG_DECLARE(music_icon_playlist_empty);
LV_IMG_DECLARE(music_icon_artist_empty);
LV_IMG_DECLARE(music_icon_song_empty);
LV_IMG_DECLARE(music_icon_album_default);
LV_IMG_DECLARE(music_progress_track);
LV_IMG_DECLARE(music_progress_value);

typedef enum _MUSIC_PAGE
{
    MUSIC_PAGE_SOURCE = 0,
    MUSIC_PAGE_PLAYER,
    MUSIC_PAGE_VOLUME,
    MUSIC_PAGE_NOW_LISTENING,
    MUSIC_PAGE_LIBRARY,
    MUSIC_PAGE_PLAYLISTS,
    MUSIC_PAGE_ARTISTS,
    MUSIC_PAGE_ARTIST_ALBUMS,
    MUSIC_PAGE_ALBUM,
    MUSIC_PAGE_ALBUM_TRACKS,
    MUSIC_PAGE_MORE,
    MUSIC_PAGE_REMOVE_CONFIRM,
    MUSIC_PAGE_PHONE_CONTROL
} MUSIC_PAGE;

typedef enum _MUSIC_SOURCE
{
    MUSIC_SOURCE_NONE = 0,
    MUSIC_SOURCE_PHONE,
    MUSIC_SOURCE_LOCAL
} MUSIC_SOURCE;

typedef enum _MUSIC_ACTION
{
    MUSIC_ACTION_PREVIOUS = 1,
    MUSIC_ACTION_TOGGLE,
    MUSIC_ACTION_NEXT,
    MUSIC_ACTION_VOLUME_DOWN,
    MUSIC_ACTION_VOLUME_UP
} MUSIC_ACTION;

/* MUSIC_UI: bounded LVGL state for one music application instance.
 * Members:
 *   - pRoot: persistent full-screen gesture target.
 *   - pTrack/pArtist/pDetail: metadata labels on the player page.
 *   - pProgress/pElapsed/pDuration: phone playback progress widgets.
 *   - pPlayLabel: center control image, updated from playback state.
 *   - pVolume/pVolumeLabel: volume-page widgets.
 *   - pStatus: bounded feedback label for command failures and hints.
 *   - pRefreshTimer: 400 ms UI-thread refresh timer.
 *   - ePage/eSource: current navigation and selected playback source.
 *   - ulRenderedAmsCount: last rendered AMS notification generation.
 *   - ulPhoneBaseTick/ulPhoneElapsed/ulPhoneDuration: extrapolated phone
 *     progress state; elapsed time is advanced only while AMS says playing.
 *   - ucPhonePlaybackState/ucPhoneVolume: parsed AMS player state and volume.
 *   - eRenderedLocalState/ucRenderedLocalVolume: local refresh cache.
 */
typedef struct _MUSIC_UI
{
    lv_obj_t *pRoot;
    lv_obj_t *pTrack;
    lv_obj_t *pArtist;
    lv_obj_t *pDetail;
    lv_obj_t *pProgress;
    lv_obj_t *pElapsed;
    lv_obj_t *pDuration;
    lv_obj_t *pPlayLabel;
    lv_obj_t *pVolume;
    lv_obj_t *pVolumeLabel;
    lv_obj_t *pStatus;
    lv_timer_t *pRefreshTimer;
    MUSIC_PAGE ePage;
    MUSIC_SOURCE eSource;
    uint32_t ulRenderedAmsCount;
    uint32_t ulPhoneBaseTick;
    uint32_t ulPhoneElapsed;
    uint32_t ulPhoneDuration;
    uint8_t ucPhonePlaybackState;
    uint8_t ucPhoneVolume;
    LOCAL_MUSIC_STATE eRenderedLocalState;
    uint8_t ucRenderedLocalVolume;
    MUSIC_PAGE eReturnPage;
    MUSIC_PAGE eMenuReturnPage;
    uint8_t ucPlayMode;
    bool bTrackRemoved;
} MUSIC_UI;

/* Module-local UI state. All LVGL members are accessed only on the GUI task. */
static MUSIC_UI l_tMusicUi;

static void MUSIC_RenderPage(MUSIC_PAGE ePage);
static void MUSIC_Refresh(void);
static void MUSIC_BackEvent(lv_event_t *pEvent);

/* MUSIC_GetFont: obtain an already registered theme font without repeatedly
 * traversing the FreeType font registry while a page is being constructed.
 * Parameter:
 *   - ucFontSize: requested font pixel size from MUSIC_FONT_*.
 * Return value: registered theme font matching the requested size.
 */
static const lv_font_t *MUSIC_GetFont(uint8_t ucFontSize)
{
    const lv_font_t *pFont;

    if (MUSIC_FONT_TITLE <= ucFontSize)
    {
        pFont = lv_theme_get_font_title(NULL);
    }
    else if (MUSIC_FONT_SUBTITLE <= ucFontSize)
    {
        pFont = lv_theme_get_font_subtitle(NULL);
    }
    else if (MUSIC_FONT_BODY <= ucFontSize)
    {
        pFont = lv_theme_get_font_normal(NULL);
    }
    else
    {
        pFont = lv_theme_get_font_small(NULL);
    }

    return pFont;
}

static const char *MUSIC_TextOrFallback(const char *pText, const char *pFallback)
{
    if ((NULL != pText) && ('\0' != pText[0]))
    {
        return pText;
    }

    return pFallback;
}

static lv_obj_t *MUSIC_CreateLabel(
    lv_obj_t *pParent,
    const char *pText,
    lv_coord_t lX,
    lv_coord_t lY,
    lv_coord_t lWidth,
    lv_color_t tColor,
    lv_text_align_t eAlign)
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
    lv_obj_set_style_text_font(pLabel, MUSIC_GetFont(MUSIC_FONT_BODY), 0);

    return pLabel;
}

static lv_obj_t *MUSIC_CreateLabelSized(
    lv_obj_t *pParent,
    const char *pText,
    lv_coord_t lX,
    lv_coord_t lY,
    lv_coord_t lWidth,
    lv_color_t tColor,
    lv_text_align_t eAlign,
    uint8_t ucFontSize)
{
    lv_obj_t *pLabel;

    pLabel = MUSIC_CreateLabel(pParent, pText, lX, lY, lWidth, tColor,
                               eAlign);
    if (NULL != pLabel)
    {
        lv_obj_set_style_text_font(pLabel, MUSIC_GetFont(ucFontSize), 0);
    }

    return pLabel;
}

static lv_obj_t *MUSIC_CreatePanel(
    lv_obj_t *pParent,
    lv_coord_t lX,
    lv_coord_t lY,
    lv_coord_t lWidth,
    lv_coord_t lHeight,
    uint32_t ulColor,
    lv_coord_t lRadius)
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
    lv_obj_set_pos(pPanel, lX, lY);
    lv_obj_set_size(pPanel, lWidth, lHeight);
    lv_obj_set_style_bg_color(pPanel, lv_color_hex(ulColor), 0);
    lv_obj_set_style_bg_opa(pPanel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(pPanel, 0, 0);
    lv_obj_set_style_radius(pPanel, lRadius, 0);
    lv_obj_set_style_pad_all(pPanel, 0, 0);
    lv_obj_clear_flag(pPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(pPanel, LV_OBJ_FLAG_CLICKABLE);

    return pPanel;
}

static lv_obj_t *MUSIC_CreateImage(
    lv_obj_t *pParent,
    const void *pImage,
    lv_coord_t lX,
    lv_coord_t lY)
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

static lv_obj_t *MUSIC_CreateImageButton(
    lv_obj_t *pParent,
    const void *pImage,
    lv_coord_t lX,
    lv_coord_t lY,
    lv_event_cb_t pCallback,
    void *pUserData)
{
    lv_obj_t *pObject;

    pObject = MUSIC_CreateImage(pParent, pImage, lX, lY);
    if (NULL == pObject)
    {
        return NULL;
    }
    lv_obj_add_flag(pObject, LV_OBJ_FLAG_CLICKABLE);
    if (NULL != pCallback)
    {
        lv_obj_add_event_cb(pObject, pCallback, LV_EVENT_SHORT_CLICKED,
                            pUserData);
    }

    return pObject;
}

static void MUSIC_ClearPage(void)
{
    if (NULL != l_tMusicUi.pRoot)
    {
        lv_obj_clean(l_tMusicUi.pRoot);
    }
    l_tMusicUi.pTrack = NULL;
    l_tMusicUi.pArtist = NULL;
    l_tMusicUi.pDetail = NULL;
    l_tMusicUi.pProgress = NULL;
    l_tMusicUi.pElapsed = NULL;
    l_tMusicUi.pDuration = NULL;
    l_tMusicUi.pPlayLabel = NULL;
    l_tMusicUi.pVolume = NULL;
    l_tMusicUi.pVolumeLabel = NULL;
    l_tMusicUi.pStatus = NULL;

    return;
}

static void MUSIC_CreateHeader(const char *pTitle, const char *pSubtitle)
{
    MUSIC_CreateLabelSized(
        l_tMusicUi.pRoot,
        pTitle,
        MUSIC_PAGE_MARGIN,
        12,
        LV_HOR_RES_MAX - (MUSIC_PAGE_MARGIN * 2),
        lv_color_hex(0xF4F2FFU),
        LV_TEXT_ALIGN_LEFT,
        MUSIC_FONT_TITLE);
    MUSIC_CreateLabelSized(
        l_tMusicUi.pRoot,
        pSubtitle,
        MUSIC_PAGE_MARGIN,
        43,
        LV_HOR_RES_MAX - (MUSIC_PAGE_MARGIN * 2),
        lv_color_hex(0xAAA7C8U),
        LV_TEXT_ALIGN_LEFT,
        MUSIC_FONT_CAPTION);

    return;
}

static void MUSIC_SetStatus(const char *pText, lv_color_t tColor)
{
    if ((NULL == l_tMusicUi.pStatus) || (NULL == pText))
    {
        return;
    }

    lv_label_set_text(l_tMusicUi.pStatus, pText);
    lv_obj_set_style_text_color(l_tMusicUi.pStatus, tColor, 0);

    return;
}

static bool MUSIC_ParsePlayback(
    const char *pText,
    uint8_t *pState,
    uint32_t *pElapsed)
{
    char *pEnd;
    long lState;
    double dElapsed;

    if ((NULL == pText) || (NULL == pState) || (NULL == pElapsed))
    {
        return false;
    }

    lState = strtol(pText, &pEnd, 10);
    if ((pEnd == pText) || (',' != *pEnd) || (0 > lState) || (3 < lState))
    {
        return false;
    }
    (void)strtod(pEnd + 1, &pEnd);
    if (',' != *pEnd)
    {
        return false;
    }
    dElapsed = strtod(pEnd + 1, &pEnd);
    if ((0.0 > dElapsed) || (4294967295.0 < dElapsed))
    {
        return false;
    }

    *pState = (uint8_t)lState;
    *pElapsed = (uint32_t)dElapsed;
    return true;
}

static uint32_t MUSIC_ParseSeconds(const char *pText)
{
    char *pEnd;
    double dValue;

    if (NULL == pText)
    {
        return 0U;
    }
    dValue = strtod(pText, &pEnd);
    if ((pEnd == pText) || (0.0 > dValue) || (4294967295.0 < dValue))
    {
        return 0U;
    }

    return (uint32_t)dValue;
}

static uint8_t MUSIC_ParsePhoneVolume(const char *pText)
{
    char *pEnd;
    double dValue;

    if (NULL == pText)
    {
        return 0U;
    }
    dValue = strtod(pText, &pEnd);
    if (pEnd == pText)
    {
        return 0U;
    }
    if (0.0 > dValue)
    {
        dValue = 0.0;
    }
    if (1.0 < dValue)
    {
        dValue = 1.0;
    }

    return (uint8_t)((dValue * 100.0) + 0.5);
}

static void MUSIC_FormatTime(char *pBuffer, size_t ulLength, uint32_t ulSeconds)
{
    if ((NULL == pBuffer) || (0U == ulLength))
    {
        return;
    }

    (void)rt_snprintf(
        pBuffer,
        ulLength,
        "%lu:%02lu",
        (unsigned long)(ulSeconds / 60U),
        (unsigned long)(ulSeconds % 60U));

    return;
}

static bool MUSIC_IsPhonePlaying(void)
{
    return (1U == l_tMusicUi.ucPhonePlaybackState);
}

static bool MUSIC_IsLocalPlaying(LOCAL_MUSIC_STATE eState)
{
    return (LOCAL_MUSIC_STATE_PLAYING == eState);
}

static int MUSIC_RunAction(MUSIC_ACTION eAction)
{
    LOCAL_MUSIC_SNAPSHOT tLocal;
    uint8_t ucMaximumVolume;
    uint8_t ucVolume;

    if (MUSIC_SOURCE_PHONE == l_tMusicUi.eSource)
    {
        switch (eAction)
        {
        case MUSIC_ACTION_PREVIOUS:
            return ble_ios_services_send_ams_cmd(BLE_AMS_CMD_PREV);
        case MUSIC_ACTION_TOGGLE:
            return ble_ios_services_send_ams_cmd(BLE_AMS_CMD_TOGGLE_PLAY_PAUSE);
        case MUSIC_ACTION_NEXT:
            return ble_ios_services_send_ams_cmd(BLE_AMS_CMD_NEXT);
        case MUSIC_ACTION_VOLUME_DOWN:
            return ble_ios_services_send_ams_cmd(BLE_AMS_CMD_VOL_DOWN);
        case MUSIC_ACTION_VOLUME_UP:
            return ble_ios_services_send_ams_cmd(BLE_AMS_CMD_VOL_UP);
        default:
            return -RT_EINVAL;
        }
    }

    if (MUSIC_SOURCE_LOCAL != l_tMusicUi.eSource)
    {
        return -RT_EINVAL;
    }
    (void)local_music_get_snapshot(&tLocal);
    switch (eAction)
    {
    case MUSIC_ACTION_PREVIOUS:
    case MUSIC_ACTION_NEXT:
        return local_music_play_file(NULL, 0U);
    case MUSIC_ACTION_TOGGLE:
        if (LOCAL_MUSIC_STATE_PLAYING == tLocal.eState)
        {
            return local_music_pause();
        }
        if ((LOCAL_MUSIC_STATE_PAUSED == tLocal.eState) ||
            (LOCAL_MUSIC_STATE_SUSPENDED == tLocal.eState))
        {
            return local_music_resume();
        }
        return local_music_play_file(NULL, 0U);
    case MUSIC_ACTION_VOLUME_DOWN:
    case MUSIC_ACTION_VOLUME_UP:
        ucMaximumVolume = (uint8_t)audio_server_get_max_volume();
        ucVolume = watch_settings_get_local_volume();
        if ((MUSIC_ACTION_VOLUME_DOWN == eAction) && (0U < ucVolume))
        {
            ucVolume--;
        }
        else if ((MUSIC_ACTION_VOLUME_UP == eAction) &&
                 (ucVolume < ucMaximumVolume))
        {
            ucVolume++;
        }
        return watch_settings_set_local_volume(ucVolume);
    default:
        return -RT_EINVAL;
    }
}

static void MUSIC_ActionEvent(lv_event_t *pEvent)
{
    MUSIC_ACTION eAction;
    int lResult;

    if ((NULL == pEvent) ||
        (LV_EVENT_SHORT_CLICKED != lv_event_get_code(pEvent)))
    {
        return;
    }

    eAction = (MUSIC_ACTION)(uintptr_t)lv_event_get_user_data(pEvent);
    lResult = MUSIC_RunAction(eAction);
    if (RT_EOK == lResult)
    {
        MUSIC_SetStatus("Control sent", lv_color_hex(0x8DEBC0U));
    }
    else
    {
        MUSIC_SetStatus("Control unavailable", lv_color_hex(0xFF9AABU));
    }
    MUSIC_Refresh();

    return;
}

static lv_obj_t *MUSIC_CreateControlButton(
    const void *pImage,
    lv_coord_t lImageWidth,
    lv_coord_t lCenterX,
    lv_coord_t lY,
    MUSIC_ACTION eAction)
{
    if (NULL == pImage)
    {
        return NULL;
    }
    return MUSIC_CreateImageButton(
        l_tMusicUi.pRoot,
        pImage,
        lCenterX - (lImageWidth / 2),
        lY,
        MUSIC_ActionEvent,
        (void *)(uintptr_t)eAction);
}

static void MUSIC_RenderBackHeader(const char *pTitle)
{
    (void)MUSIC_CreateImageButton(l_tMusicUi.pRoot, LV_EXT_IMG_GET(music_icon_back),
                                  0, 0, MUSIC_BackEvent, NULL);
    MUSIC_CreateLabelSized(l_tMusicUi.pRoot, pTitle, 68, 18,
                           LV_HOR_RES_MAX - 142,
                           lv_color_hex(0xFF3B57U), LV_TEXT_ALIGN_LEFT,
                           MUSIC_FONT_TITLE);

    return;
}

static void MUSIC_RenderCloseHeader(const char *pTitle)
{
    (void)MUSIC_CreateImageButton(l_tMusicUi.pRoot, LV_EXT_IMG_GET(music_icon_cancel),
                                  0, 0, MUSIC_BackEvent, NULL);
    MUSIC_CreateLabelSized(l_tMusicUi.pRoot, pTitle, 68, 18,
                           LV_HOR_RES_MAX - 142,
                           lv_color_hex(0xFF3B57U), LV_TEXT_ALIGN_LEFT,
                           MUSIC_FONT_TITLE);

    return;
}

static lv_obj_t *MUSIC_CreateListRow(
    lv_coord_t lY,
    const void *pIcon,
    const char *pTitle,
    const char *pSubtitle,
    lv_event_cb_t pCallback,
    void *pUserData)
{
    bool bNowListening;
    lv_obj_t *pRow;

    bNowListening = (MUSIC_PAGE_SOURCE == l_tMusicUi.ePage);
#ifdef LV_USING_FILE_RESOURCE
    bNowListening = bNowListening &&
                    (0 == strcmp((const char *)LV_EXT_IMG_GET(music_icon_now_listening),
                                 (const char *)pIcon));
#else
    bNowListening = bNowListening &&
                    (LV_EXT_IMG_GET(music_icon_now_listening) == pIcon);
#endif
    pRow = lv_obj_create(l_tMusicUi.pRoot);
    if (NULL == pRow)
    {
        return NULL;
    }
    lv_obj_set_pos(pRow, 10, lY);
    lv_obj_set_size(pRow, 390, bNowListening ? 90 : 100);
    lv_obj_set_style_bg_color(
        pRow,
        bNowListening ? lv_color_hex(0xE63232U) : lv_color_hex(0x242227U),
        0);
    lv_obj_set_style_bg_opa(
        pRow,
        bNowListening ? LV_OPA_20 : LV_OPA_COVER,
        0);
    lv_obj_set_style_border_width(pRow, 0, 0);
    lv_obj_set_style_radius(pRow, 20, 0);
    lv_obj_set_style_pad_all(pRow, 0, 0);
    lv_obj_clear_flag(pRow, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(pRow, LV_OBJ_FLAG_CLICKABLE);
    if (NULL != pCallback)
    {
        lv_obj_add_event_cb(pRow, pCallback, LV_EVENT_SHORT_CLICKED,
                            pUserData);
    }
    if (NULL != pIcon)
    {
        (void)MUSIC_CreateImage(pRow, pIcon, 12, 26);
    }
    MUSIC_CreateLabelSized(pRow, pTitle, 70,
                           (NULL == pSubtitle) ? 34 : 17,
                           270, lv_color_hex(0xF6F4FFU),
                           LV_TEXT_ALIGN_LEFT, MUSIC_FONT_BODY);
    if (NULL != pSubtitle)
    {
        MUSIC_CreateLabelSized(pRow, pSubtitle, 70, 51, 270,
                               lv_color_hex(0x9A98A6U),
                               LV_TEXT_ALIGN_LEFT, MUSIC_FONT_CAPTION);
    }

    return pRow;
}

static void MUSIC_PageEvent(lv_event_t *pEvent)
{
    MUSIC_PAGE ePage;

    if ((NULL == pEvent) ||
        (LV_EVENT_SHORT_CLICKED != lv_event_get_code(pEvent)))
    {
        return;
    }
    ePage = (MUSIC_PAGE)(uintptr_t)lv_event_get_user_data(pEvent);
    MUSIC_RenderPage(ePage);

    return;
}

static void MUSIC_OpenLocalPageEvent(lv_event_t *pEvent)
{
    MUSIC_PAGE ePage;

    if ((NULL == pEvent) ||
        (LV_EVENT_SHORT_CLICKED != lv_event_get_code(pEvent)))
    {
        return;
    }
    ePage = (MUSIC_PAGE)(uintptr_t)lv_event_get_user_data(pEvent);
    l_tMusicUi.eSource = MUSIC_SOURCE_LOCAL;
    MUSIC_RenderPage(ePage);

    return;
}

static void MUSIC_OpenLocalPlayerEvent(lv_event_t *pEvent)
{
    int lResult;

    if ((NULL == pEvent) ||
        (LV_EVENT_SHORT_CLICKED != lv_event_get_code(pEvent)))
    {
        return;
    }
    l_tMusicUi.eReturnPage = l_tMusicUi.ePage;
    l_tMusicUi.eSource = MUSIC_SOURCE_LOCAL;
    lResult = local_music_play_file(NULL,
                                   (2U == l_tMusicUi.ucPlayMode) ? 1U : 0U);
    MUSIC_RenderPage(MUSIC_PAGE_PLAYER);
    if (RT_EOK != lResult)
    {
        MUSIC_SetStatus("Track unavailable", lv_color_hex(0xFF9AABU));
    }

    return;
}

static void MUSIC_OpenPhoneControlEvent(lv_event_t *pEvent)
{
    if ((NULL == pEvent) ||
        (LV_EVENT_SHORT_CLICKED != lv_event_get_code(pEvent)))
    {
        return;
    }
    l_tMusicUi.eSource = MUSIC_SOURCE_PHONE;
    l_tMusicUi.eReturnPage = MUSIC_PAGE_SOURCE;
    MUSIC_RenderPage(MUSIC_PAGE_PHONE_CONTROL);

    return;
}

static void MUSIC_NowPlayingShortcutEvent(lv_event_t *pEvent)
{
    if ((NULL == pEvent) ||
        (LV_EVENT_SHORT_CLICKED != lv_event_get_code(pEvent)))
    {
        return;
    }
    l_tMusicUi.eReturnPage = l_tMusicUi.ePage;
    if (MUSIC_SOURCE_NONE == l_tMusicUi.eSource)
    {
        l_tMusicUi.eSource = MUSIC_SOURCE_LOCAL;
    }
    MUSIC_RenderPage((MUSIC_SOURCE_PHONE == l_tMusicUi.eSource) ?
                     MUSIC_PAGE_PHONE_CONTROL : MUSIC_PAGE_PLAYER);

    return;
}

static void MUSIC_CreateNowPlayingShortcut(void)
{
    (void)MUSIC_CreateImageButton(
        l_tMusicUi.pRoot,
        (MUSIC_SOURCE_NONE == l_tMusicUi.eSource) ?
            LV_EXT_IMG_GET(music_icon_not_playing) : LV_EXT_IMG_GET(music_icon_playing),
        LV_HOR_RES_MAX - 74,
        0,
        MUSIC_NowPlayingShortcutEvent,
        NULL);

    return;
}

static void MUSIC_RenderSourcePage(void)
{
    rt_kprintf("music: source render begin\n");
    MUSIC_CreateHeader("音乐", "");
    rt_kprintf("music: source header ready\n");
    MUSIC_CreateNowPlayingShortcut();
    rt_kprintf("music: source shortcut ready\n");
    (void)MUSIC_CreateListRow(92, LV_EXT_IMG_GET(music_icon_now_listening),
                              "现在就听", NULL,
                              MUSIC_OpenLocalPageEvent,
                              (void *)(uintptr_t)MUSIC_PAGE_NOW_LISTENING);
    (void)MUSIC_CreateListRow(196, LV_EXT_IMG_GET(music_icon_library), "资料库", NULL,
                              MUSIC_PageEvent,
                              (void *)(uintptr_t)MUSIC_PAGE_LIBRARY);
    (void)MUSIC_CreateListRow(300, LV_EXT_IMG_GET(music_icon_phone_music),
                              "手机音乐控制", NULL,
                              MUSIC_OpenPhoneControlEvent, NULL);
    MUSIC_CreateLabel(l_tMusicUi.pRoot, "左滑退出", MUSIC_PAGE_MARGIN,
                      LV_VER_RES_MAX - 42,
                      LV_HOR_RES_MAX - (MUSIC_PAGE_MARGIN * 2),
                      lv_color_hex(0x7C7998U), LV_TEXT_ALIGN_CENTER);
    rt_kprintf("music: source render complete\n");

    return;
}

static void MUSIC_RenderEmptyPage(
    const char *pTitle,
    const char *pText,
    const void *pIcon)
{
    MUSIC_RenderBackHeader(pTitle);
    MUSIC_CreateNowPlayingShortcut();
    (void)MUSIC_CreateImage(l_tMusicUi.pRoot, pIcon,
                            (LV_HOR_RES_MAX - 120) / 2, 160);
    MUSIC_CreateLabelSized(l_tMusicUi.pRoot, pText, MUSIC_PAGE_MARGIN, 300,
                           LV_HOR_RES_MAX - (MUSIC_PAGE_MARGIN * 2),
                           lv_color_hex(0x8F8C98U), LV_TEXT_ALIGN_CENTER,
                           MUSIC_FONT_BODY);

    return;
}

static void MUSIC_RenderNowListeningPage(void)
{
    MUSIC_RenderBackHeader("现在就听");
    MUSIC_CreateNowPlayingShortcut();
    if (l_tMusicUi.bTrackRemoved)
    {
        (void)MUSIC_CreateImage(l_tMusicUi.pRoot, LV_EXT_IMG_GET(music_icon_song_empty),
                                (LV_HOR_RES_MAX - 120) / 2, 160);
        MUSIC_CreateLabelSized(
            l_tMusicUi.pRoot, "暂无正在播放的音乐", MUSIC_PAGE_MARGIN, 300,
            LV_HOR_RES_MAX - (MUSIC_PAGE_MARGIN * 2),
            lv_color_hex(0x8F8C98U), LV_TEXT_ALIGN_CENTER,
            MUSIC_FONT_BODY);
        return;
    }
    (void)MUSIC_CreateListRow(92, LV_EXT_IMG_GET(music_icon_song), "那些花儿", "朴树",
                              MUSIC_OpenLocalPlayerEvent, NULL);
    (void)MUSIC_CreateListRow(196, LV_EXT_IMG_GET(music_icon_song), "晚风心里吹",
                              "本地音乐",
                              MUSIC_OpenLocalPlayerEvent, NULL);

    return;
}

static void MUSIC_RenderLibraryPage(void)
{
    MUSIC_RenderBackHeader("资料库");
    MUSIC_CreateNowPlayingShortcut();
    (void)MUSIC_CreateListRow(92, LV_EXT_IMG_GET(music_icon_playlist), "播放列表", NULL,
                              MUSIC_PageEvent,
                              (void *)(uintptr_t)MUSIC_PAGE_PLAYLISTS);
    (void)MUSIC_CreateListRow(196, LV_EXT_IMG_GET(music_icon_artist), "艺人", NULL,
                              MUSIC_PageEvent,
                              (void *)(uintptr_t)MUSIC_PAGE_ARTISTS);

    return;
}

static void MUSIC_RenderPlaylistsPage(void)
{
    if (l_tMusicUi.bTrackRemoved)
    {
        MUSIC_RenderEmptyPage("播放列表", "暂无播放列表",
                              LV_EXT_IMG_GET(music_icon_playlist_empty));
        return;
    }
    MUSIC_RenderBackHeader("播放列表");
    MUSIC_CreateNowPlayingShortcut();
    (void)MUSIC_CreateListRow(92, LV_EXT_IMG_GET(music_icon_song), "那些花儿", NULL,
                              MUSIC_OpenLocalPlayerEvent, NULL);
    (void)MUSIC_CreateListRow(196, LV_EXT_IMG_GET(music_icon_song), "晚风心里吹", NULL,
                              MUSIC_OpenLocalPlayerEvent, NULL);
    (void)MUSIC_CreateListRow(300, LV_EXT_IMG_GET(music_icon_song), "东风破", NULL,
                              MUSIC_OpenLocalPlayerEvent, NULL);

    return;
}

static void MUSIC_RenderArtistsPage(void)
{
    if (l_tMusicUi.bTrackRemoved)
    {
        MUSIC_RenderEmptyPage("艺人", "暂无艺人", LV_EXT_IMG_GET(music_icon_artist_empty));
        return;
    }
    MUSIC_RenderBackHeader("艺人");
    MUSIC_CreateNowPlayingShortcut();
    (void)MUSIC_CreateListRow(92, LV_EXT_IMG_GET(music_icon_artist), "朴树", NULL,
                              MUSIC_PageEvent,
                              (void *)(uintptr_t)MUSIC_PAGE_ARTIST_ALBUMS);
    (void)MUSIC_CreateListRow(196, LV_EXT_IMG_GET(music_icon_artist), "周杰伦", NULL,
                              MUSIC_PageEvent,
                              (void *)(uintptr_t)MUSIC_PAGE_ARTIST_ALBUMS);
    (void)MUSIC_CreateListRow(300, LV_EXT_IMG_GET(music_icon_artist), "蔡健雅", NULL,
                              MUSIC_PageEvent,
                              (void *)(uintptr_t)MUSIC_PAGE_ARTIST_ALBUMS);

    return;
}

static void MUSIC_RenderArtistAlbumsPage(void)
{
    MUSIC_RenderBackHeader("朴树");
    MUSIC_CreateNowPlayingShortcut();
    (void)MUSIC_CreateListRow(92, LV_EXT_IMG_GET(music_icon_album), "我的2002", "2002年",
                              MUSIC_PageEvent,
                              (void *)(uintptr_t)MUSIC_PAGE_ALBUM);
    (void)MUSIC_CreateListRow(196, LV_EXT_IMG_GET(music_icon_album), "猎户星座", "2017",
                              MUSIC_PageEvent,
                              (void *)(uintptr_t)MUSIC_PAGE_ALBUM);
    (void)MUSIC_CreateListRow(300, LV_EXT_IMG_GET(music_icon_album), "空帆船", "2018",
                              MUSIC_PageEvent,
                              (void *)(uintptr_t)MUSIC_PAGE_ALBUM);

    return;
}

static void MUSIC_OpenMoreEvent(lv_event_t *pEvent)
{
    if ((NULL == pEvent) ||
        (LV_EVENT_SHORT_CLICKED != lv_event_get_code(pEvent)))
    {
        return;
    }
    l_tMusicUi.eMenuReturnPage = l_tMusicUi.ePage;
    MUSIC_RenderPage(MUSIC_PAGE_MORE);

    return;
}

static void MUSIC_RenderAlbumPage(void)
{
    (void)MUSIC_CreateImage(l_tMusicUi.pRoot, LV_EXT_IMG_GET(music_bg_blur_full), 0, 0);
    MUSIC_RenderBackHeader("专辑");
    MUSIC_CreateNowPlayingShortcut();
    (void)MUSIC_CreateImage(l_tMusicUi.pRoot, LV_EXT_IMG_GET(music_icon_album_default),
                            (LV_HOR_RES_MAX - 74) / 2, 76);
    MUSIC_CreateLabelSized(l_tMusicUi.pRoot, "那些花儿", MUSIC_PAGE_MARGIN,
                           158, LV_HOR_RES_MAX - (MUSIC_PAGE_MARGIN * 2),
                           lv_color_hex(0xF6F4FFU), LV_TEXT_ALIGN_CENTER,
                           MUSIC_FONT_SUBTITLE);
    MUSIC_CreateLabelSized(l_tMusicUi.pRoot, "朴树", MUSIC_PAGE_MARGIN, 190,
                           LV_HOR_RES_MAX - (MUSIC_PAGE_MARGIN * 2),
                           lv_color_hex(0xAAA7B2U), LV_TEXT_ALIGN_CENTER,
                           MUSIC_FONT_CAPTION);
    (void)MUSIC_CreateImageButton(l_tMusicUi.pRoot, LV_EXT_IMG_GET(music_icon_play),
                                  36, 204, MUSIC_OpenLocalPlayerEvent, NULL);
    (void)MUSIC_CreateImageButton(l_tMusicUi.pRoot, LV_EXT_IMG_GET(music_icon_more),
                                  LV_HOR_RES_MAX - 110, 215,
                                  MUSIC_OpenMoreEvent, NULL);
    (void)MUSIC_CreateListRow(294, LV_EXT_IMG_GET(music_icon_song), "那些花儿", NULL,
                              MUSIC_OpenLocalPlayerEvent, NULL);
    (void)MUSIC_CreateListRow(398, LV_EXT_IMG_GET(music_icon_list_play),
                              "查看所有歌曲", NULL,
                              MUSIC_PageEvent,
                              (void *)(uintptr_t)MUSIC_PAGE_ALBUM_TRACKS);

    return;
}

static void MUSIC_RenderAlbumTracksPage(void)
{
    MUSIC_RenderBackHeader("专辑歌曲");
    MUSIC_CreateNowPlayingShortcut();
    (void)MUSIC_CreateListRow(92, LV_EXT_IMG_GET(music_icon_song), "1  New Boy", NULL,
                              MUSIC_OpenLocalPlayerEvent, NULL);
    (void)MUSIC_CreateListRow(196, LV_EXT_IMG_GET(music_icon_song), "2  妈妈，我", NULL,
                              MUSIC_OpenLocalPlayerEvent, NULL);
    (void)MUSIC_CreateListRow(300, LV_EXT_IMG_GET(music_icon_song), "3  那些花儿", NULL,
                              MUSIC_OpenLocalPlayerEvent, NULL);

    return;
}

static void MUSIC_PlayModeEvent(lv_event_t *pEvent)
{
    if ((NULL == pEvent) ||
        (LV_EVENT_SHORT_CLICKED != lv_event_get_code(pEvent)))
    {
        return;
    }
    l_tMusicUi.ucPlayMode = (uint8_t)((l_tMusicUi.ucPlayMode + 1U) % 3U);
    MUSIC_RenderPage(MUSIC_PAGE_MORE);

    return;
}

static void MUSIC_RemoveRequestEvent(lv_event_t *pEvent)
{
    if ((NULL == pEvent) ||
        (LV_EVENT_SHORT_CLICKED != lv_event_get_code(pEvent)))
    {
        return;
    }
    MUSIC_RenderPage(MUSIC_PAGE_REMOVE_CONFIRM);

    return;
}

static void MUSIC_RenderMorePage(void)
{
    const char *pMode;
    const void *pModeIcon;

    pMode = (0U == l_tMusicUi.ucPlayMode) ? "顺序播放" :
            ((1U == l_tMusicUi.ucPlayMode) ? "随机播放" : "单曲循环");
    pModeIcon = (0U == l_tMusicUi.ucPlayMode) ? LV_EXT_IMG_GET(music_icon_repeat) :
                ((1U == l_tMusicUi.ucPlayMode) ? LV_EXT_IMG_GET(music_icon_shuffle) :
                                                 LV_EXT_IMG_GET(music_icon_repeat_one));
    MUSIC_RenderCloseHeader("那些花儿");
    (void)MUSIC_CreateListRow(80, LV_EXT_IMG_GET(music_icon_remove), "移除...", NULL,
                              MUSIC_RemoveRequestEvent, NULL);
    (void)MUSIC_CreateListRow(180, pModeIcon, pMode, NULL,
                              MUSIC_PlayModeEvent, NULL);
    (void)MUSIC_CreateListRow(280, LV_EXT_IMG_GET(music_icon_go_artist),
                              "前往艺人", NULL,
                              MUSIC_PageEvent,
                              (void *)(uintptr_t)MUSIC_PAGE_ARTIST_ALBUMS);
    (void)MUSIC_CreateListRow(380, LV_EXT_IMG_GET(music_icon_go_album),
                              "前往专辑", NULL,
                              MUSIC_PageEvent,
                              (void *)(uintptr_t)MUSIC_PAGE_ALBUM);

    return;
}

static void MUSIC_RemoveConfirmEvent(lv_event_t *pEvent)
{
    if ((NULL == pEvent) ||
        (LV_EVENT_SHORT_CLICKED != lv_event_get_code(pEvent)))
    {
        return;
    }
    (void)local_music_stop();
    l_tMusicUi.bTrackRemoved = true;
    MUSIC_RenderPage(MUSIC_PAGE_NOW_LISTENING);

    return;
}

static void MUSIC_RenderRemoveConfirmPage(void)
{
    MUSIC_RenderCloseHeader("移除音乐");
    MUSIC_CreateLabel(l_tMusicUi.pRoot,
                      "你要从资料库删除，或\n从此设备移除吗？",
                      36, 125, LV_HOR_RES_MAX - 72,
                      lv_color_hex(0xF6F4FFU), LV_TEXT_ALIGN_CENTER);
    (void)MUSIC_CreateListRow(250, LV_EXT_IMG_GET(music_icon_remove),
                              "从设备移除", NULL,
                              MUSIC_RemoveConfirmEvent, NULL);
    (void)MUSIC_CreateListRow(354, LV_EXT_IMG_GET(music_icon_remove),
                              "从资料库删除", NULL,
                              MUSIC_RemoveConfirmEvent, NULL);

    return;
}

static void MUSIC_CreateArtwork(void)
{
    (void)MUSIC_CreateImage(l_tMusicUi.pRoot, LV_EXT_IMG_GET(music_icon_album_default),
                            (LV_HOR_RES_MAX - 74) / 2, 72);

    return;
}

static void MUSIC_OpenSettingsEvent(lv_event_t *pEvent)
{
    if ((NULL == pEvent) ||
        (LV_EVENT_SHORT_CLICKED != lv_event_get_code(pEvent)))
    {
        return;
    }
    l_tMusicUi.eMenuReturnPage = l_tMusicUi.ePage;
    MUSIC_RenderPage(MUSIC_PAGE_VOLUME);

    return;
}

static void MUSIC_RenderPlayerPage(void)
{
    uint32_t ulAccent;
    lv_coord_t lCenter;

    ulAccent = (MUSIC_SOURCE_PHONE == l_tMusicUi.eSource) ?
        0x7D79F2U : 0xFF8A65U;
    (void)MUSIC_CreateImage(l_tMusicUi.pRoot, LV_EXT_IMG_GET(music_bg_blur_full), 0, 0);
    MUSIC_RenderBackHeader((MUSIC_SOURCE_PHONE == l_tMusicUi.eSource) ?
                           "手机音乐控制" : "正在播放");
    (void)MUSIC_CreateImageButton(
        l_tMusicUi.pRoot, LV_EXT_IMG_GET(music_icon_more), LV_HOR_RES_MAX - 74, 0,
        (MUSIC_SOURCE_PHONE == l_tMusicUi.eSource) ?
            MUSIC_OpenSettingsEvent : MUSIC_OpenMoreEvent,
        NULL);
    MUSIC_CreateArtwork();

    l_tMusicUi.pTrack = MUSIC_CreateLabelSized(
        l_tMusicUi.pRoot,
        "Nothing playing",
        30,
        158,
        LV_HOR_RES_MAX - 60,
        lv_color_hex(0xF7F5FFU),
        LV_TEXT_ALIGN_CENTER,
        MUSIC_FONT_SUBTITLE);
    l_tMusicUi.pArtist = MUSIC_CreateLabelSized(
        l_tMusicUi.pRoot,
        "-",
        38,
        194,
        LV_HOR_RES_MAX - 76,
        lv_color_hex(0xC3C0D9U),
        LV_TEXT_ALIGN_CENTER,
        MUSIC_FONT_CAPTION);
    l_tMusicUi.pDetail = MUSIC_CreateLabelSized(
        l_tMusicUi.pRoot,
        "-",
        38,
        220,
        LV_HOR_RES_MAX - 76,
        lv_color_hex(0x8F8CA8U),
        LV_TEXT_ALIGN_CENTER,
        MUSIC_FONT_CAPTION);

    (void)MUSIC_CreateImage(l_tMusicUi.pRoot, LV_EXT_IMG_GET(music_progress_track),
                            (LV_HOR_RES_MAX - 240) / 2, 260);
    l_tMusicUi.pProgress = lv_bar_create(l_tMusicUi.pRoot);
    lv_obj_set_size(l_tMusicUi.pProgress, 240, 12);
    lv_obj_set_pos(l_tMusicUi.pProgress, (LV_HOR_RES_MAX - 240) / 2, 260);
    lv_bar_set_range(l_tMusicUi.pProgress, 0, MUSIC_PROGRESS_RANGE);
    lv_bar_set_value(l_tMusicUi.pProgress, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(
        l_tMusicUi.pProgress,
        lv_color_hex(0xFFFFFFU),
        LV_PART_MAIN);
    lv_obj_set_style_bg_opa(l_tMusicUi.pProgress, LV_OPA_TRANSP,
                            LV_PART_MAIN);
    lv_obj_set_style_bg_color(
        l_tMusicUi.pProgress,
        lv_color_hex(ulAccent),
        LV_PART_INDICATOR);
    lv_obj_set_style_bg_img_src(l_tMusicUi.pProgress, LV_EXT_IMG_GET(music_progress_value),
                                LV_PART_INDICATOR);
    lv_obj_set_style_radius(
        l_tMusicUi.pProgress,
        LV_RADIUS_CIRCLE,
        LV_PART_MAIN);
    lv_obj_set_style_radius(
        l_tMusicUi.pProgress,
        LV_RADIUS_CIRCLE,
        LV_PART_INDICATOR);

    l_tMusicUi.pElapsed = MUSIC_CreateLabelSized(
        l_tMusicUi.pRoot,
        "0:00",
        38,
        278,
        100,
        lv_color_hex(0x8F8CA8U),
        LV_TEXT_ALIGN_LEFT,
        MUSIC_FONT_CAPTION);
    l_tMusicUi.pDuration = MUSIC_CreateLabelSized(
        l_tMusicUi.pRoot,
        "0:00",
        LV_HOR_RES_MAX - 138,
        278,
        100,
        lv_color_hex(0x8F8CA8U),
        LV_TEXT_ALIGN_RIGHT,
        MUSIC_FONT_CAPTION);

    lCenter = LV_HOR_RES_MAX / 2;
    (void)MUSIC_CreateControlButton(
        LV_EXT_IMG_GET(music_icon_previous),
        MUSIC_CONTROL_ICON_WIDTH,
        lCenter - 120,
        340,
        MUSIC_ACTION_PREVIOUS);
    l_tMusicUi.pPlayLabel = MUSIC_CreateControlButton(
        LV_EXT_IMG_GET(music_icon_play),
        MUSIC_PLAY_ICON_WIDTH,
        lCenter,
        330,
        MUSIC_ACTION_TOGGLE);
    (void)MUSIC_CreateControlButton(
        LV_EXT_IMG_GET(music_icon_next),
        MUSIC_CONTROL_ICON_WIDTH,
        lCenter + 120,
        340,
        MUSIC_ACTION_NEXT);
    l_tMusicUi.pStatus = MUSIC_CreateLabelSized(
        l_tMusicUi.pRoot,
        "左滑返回，右滑进入更多设置",
        MUSIC_PAGE_MARGIN,
        LV_VER_RES_MAX - 32,
        LV_HOR_RES_MAX - (MUSIC_PAGE_MARGIN * 2),
        lv_color_hex(0x77748FU),
        LV_TEXT_ALIGN_CENTER,
        MUSIC_FONT_CAPTION);

    return;
}

static void MUSIC_RenderVolumePage(void)
{
    uint32_t ulAccent;
    lv_coord_t lCenter;

    ulAccent = (MUSIC_SOURCE_PHONE == l_tMusicUi.eSource) ?
        0x7D79F2U : 0xFF8A65U;
    MUSIC_RenderBackHeader("更多设置");
    (void)MUSIC_CreateImage(
        l_tMusicUi.pRoot,
        (MUSIC_SOURCE_PHONE == l_tMusicUi.eSource) ?
            LV_EXT_IMG_GET(music_icon_phone_play) : LV_EXT_IMG_GET(music_icon_watch_play),
        82,
        67);
    MUSIC_CreateLabelSized(
        l_tMusicUi.pRoot,
        (MUSIC_SOURCE_PHONE == l_tMusicUi.eSource) ?
            "手机音乐" : "手表音乐",
        MUSIC_PAGE_MARGIN,
        58,
        LV_HOR_RES_MAX - (MUSIC_PAGE_MARGIN * 2),
        lv_color_hex(0xAAA7C8U),
        LV_TEXT_ALIGN_CENTER,
        MUSIC_FONT_BODY);
    MUSIC_CreatePanel(
        l_tMusicUi.pRoot,
        (LV_HOR_RES_MAX - 250) / 2,
        94,
        250,
        190,
        0x242440U,
        36);
    MUSIC_CreateLabelSized(
        l_tMusicUi.pRoot,
        "音量",
        40,
        120,
        LV_HOR_RES_MAX - 80,
        lv_color_hex(0xAAA7C8U),
        LV_TEXT_ALIGN_CENTER,
        MUSIC_FONT_BODY);
    l_tMusicUi.pVolumeLabel = MUSIC_CreateLabelSized(
        l_tMusicUi.pRoot,
        "0%",
        40,
        165,
        LV_HOR_RES_MAX - 80,
        lv_color_hex(0xF7F5FFU),
        LV_TEXT_ALIGN_CENTER,
        MUSIC_FONT_TITLE);
    l_tMusicUi.pVolume = lv_bar_create(l_tMusicUi.pRoot);
    lv_obj_set_pos(l_tMusicUi.pVolume, 72, 235);
    lv_obj_set_size(l_tMusicUi.pVolume, LV_HOR_RES_MAX - 144, 16);
    lv_bar_set_range(l_tMusicUi.pVolume, 0, MUSIC_PHONE_VOLUME_MAX);
    lv_bar_set_value(l_tMusicUi.pVolume, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(
        l_tMusicUi.pVolume,
        lv_color_hex(0x3A3954U),
        LV_PART_MAIN);
    lv_obj_set_style_bg_color(
        l_tMusicUi.pVolume,
        lv_color_hex(ulAccent),
        LV_PART_INDICATOR);
    lv_obj_set_style_radius(
        l_tMusicUi.pVolume,
        LV_RADIUS_CIRCLE,
        LV_PART_MAIN);
    lv_obj_set_style_radius(
        l_tMusicUi.pVolume,
        LV_RADIUS_CIRCLE,
        LV_PART_INDICATOR);

    lCenter = LV_HOR_RES_MAX / 2;
    (void)MUSIC_CreateControlButton(
        LV_EXT_IMG_GET(music_icon_volume_down),
        MUSIC_VOLUME_ICON_WIDTH,
        lCenter - 78,
        315,
        MUSIC_ACTION_VOLUME_DOWN);
    (void)MUSIC_CreateControlButton(
        LV_EXT_IMG_GET(music_icon_volume_up),
        MUSIC_VOLUME_ICON_WIDTH,
        lCenter + 78,
        315,
        MUSIC_ACTION_VOLUME_UP);
    l_tMusicUi.pStatus = MUSIC_CreateLabelSized(
        l_tMusicUi.pRoot,
        "左滑返回",
        MUSIC_PAGE_MARGIN,
        LV_VER_RES_MAX - 42,
        LV_HOR_RES_MAX - (MUSIC_PAGE_MARGIN * 2),
        lv_color_hex(0x77748FU),
        LV_TEXT_ALIGN_CENTER,
        MUSIC_FONT_CAPTION);

    return;
}

static void MUSIC_RenderPage(MUSIC_PAGE ePage)
{
    if (NULL == l_tMusicUi.pRoot)
    {
        return;
    }

    MUSIC_ClearPage();
    l_tMusicUi.ePage = ePage;
    l_tMusicUi.ulRenderedAmsCount = MUSIC_INVALID_GENERATION;
    l_tMusicUi.eRenderedLocalState = (LOCAL_MUSIC_STATE)0xFFU;
    l_tMusicUi.ucRenderedLocalVolume = 0xFFU;
    if (MUSIC_PAGE_SOURCE == ePage)
    {
        MUSIC_RenderSourcePage();
    }
    else if (MUSIC_PAGE_PLAYER == ePage)
    {
        MUSIC_RenderPlayerPage();
    }
    else if (MUSIC_PAGE_VOLUME == ePage)
    {
        MUSIC_RenderVolumePage();
    }
    else if (MUSIC_PAGE_NOW_LISTENING == ePage)
    {
        MUSIC_RenderNowListeningPage();
    }
    else if (MUSIC_PAGE_LIBRARY == ePage)
    {
        MUSIC_RenderLibraryPage();
    }
    else if (MUSIC_PAGE_PLAYLISTS == ePage)
    {
        MUSIC_RenderPlaylistsPage();
    }
    else if (MUSIC_PAGE_ARTISTS == ePage)
    {
        MUSIC_RenderArtistsPage();
    }
    else if (MUSIC_PAGE_ARTIST_ALBUMS == ePage)
    {
        MUSIC_RenderArtistAlbumsPage();
    }
    else if (MUSIC_PAGE_ALBUM == ePage)
    {
        MUSIC_RenderAlbumPage();
    }
    else if (MUSIC_PAGE_ALBUM_TRACKS == ePage)
    {
        MUSIC_RenderAlbumTracksPage();
    }
    else if (MUSIC_PAGE_MORE == ePage)
    {
        MUSIC_RenderMorePage();
    }
    else if (MUSIC_PAGE_REMOVE_CONFIRM == ePage)
    {
        MUSIC_RenderRemoveConfirmPage();
    }
    else if (MUSIC_PAGE_PHONE_CONTROL == ePage)
    {
        MUSIC_RenderPlayerPage();
    }
    MUSIC_Refresh();

    return;
}

static void MUSIC_RefreshPhoneMetadata(const ble_ios_services_snapshot_t *pIos)
{
    char aDetail[BLE_IOS_TEXT_MEDIA_LEN + 40U];
    uint8_t ucState;
    uint32_t ulElapsed;

    if ((NULL == pIos) ||
        (l_tMusicUi.ulRenderedAmsCount == pIos->ams_count))
    {
        return;
    }

    l_tMusicUi.ulRenderedAmsCount = pIos->ams_count;
    ucState = 0U;
    ulElapsed = 0U;
    if (MUSIC_ParsePlayback(pIos->playback, &ucState, &ulElapsed))
    {
        l_tMusicUi.ucPhonePlaybackState = ucState;
        l_tMusicUi.ulPhoneElapsed = ulElapsed;
        l_tMusicUi.ulPhoneBaseTick = rt_tick_get();
    }
    l_tMusicUi.ulPhoneDuration = MUSIC_ParseSeconds(pIos->duration);
    l_tMusicUi.ucPhoneVolume = MUSIC_ParsePhoneVolume(pIos->volume);

    if ((MUSIC_PAGE_PLAYER == l_tMusicUi.ePage) ||
        (MUSIC_PAGE_PHONE_CONTROL == l_tMusicUi.ePage))
    {
        lv_label_set_text(
            l_tMusicUi.pTrack,
            MUSIC_TextOrFallback(pIos->track, "Nothing playing"));
        lv_label_set_text(
            l_tMusicUi.pArtist,
            MUSIC_TextOrFallback(
                pIos->artist,
                MUSIC_TextOrFallback(pIos->player, "Connect iPhone")));
        (void)rt_snprintf(
            aDetail,
            sizeof(aDetail),
            "%s  |  Lyrics unavailable via AMS",
            MUSIC_TextOrFallback(pIos->album, "Unknown album"));
        lv_label_set_text(l_tMusicUi.pDetail, aDetail);
        lv_img_set_src(
            l_tMusicUi.pPlayLabel,
            MUSIC_IsPhonePlaying() ? LV_EXT_IMG_GET(music_icon_pause) : LV_EXT_IMG_GET(music_icon_play));
    }

    return;
}

static void MUSIC_RefreshPhoneProgress(void)
{
    char aElapsed[16];
    char aDuration[16];
    uint32_t ulElapsed;
    uint32_t ulProgress;

    if ((MUSIC_PAGE_PLAYER != l_tMusicUi.ePage) &&
        (MUSIC_PAGE_PHONE_CONTROL != l_tMusicUi.ePage))
    {
        return;
    }

    ulElapsed = l_tMusicUi.ulPhoneElapsed;
    if (MUSIC_IsPhonePlaying())
    {
        ulElapsed += (rt_tick_get() - l_tMusicUi.ulPhoneBaseTick) /
            RT_TICK_PER_SECOND;
    }
    if ((0U != l_tMusicUi.ulPhoneDuration) &&
        (l_tMusicUi.ulPhoneDuration < ulElapsed))
    {
        ulElapsed = l_tMusicUi.ulPhoneDuration;
    }
    ulProgress = 0U;
    if (0U != l_tMusicUi.ulPhoneDuration)
    {
        ulProgress = (uint32_t)(((uint64_t)ulElapsed * MUSIC_PROGRESS_RANGE) /
                                l_tMusicUi.ulPhoneDuration);
    }
    lv_bar_set_value(l_tMusicUi.pProgress, (int32_t)ulProgress, LV_ANIM_OFF);
    MUSIC_FormatTime(aElapsed, sizeof(aElapsed), ulElapsed);
    MUSIC_FormatTime(aDuration, sizeof(aDuration), l_tMusicUi.ulPhoneDuration);
    lv_label_set_text(l_tMusicUi.pElapsed, aElapsed);
    lv_label_set_text(l_tMusicUi.pDuration, aDuration);

    return;
}

static void MUSIC_RefreshLocal(const LOCAL_MUSIC_SNAPSHOT *pLocal)
{
    const char *pFileName;
    char aVolume[16];
    uint8_t ucVolume;
    uint8_t ucMaximumVolume;
    uint8_t ucPercent;

    if (NULL == pLocal)
    {
        return;
    }
    ucVolume = watch_settings_get_local_volume();
    if ((l_tMusicUi.eRenderedLocalState == pLocal->eState) &&
        (l_tMusicUi.ucRenderedLocalVolume == ucVolume))
    {
        return;
    }
    l_tMusicUi.eRenderedLocalState = pLocal->eState;
    l_tMusicUi.ucRenderedLocalVolume = ucVolume;

    if (MUSIC_PAGE_PLAYER == l_tMusicUi.ePage)
    {
        pFileName = strrchr(pLocal->aPath, '/');
        pFileName = (NULL == pFileName) ? pLocal->aPath : pFileName + 1;
        lv_label_set_text(
            l_tMusicUi.pTrack,
            MUSIC_TextOrFallback(pFileName, "Local audio"));
        lv_label_set_text(l_tMusicUi.pArtist, "On-device audio");
        lv_label_set_text(
            l_tMusicUi.pDetail,
            "Local file  |  No embedded lyrics");
        lv_img_set_src(
            l_tMusicUi.pPlayLabel,
            MUSIC_IsLocalPlaying(pLocal->eState) ?
                LV_EXT_IMG_GET(music_icon_pause) : LV_EXT_IMG_GET(music_icon_play));
        lv_bar_set_value(l_tMusicUi.pProgress, 0, LV_ANIM_OFF);
        lv_label_set_text(l_tMusicUi.pElapsed, "0:00");
        lv_label_set_text(l_tMusicUi.pDuration, "--:--");
    }

    ucMaximumVolume = (uint8_t)audio_server_get_max_volume();
    ucPercent = (0U == ucMaximumVolume) ? 0U :
        (uint8_t)(((uint16_t)ucVolume * 100U) / ucMaximumVolume);
    if (MUSIC_PAGE_VOLUME == l_tMusicUi.ePage)
    {
        lv_bar_set_value(l_tMusicUi.pVolume, ucPercent, LV_ANIM_ON);
        (void)rt_snprintf(aVolume, sizeof(aVolume), "%u%%", ucPercent);
        lv_label_set_text(l_tMusicUi.pVolumeLabel, aVolume);
    }

    return;
}

static void MUSIC_Refresh(void)
{
    if (MUSIC_SOURCE_PHONE == l_tMusicUi.eSource)
    {
        ble_ios_services_snapshot_t tIos;
        char aVolume[16];

        ble_ios_services_get_snapshot(&tIos);
        MUSIC_RefreshPhoneMetadata(&tIos);
        MUSIC_RefreshPhoneProgress();
        if (MUSIC_PAGE_VOLUME == l_tMusicUi.ePage)
        {
            lv_bar_set_value(
                l_tMusicUi.pVolume,
                l_tMusicUi.ucPhoneVolume,
                LV_ANIM_ON);
            (void)rt_snprintf(
                aVolume,
                sizeof(aVolume),
                "%u%%",
                l_tMusicUi.ucPhoneVolume);
            lv_label_set_text(l_tMusicUi.pVolumeLabel, aVolume);
        }
    }
    else if (MUSIC_SOURCE_LOCAL == l_tMusicUi.eSource)
    {
        LOCAL_MUSIC_SNAPSHOT tLocal;

        if (RT_EOK != local_music_get_snapshot(&tLocal))
        {
            MUSIC_SetStatus("Local player unavailable", lv_color_hex(0xFF9AABU));
            return;
        }
        MUSIC_RefreshLocal(&tLocal);
    }

    return;
}

static void MUSIC_GoBack(void)
{
    MUSIC_PAGE eTargetPage;

    eTargetPage = MUSIC_PAGE_SOURCE;
    switch (l_tMusicUi.ePage)
    {
    case MUSIC_PAGE_SOURCE:
        if (RT_EOK != gui_app_goback())
        {
            (void)gui_app_run("Main");
        }
        return;
    case MUSIC_PAGE_PLAYER:
        eTargetPage = l_tMusicUi.eReturnPage;
        break;
    case MUSIC_PAGE_VOLUME:
        eTargetPage = l_tMusicUi.eMenuReturnPage;
        break;
    case MUSIC_PAGE_NOW_LISTENING:
    case MUSIC_PAGE_LIBRARY:
    case MUSIC_PAGE_PHONE_CONTROL:
        eTargetPage = MUSIC_PAGE_SOURCE;
        break;
    case MUSIC_PAGE_PLAYLISTS:
    case MUSIC_PAGE_ARTISTS:
        eTargetPage = MUSIC_PAGE_LIBRARY;
        break;
    case MUSIC_PAGE_ARTIST_ALBUMS:
        eTargetPage = MUSIC_PAGE_ARTISTS;
        break;
    case MUSIC_PAGE_ALBUM:
        eTargetPage = MUSIC_PAGE_ARTIST_ALBUMS;
        break;
    case MUSIC_PAGE_ALBUM_TRACKS:
        eTargetPage = MUSIC_PAGE_ALBUM;
        break;
    case MUSIC_PAGE_MORE:
        eTargetPage = l_tMusicUi.eMenuReturnPage;
        break;
    case MUSIC_PAGE_REMOVE_CONFIRM:
        eTargetPage = MUSIC_PAGE_MORE;
        break;
    default:
        eTargetPage = MUSIC_PAGE_SOURCE;
        break;
    }
    if ((MUSIC_PAGE_SOURCE > eTargetPage) ||
        (MUSIC_PAGE_PHONE_CONTROL < eTargetPage))
    {
        eTargetPage = MUSIC_PAGE_SOURCE;
    }
    MUSIC_RenderPage(eTargetPage);

    return;
}

static void MUSIC_BackEvent(lv_event_t *pEvent)
{
    if ((NULL == pEvent) ||
        (LV_EVENT_SHORT_CLICKED != lv_event_get_code(pEvent)))
    {
        return;
    }
    MUSIC_GoBack();

    return;
}

static void MUSIC_GestureEvent(lv_event_t *pEvent)
{
    lv_indev_t *pInput;
    lv_dir_t eDirection;

    if ((NULL == pEvent) ||
        (LV_EVENT_GESTURE != lv_event_get_code(pEvent)))
    {
        return;
    }
    pInput = lv_indev_get_act();
    if (NULL == pInput)
    {
        return;
    }
    eDirection = lv_indev_get_gesture_dir(pInput);
    if (LV_DIR_LEFT == eDirection)
    {
        MUSIC_GoBack();
    }
    else if ((LV_DIR_RIGHT == eDirection) &&
             ((MUSIC_PAGE_PLAYER == l_tMusicUi.ePage) ||
              (MUSIC_PAGE_PHONE_CONTROL == l_tMusicUi.ePage)) &&
             (MUSIC_SOURCE_NONE != l_tMusicUi.eSource))
    {
        l_tMusicUi.eMenuReturnPage = l_tMusicUi.ePage;
        MUSIC_RenderPage(MUSIC_PAGE_VOLUME);
    }

    return;
}

static void MUSIC_TimerCallback(lv_timer_t *pTimer)
{
    (void)pTimer;
    MUSIC_Refresh();

    return;
}

static void MUSIC_OnStart(void)
{
    rt_kprintf("music: start begin\n");
    (void)rt_memset(&l_tMusicUi, 0, sizeof(l_tMusicUi));
    l_tMusicUi.eSource = MUSIC_SOURCE_NONE;
    l_tMusicUi.ulRenderedAmsCount = MUSIC_INVALID_GENERATION;
    l_tMusicUi.eRenderedLocalState = (LOCAL_MUSIC_STATE)0xFFU;
    l_tMusicUi.ucRenderedLocalVolume = 0xFFU;

    l_tMusicUi.pRoot = lv_obj_create(lv_scr_act());
    if (NULL == l_tMusicUi.pRoot)
    {
        rt_kprintf("music: root allocation failed\n");
        return;
    }
    lv_obj_set_size(l_tMusicUi.pRoot, LV_HOR_RES_MAX, LV_VER_RES_MAX);
    lv_obj_set_style_bg_color(l_tMusicUi.pRoot, lv_color_hex(0x121225U), 0);
    lv_obj_set_style_bg_grad_color(
        l_tMusicUi.pRoot,
        lv_color_hex(0x27264BU),
        0);
    lv_obj_set_style_bg_grad_dir(l_tMusicUi.pRoot, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(l_tMusicUi.pRoot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(l_tMusicUi.pRoot, 0, 0);
    lv_obj_set_style_pad_all(l_tMusicUi.pRoot, 0, 0);
    lv_obj_clear_flag(l_tMusicUi.pRoot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(
        l_tMusicUi.pRoot,
        MUSIC_GestureEvent,
        LV_EVENT_GESTURE,
        NULL);

    MUSIC_RenderPage(MUSIC_PAGE_SOURCE);
    rt_kprintf("music: source page ready\n");
    l_tMusicUi.pRefreshTimer = lv_timer_create(
        MUSIC_TimerCallback,
        MUSIC_REFRESH_MS,
        NULL);
    rt_kprintf("music: start complete\n");

    return;
}

static void MUSIC_OnStop(void)
{
    if (NULL != l_tMusicUi.pRefreshTimer)
    {
        lv_timer_del(l_tMusicUi.pRefreshTimer);
        l_tMusicUi.pRefreshTimer = NULL;
    }
    if (NULL != l_tMusicUi.pRoot)
    {
        lv_obj_del(l_tMusicUi.pRoot);
        l_tMusicUi.pRoot = NULL;
    }
    (void)rt_memset(&l_tMusicUi, 0, sizeof(l_tMusicUi));

    return;
}

static void MUSIC_MessageHandler(gui_app_msg_type_t eMessage, void *pParameter)
{
    (void)pParameter;
    if (GUI_APP_MSG_ONSTART == eMessage)
    {
        MUSIC_OnStart();
    }
    else if (GUI_APP_MSG_ONSTOP == eMessage)
    {
        MUSIC_OnStop();
    }

    return;
}

static int MUSIC_AppMain(intent_t tIntent)
{
    (void)tIntent;
    gui_app_regist_msg_handler(APP_ID, MUSIC_MessageHandler);

    return 0;
}

LV_IMG_DECLARE(img_itunes);
BUILTIN_APP_EXPORT(
    LV_EXT_STR_ID(music),
    LV_EXT_IMG_GET(img_itunes),
    APP_ID,
    MUSIC_AppMain);
