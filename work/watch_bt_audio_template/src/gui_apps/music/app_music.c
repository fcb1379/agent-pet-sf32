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
#else
    #include "bf0_ble_ams.h"
#endif /* BSP_USING_PC_SIMULATOR */
#include "audio_server.h"
#include "ble_ios_services.h"
#include "local_music_player.h"
#include "watch_settings.h"

#define APP_ID                          "music"
#define MUSIC_REFRESH_MS                (400U)
#define MUSIC_PAGE_MARGIN               (18)
#define MUSIC_CARD_GAP                  (14)
#define MUSIC_CONTROL_SIZE              (72)
#define MUSIC_PROGRESS_RANGE            (1000)
#define MUSIC_PHONE_VOLUME_MAX          (100U)
#define MUSIC_INVALID_GENERATION        (0xFFFFFFFFUL)

LV_IMG_DECLARE(img_phone_music);
LV_IMG_DECLARE(img_local_music);

typedef enum _MUSIC_PAGE
{
    MUSIC_PAGE_SOURCE = 0,
    MUSIC_PAGE_PLAYER,
    MUSIC_PAGE_VOLUME
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
 *   - pPlayLabel: center control label, updated from playback state.
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
} MUSIC_UI;

/* Module-local UI state. All LVGL members are accessed only on the GUI task. */
static MUSIC_UI l_tMusicUi;

static void MUSIC_RenderPage(MUSIC_PAGE ePage);
static void MUSIC_Refresh(void);

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
    MUSIC_CreateLabel(
        l_tMusicUi.pRoot,
        pTitle,
        MUSIC_PAGE_MARGIN,
        12,
        LV_HOR_RES_MAX - (MUSIC_PAGE_MARGIN * 2),
        lv_color_hex(0xF4F2FFU),
        LV_TEXT_ALIGN_LEFT);
    MUSIC_CreateLabel(
        l_tMusicUi.pRoot,
        pSubtitle,
        MUSIC_PAGE_MARGIN,
        43,
        LV_HOR_RES_MAX - (MUSIC_PAGE_MARGIN * 2),
        lv_color_hex(0xAAA7C8U),
        LV_TEXT_ALIGN_LEFT);

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
    const char *pText,
    lv_coord_t lCenterX,
    lv_coord_t lY,
    lv_coord_t lSize,
    MUSIC_ACTION eAction,
    uint32_t ulColor)
{
    lv_obj_t *pButton;
    lv_obj_t *pLabel;

    pButton = MUSIC_CreatePanel(
        l_tMusicUi.pRoot,
        lCenterX - (lSize / 2),
        lY,
        lSize,
        lSize,
        ulColor,
        LV_RADIUS_CIRCLE);
    if (NULL == pButton)
    {
        return NULL;
    }
    lv_obj_add_flag(pButton, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        pButton,
        MUSIC_ActionEvent,
        LV_EVENT_SHORT_CLICKED,
        (void *)(uintptr_t)eAction);
    pLabel = MUSIC_CreateLabel(
        pButton,
        pText,
        0,
        (lSize - 24) / 2,
        lSize,
        lv_color_hex(0xF8F7FFU),
        LV_TEXT_ALIGN_CENTER);

    return pLabel;
}

static void MUSIC_SourceEvent(lv_event_t *pEvent)
{
    MUSIC_SOURCE eSource;

    if ((NULL == pEvent) ||
        (LV_EVENT_SHORT_CLICKED != lv_event_get_code(pEvent)))
    {
        return;
    }

    eSource = (MUSIC_SOURCE)(uintptr_t)lv_event_get_user_data(pEvent);
    if ((MUSIC_SOURCE_PHONE != eSource) && (MUSIC_SOURCE_LOCAL != eSource))
    {
        return;
    }
    l_tMusicUi.eSource = eSource;
    MUSIC_RenderPage(MUSIC_PAGE_PLAYER);

    return;
}

static void MUSIC_CreateSourceCard(
    MUSIC_SOURCE eSource,
    lv_coord_t lX,
    const lv_img_dsc_t *pImage,
    const char *pTitle,
    const char *pSubtitle,
    uint32_t ulColor)
{
    lv_coord_t lCardWidth;
    lv_obj_t *pCard;
    lv_obj_t *pIcon;

    lCardWidth = (LV_HOR_RES_MAX - (MUSIC_PAGE_MARGIN * 2) -
                  MUSIC_CARD_GAP) / 2;
    pCard = MUSIC_CreatePanel(
        l_tMusicUi.pRoot,
        lX,
        92,
        lCardWidth,
        264,
        ulColor,
        28);
    if (NULL == pCard)
    {
        return;
    }
    lv_obj_set_style_border_width(pCard, 2, 0);
    lv_obj_set_style_border_color(pCard, lv_color_hex(0x67658AU), 0);
    lv_obj_add_flag(pCard, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        pCard,
        MUSIC_SourceEvent,
        LV_EVENT_SHORT_CLICKED,
        (void *)(uintptr_t)eSource);

    pIcon = lv_img_create(pCard);
    lv_img_set_src(pIcon, pImage);
    lv_obj_align(pIcon, LV_ALIGN_TOP_MID, 0, 24);
    lv_obj_clear_flag(pIcon, LV_OBJ_FLAG_CLICKABLE);
    MUSIC_CreateLabel(
        pCard,
        pTitle,
        8,
        158,
        lCardWidth - 16,
        lv_color_hex(0xF6F4FFU),
        LV_TEXT_ALIGN_CENTER);
    MUSIC_CreateLabel(
        pCard,
        pSubtitle,
        8,
        194,
        lCardWidth - 16,
        lv_color_hex(0xB5B1D2U),
        LV_TEXT_ALIGN_CENTER);
    MUSIC_CreateLabel(
        pCard,
        "Tap to open",
        8,
        229,
        lCardWidth - 16,
        lv_color_hex(0x7DE2FFU),
        LV_TEXT_ALIGN_CENTER);

    return;
}

static void MUSIC_RenderSourcePage(void)
{
    lv_coord_t lCardWidth;

    MUSIC_CreateHeader("Music", "Choose a playback source");
    lCardWidth = (LV_HOR_RES_MAX - (MUSIC_PAGE_MARGIN * 2) -
                  MUSIC_CARD_GAP) / 2;
    MUSIC_CreateSourceCard(
        MUSIC_SOURCE_PHONE,
        MUSIC_PAGE_MARGIN,
        &img_phone_music,
        "Phone music",
        "iPhone / AMS",
        0x202547U);
    MUSIC_CreateSourceCard(
        MUSIC_SOURCE_LOCAL,
        MUSIC_PAGE_MARGIN + lCardWidth + MUSIC_CARD_GAP,
        &img_local_music,
        "Local music",
        "On-device file",
        0x3B2E36U);
    MUSIC_CreateLabel(
        l_tMusicUi.pRoot,
        "Swipe left to exit",
        MUSIC_PAGE_MARGIN,
        LV_VER_RES_MAX - 42,
        LV_HOR_RES_MAX - (MUSIC_PAGE_MARGIN * 2),
        lv_color_hex(0x7C7998U),
        LV_TEXT_ALIGN_CENTER);

    return;
}

static void MUSIC_CreateArtwork(void)
{
    lv_obj_t *pCover;
    lv_obj_t *pDisc;
    lv_obj_t *pCenter;
    lv_obj_t *pHighlight;
    uint32_t ulAccent;

    ulAccent = (MUSIC_SOURCE_PHONE == l_tMusicUi.eSource) ?
        0x7D79F2U : 0xFF8A65U;
    pCover = MUSIC_CreatePanel(
        l_tMusicUi.pRoot,
        (LV_HOR_RES_MAX - 152) / 2,
        62,
        152,
        152,
        0x242440U,
        34);
    if (NULL == pCover)
    {
        return;
    }
    lv_obj_set_style_bg_grad_color(pCover, lv_color_hex(ulAccent), 0);
    lv_obj_set_style_bg_grad_dir(pCover, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(pCover, 2, 0);
    lv_obj_set_style_border_color(pCover, lv_color_hex(0x8985B1U), 0);

    pDisc = MUSIC_CreatePanel(pCover, 23, 23, 106, 106, 0x11111DU,
                              LV_RADIUS_CIRCLE);
    pCenter = MUSIC_CreatePanel(pDisc, 35, 35, 36, 36, ulAccent,
                                LV_RADIUS_CIRCLE);
    pHighlight = MUSIC_CreatePanel(pDisc, 70, 18, 9, 9, 0xF3F1FFU,
                                   LV_RADIUS_CIRCLE);
    (void)pCenter;
    (void)pHighlight;

    return;
}

static void MUSIC_RenderPlayerPage(void)
{
    const char *pSourceName;
    uint32_t ulAccent;
    lv_coord_t lCenter;

    pSourceName = (MUSIC_SOURCE_PHONE == l_tMusicUi.eSource) ?
        "PHONE" : "LOCAL";
    ulAccent = (MUSIC_SOURCE_PHONE == l_tMusicUi.eSource) ?
        0x7D79F2U : 0xFF8A65U;
    MUSIC_CreateLabel(
        l_tMusicUi.pRoot,
        pSourceName,
        MUSIC_PAGE_MARGIN,
        13,
        100,
        lv_color_hex(ulAccent),
        LV_TEXT_ALIGN_LEFT);
    MUSIC_CreateLabel(
        l_tMusicUi.pRoot,
        "Now playing",
        LV_HOR_RES_MAX - 150,
        13,
        132,
        lv_color_hex(0xAAA7C8U),
        LV_TEXT_ALIGN_RIGHT);
    MUSIC_CreateArtwork();

    l_tMusicUi.pTrack = MUSIC_CreateLabel(
        l_tMusicUi.pRoot,
        "Nothing playing",
        30,
        225,
        LV_HOR_RES_MAX - 60,
        lv_color_hex(0xF7F5FFU),
        LV_TEXT_ALIGN_CENTER);
    l_tMusicUi.pArtist = MUSIC_CreateLabel(
        l_tMusicUi.pRoot,
        "-",
        38,
        257,
        LV_HOR_RES_MAX - 76,
        lv_color_hex(0xC3C0D9U),
        LV_TEXT_ALIGN_CENTER);
    l_tMusicUi.pDetail = MUSIC_CreateLabel(
        l_tMusicUi.pRoot,
        "-",
        38,
        284,
        LV_HOR_RES_MAX - 76,
        lv_color_hex(0x8F8CA8U),
        LV_TEXT_ALIGN_CENTER);

    l_tMusicUi.pProgress = lv_bar_create(l_tMusicUi.pRoot);
    lv_obj_set_size(l_tMusicUi.pProgress, LV_HOR_RES_MAX - 76, 10);
    lv_obj_set_pos(l_tMusicUi.pProgress, 38, 315);
    lv_bar_set_range(l_tMusicUi.pProgress, 0, MUSIC_PROGRESS_RANGE);
    lv_bar_set_value(l_tMusicUi.pProgress, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(
        l_tMusicUi.pProgress,
        lv_color_hex(0x34334FU),
        LV_PART_MAIN);
    lv_obj_set_style_bg_color(
        l_tMusicUi.pProgress,
        lv_color_hex(ulAccent),
        LV_PART_INDICATOR);
    lv_obj_set_style_radius(
        l_tMusicUi.pProgress,
        LV_RADIUS_CIRCLE,
        LV_PART_MAIN);
    lv_obj_set_style_radius(
        l_tMusicUi.pProgress,
        LV_RADIUS_CIRCLE,
        LV_PART_INDICATOR);

    l_tMusicUi.pElapsed = MUSIC_CreateLabel(
        l_tMusicUi.pRoot,
        "0:00",
        38,
        330,
        100,
        lv_color_hex(0x8F8CA8U),
        LV_TEXT_ALIGN_LEFT);
    l_tMusicUi.pDuration = MUSIC_CreateLabel(
        l_tMusicUi.pRoot,
        "0:00",
        LV_HOR_RES_MAX - 138,
        330,
        100,
        lv_color_hex(0x8F8CA8U),
        LV_TEXT_ALIGN_RIGHT);

    lCenter = LV_HOR_RES_MAX / 2;
    (void)MUSIC_CreateControlButton(
        "<",
        lCenter - 104,
        358,
        58,
        MUSIC_ACTION_PREVIOUS,
        0x33324FU);
    l_tMusicUi.pPlayLabel = MUSIC_CreateControlButton(
        ">",
        lCenter,
        350,
        MUSIC_CONTROL_SIZE,
        MUSIC_ACTION_TOGGLE,
        ulAccent);
    (void)MUSIC_CreateControlButton(
        ">",
        lCenter + 104,
        358,
        58,
        MUSIC_ACTION_NEXT,
        0x33324FU);
    l_tMusicUi.pStatus = MUSIC_CreateLabel(
        l_tMusicUi.pRoot,
        "Left: back   Right: volume",
        MUSIC_PAGE_MARGIN,
        LV_VER_RES_MAX - 25,
        LV_HOR_RES_MAX - (MUSIC_PAGE_MARGIN * 2),
        lv_color_hex(0x77748FU),
        LV_TEXT_ALIGN_CENTER);

    return;
}

static void MUSIC_RenderVolumePage(void)
{
    uint32_t ulAccent;
    lv_coord_t lCenter;

    ulAccent = (MUSIC_SOURCE_PHONE == l_tMusicUi.eSource) ?
        0x7D79F2U : 0xFF8A65U;
    MUSIC_CreateHeader(
        "Volume",
        (MUSIC_SOURCE_PHONE == l_tMusicUi.eSource) ?
            "Phone media volume" : "Watch speaker volume");
    MUSIC_CreatePanel(
        l_tMusicUi.pRoot,
        (LV_HOR_RES_MAX - 250) / 2,
        94,
        250,
        190,
        0x242440U,
        36);
    MUSIC_CreateLabel(
        l_tMusicUi.pRoot,
        "VOLUME",
        40,
        120,
        LV_HOR_RES_MAX - 80,
        lv_color_hex(0xAAA7C8U),
        LV_TEXT_ALIGN_CENTER);
    l_tMusicUi.pVolumeLabel = MUSIC_CreateLabel(
        l_tMusicUi.pRoot,
        "0%",
        40,
        165,
        LV_HOR_RES_MAX - 80,
        lv_color_hex(0xF7F5FFU),
        LV_TEXT_ALIGN_CENTER);
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
        "-",
        lCenter - 78,
        315,
        MUSIC_CONTROL_SIZE,
        MUSIC_ACTION_VOLUME_DOWN,
        0x33324FU);
    (void)MUSIC_CreateControlButton(
        "+",
        lCenter + 78,
        315,
        MUSIC_CONTROL_SIZE,
        MUSIC_ACTION_VOLUME_UP,
        ulAccent);
    l_tMusicUi.pStatus = MUSIC_CreateLabel(
        l_tMusicUi.pRoot,
        "Swipe left to return",
        MUSIC_PAGE_MARGIN,
        LV_VER_RES_MAX - 42,
        LV_HOR_RES_MAX - (MUSIC_PAGE_MARGIN * 2),
        lv_color_hex(0x77748FU),
        LV_TEXT_ALIGN_CENTER);

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

    if (MUSIC_PAGE_PLAYER == l_tMusicUi.ePage)
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
        lv_label_set_text(
            l_tMusicUi.pPlayLabel,
            MUSIC_IsPhonePlaying() ? "||" : ">");
    }

    return;
}

static void MUSIC_RefreshPhoneProgress(void)
{
    char aElapsed[16];
    char aDuration[16];
    uint32_t ulElapsed;
    uint32_t ulProgress;

    if (MUSIC_PAGE_PLAYER != l_tMusicUi.ePage)
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
        lv_label_set_text(
            l_tMusicUi.pPlayLabel,
            MUSIC_IsLocalPlaying(pLocal->eState) ? "||" : ">");
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
        if (MUSIC_PAGE_SOURCE == l_tMusicUi.ePage)
        {
            if (RT_EOK != gui_app_goback())
            {
                (void)gui_app_run("Main");
            }
        }
        else if (MUSIC_PAGE_VOLUME == l_tMusicUi.ePage)
        {
            MUSIC_RenderPage(MUSIC_PAGE_PLAYER);
        }
        else
        {
            MUSIC_RenderPage(MUSIC_PAGE_SOURCE);
        }
    }
    else if ((LV_DIR_RIGHT == eDirection) &&
             (MUSIC_PAGE_PLAYER == l_tMusicUi.ePage) &&
             (MUSIC_SOURCE_NONE != l_tMusicUi.eSource))
    {
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
    (void)rt_memset(&l_tMusicUi, 0, sizeof(l_tMusicUi));
    l_tMusicUi.eSource = MUSIC_SOURCE_NONE;
    l_tMusicUi.ulRenderedAmsCount = MUSIC_INVALID_GENERATION;
    l_tMusicUi.eRenderedLocalState = (LOCAL_MUSIC_STATE)0xFFU;
    l_tMusicUi.ucRenderedLocalVolume = 0xFFU;

    l_tMusicUi.pRoot = lv_obj_create(lv_scr_act());
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
    l_tMusicUi.pRefreshTimer = lv_timer_create(
        MUSIC_TimerCallback,
        MUSIC_REFRESH_MS,
        NULL);

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
