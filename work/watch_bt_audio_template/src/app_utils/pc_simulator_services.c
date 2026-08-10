/* PC simulator service model for the complete watch UI.
 * Hardware builds continue to use the BLE, flash, audio, and alarm services. */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <rtthread.h>

#include "agent_pet_ble_service.h"
#include "badge_transfer.h"
#include "ble_ios_services.h"
#include "bt_audio_sink.h"
#include "local_music_player.h"
#include "watch_alarm_service.h"
#include "watch_settings.h"

#define PCSIM_VOLUME_MAX                 (15U)
#define PCSIM_DEFAULT_TIMER_SECONDS      (300U)

/* Persistent simulator settings; values mirror a normally configured watch. */
static watch_settings_snapshot_t l_tSettings =
{
    WATCH_SETTINGS_DEFAULT_LOCAL_VOLUME,
    WATCH_SETTINGS_DEFAULT_BT_VOLUME,
    WATCH_AUDIO_ROUTE_SPEAKER_ONLY,
    480,
    0U,
    1U,
    7U,
    30U,
    0x7FU,
    1U
};

/* Alarm/timer state is updated from the RT-Thread PC tick counter. */
static watch_alarm_snapshot_t l_tAlarm =
{
    1U,
    7U,
    30U,
    0x7FU,
    1U,
    0U,
    0U,
    0U,
    PCSIM_DEFAULT_TIMER_SECONDS
};
static rt_tick_t l_tTimerStartTick;

/* Demo iOS state lets notification and music widgets render useful content. */
static ble_ios_services_snapshot_t l_tIos =
{
    3U,
    1U,
    0x10203040UL,
    1U,
    "Messages",
    "Agent Pet simulator",
    "Full UI services are running in PC simulation mode.",
    "Simulator Music",
    "Paused",
    "SiFli UI Team",
    "Desktop Preview",
    "Complete Interface"
};

/* Bluetooth health is deliberately healthy for the status diagnostics page. */
static bt_audio_sink_health_t l_tBtHealth =
{
    BT_AUDIO_SINK_STATE_CONNECTED,
    0,
    0U,
    0U,
    0U
};

/* Badge transfer starts ready so the badge screen has deterministic state. */
static badge_transfer_snapshot_t l_tBadge =
{
    BADGE_TRANSFER_READY,
    16384U,
    16384U,
    1U,
    0U,
    0,
    1U
};

/* Agent state is populated lazily to keep nested initializers portable to MSVC. */
static AGENTPET_BLE_STATUS l_tAgentStatus;
static bool l_bAgentInitialized;

/***************************
 * rt_spi_msd_init: report that removable SPI storage is unavailable in the
 * PC simulator. Hardware builds link the real SPI mass-storage driver.
 * Parameters: none.
 * Return: -RT_ENOSYS because the simulator has no SPI TF-card controller.
 ***************************/
int rt_spi_msd_init(void)
{
    return -RT_ENOSYS;
}

/* Update the visible timer from elapsed simulator ticks.
 * Parameters: none.
 * Return value: none. */
static void PcSim_UpdateTimer(void)
{
    rt_tick_t tNow;
    uint32_t ulElapsedSeconds;

    if (0U == l_tAlarm.timer_running)
    {
        return;
    }

    tNow = rt_tick_get();
    ulElapsedSeconds = (uint32_t)((tNow - l_tTimerStartTick) / RT_TICK_PER_SECOND);
    if (ulElapsedSeconds >= l_tAlarm.timer_remaining_seconds)
    {
        l_tAlarm.timer_remaining_seconds = 0U;
        l_tAlarm.timer_running = 0U;
        l_tAlarm.timer_ringing = 1U;
    }
    else if (0U < ulElapsedSeconds)
    {
        l_tAlarm.timer_remaining_seconds -= ulElapsedSeconds;
        l_tTimerStartTick = tNow;
    }

    return;
}

/* Initialize alarm/timer services for the full PC UI.
 * Parameters: none.
 * Return value: RT_EOK. */
rt_err_t watch_alarm_service_init(void)
{
    l_tTimerStartTick = rt_tick_get();
    return RT_EOK;
}

rt_err_t watch_alarm_set(uint8_t ucEnabled, uint8_t ucHour, uint8_t ucMinute)
{
    if ((23U < ucHour) || (59U < ucMinute))
    {
        return -RT_EINVAL;
    }

    l_tAlarm.alarm_enabled = (0U != ucEnabled) ? 1U : 0U;
    l_tAlarm.alarm_hour = ucHour;
    l_tAlarm.alarm_minute = ucMinute;
    l_tSettings.alarm_enabled = l_tAlarm.alarm_enabled;
    l_tSettings.alarm_hour = ucHour;
    l_tSettings.alarm_minute = ucMinute;
    return RT_EOK;
}

rt_err_t watch_alarm_set_repeat(uint8_t ucRepeatMask)
{
    l_tAlarm.alarm_repeat_mask = ucRepeatMask & 0x7FU;
    l_tSettings.alarm_repeat_mask = l_tAlarm.alarm_repeat_mask;
    return RT_EOK;
}

rt_err_t watch_alarm_set_present(uint8_t ucPresent)
{
    l_tAlarm.alarm_present = (0U != ucPresent) ? 1U : 0U;
    l_tSettings.alarm_present = l_tAlarm.alarm_present;
    return RT_EOK;
}

rt_err_t watch_alarm_dismiss(void)
{
    l_tAlarm.alarm_ringing = 0U;
    l_tAlarm.timer_ringing = 0U;
    return RT_EOK;
}

rt_err_t watch_alarm_snooze(uint32_t ulSeconds)
{
    (void)ulSeconds;
    if (0U == l_tAlarm.alarm_ringing)
    {
        return -RT_ERROR;
    }
    l_tAlarm.alarm_ringing = 0U;
    return RT_EOK;
}

rt_err_t watch_alarm_get_snapshot(watch_alarm_snapshot_t *pSnapshot)
{
    if (RT_NULL == pSnapshot)
    {
        return -RT_EINVAL;
    }

    PcSim_UpdateTimer();
    *pSnapshot = l_tAlarm;
    return RT_EOK;
}

rt_err_t watch_timer_start(uint32_t ulSeconds)
{
    if (0U == ulSeconds)
    {
        return -RT_EINVAL;
    }

    l_tAlarm.timer_remaining_seconds = ulSeconds;
    l_tAlarm.timer_ringing = 0U;
    l_tAlarm.timer_running = 1U;
    l_tTimerStartTick = rt_tick_get();
    return RT_EOK;
}

rt_err_t watch_timer_pause(void)
{
    PcSim_UpdateTimer();
    l_tAlarm.timer_running = 0U;
    return RT_EOK;
}

rt_err_t watch_timer_reset(void)
{
    l_tAlarm.timer_remaining_seconds = PCSIM_DEFAULT_TIMER_SECONDS;
    l_tAlarm.timer_ringing = 0U;
    l_tAlarm.timer_running = 0U;
    return RT_EOK;
}

rt_err_t watch_settings_get_snapshot(watch_settings_snapshot_t *pSnapshot)
{
    if (RT_NULL == pSnapshot)
    {
        return -RT_EINVAL;
    }

    *pSnapshot = l_tSettings;
    return RT_EOK;
}

uint8_t watch_settings_get_local_volume(void)
{
    return l_tSettings.local_volume;
}

uint8_t watch_settings_get_bt_volume(void)
{
    return l_tSettings.bt_volume;
}

watch_audio_route_t watch_settings_get_route(void)
{
    return l_tSettings.route;
}

rt_err_t watch_settings_set_local_volume(uint8_t ucVolume)
{
    if (PCSIM_VOLUME_MAX < ucVolume)
    {
        return -RT_EINVAL;
    }

    l_tSettings.local_volume = ucVolume;
    return RT_EOK;
}

rt_err_t watch_settings_set_bt_volume(uint8_t ucVolume)
{
    if (PCSIM_VOLUME_MAX < ucVolume)
    {
        return -RT_EINVAL;
    }

    l_tSettings.bt_volume = ucVolume;
    return RT_EOK;
}

rt_err_t watch_settings_set_route(watch_audio_route_t eRoute)
{
    if (WATCH_AUDIO_ROUTE_SPEAKER_ONLY != eRoute)
    {
        return -RT_EINVAL;
    }

    l_tSettings.route = eRoute;
    return RT_EOK;
}

rt_err_t watch_settings_set_time_sync(int16_t sTimezoneOffsetMinutes, uint32_t ulUtcEpoch)
{
    l_tSettings.timezone_offset_minutes = sTimezoneOffsetMinutes;
    l_tSettings.last_time_sync_epoch = ulUtcEpoch;
    return RT_EOK;
}

rt_err_t watch_settings_set_alarm(uint8_t ucEnabled, uint8_t ucHour, uint8_t ucMinute)
{
    return watch_alarm_set(ucEnabled, ucHour, ucMinute);
}

rt_err_t watch_settings_apply_audio(void)
{
    return RT_EOK;
}

rt_err_t watch_settings_reset(void)
{
    l_tSettings.local_volume = WATCH_SETTINGS_DEFAULT_LOCAL_VOLUME;
    l_tSettings.bt_volume = WATCH_SETTINGS_DEFAULT_BT_VOLUME;
    l_tSettings.route = WATCH_AUDIO_ROUTE_SPEAKER_ONLY;
    return RT_EOK;
}

rt_bool_t bt_audio_sink_is_connected(void)
{
    return RT_TRUE;
}

rt_bool_t bt_audio_sink_is_streaming(void)
{
    return (BT_AUDIO_SINK_STATE_STREAMING == l_tBtHealth.state) ? RT_TRUE : RT_FALSE;
}

void bt_audio_sink_get_health(bt_audio_sink_health_t *pHealth)
{
    if (RT_NULL != pHealth)
    {
        *pHealth = l_tBtHealth;
    }

    return;
}

int bt_audio_sink_request_recovery(void)
{
    l_tBtHealth.state = BT_AUDIO_SINK_STATE_CONNECTED;
    l_tBtHealth.last_error = 0;
    l_tBtHealth.recovery_count++;
    l_tBtHealth.last_event_tick = (uint32_t)rt_tick_get();
    return RT_EOK;
}

void ble_ios_services_get_snapshot(ble_ios_services_snapshot_t *pSnapshot)
{
    if (RT_NULL != pSnapshot)
    {
        *pSnapshot = l_tIos;
    }

    return;
}

rt_err_t ble_ios_services_send_ams_cmd(uint8_t ucCmd)
{
    (void)ucCmd;
    l_tIos.ams_count++;
    return RT_EOK;
}

/***************************
 * local_music_get_snapshot: 获取PC模拟器本地音乐播放状态
 * 参数：
 *   - pSnapshot: 播放状态输出指针
 * 返回值：成功返回RT_EOK，空指针返回-RT_EINVAL
 ***************************/
int local_music_get_snapshot(LOCAL_MUSIC_SNAPSHOT *pSnapshot)
{
    if (RT_NULL == pSnapshot)
    {
        return -RT_EINVAL;
    }

    memset(pSnapshot, 0, sizeof(*pSnapshot));
    if (BT_AUDIO_SINK_STATE_STREAMING == l_tBtHealth.state)
    {
        pSnapshot->eState = LOCAL_MUSIC_STATE_PLAYING;
    }
    else
    {
        pSnapshot->eState = LOCAL_MUSIC_STATE_PAUSED;
    }
    pSnapshot->ulLastCallback = (uint32_t)rt_tick_get();

    return RT_EOK;
}

int local_music_play_file(const char *pPath, uint32_t ulLoop)
{
    (void)pPath;
    (void)ulLoop;
    l_tBtHealth.state = BT_AUDIO_SINK_STATE_STREAMING;
    return RT_EOK;
}

/***************************
 * LOCALMUSIC_PlayEffect: Simulate successful short sound-effect playback.
 * Parameters:
 *   - pPath: Sound-effect file path.
 * Return value: RT_EOK on success, -RT_EINVAL for a NULL path.
 ***************************/
int LOCALMUSIC_PlayEffect(const char *pPath)
{
    if (RT_NULL == pPath)
    {
        return -RT_EINVAL;
    }

    return RT_EOK;
}

int local_music_stop(void)
{
    l_tBtHealth.state = BT_AUDIO_SINK_STATE_CONNECTED;
    return RT_EOK;
}

int local_music_pause(void)
{
    l_tBtHealth.state = BT_AUDIO_SINK_STATE_CONNECTED;
    return RT_EOK;
}

int local_music_resume(void)
{
    l_tBtHealth.state = BT_AUDIO_SINK_STATE_STREAMING;
    return RT_EOK;
}

void badge_transfer_get_snapshot(badge_transfer_snapshot_t *pSnapshot)
{
    if (RT_NULL != pSnapshot)
    {
        *pSnapshot = l_tBadge;
    }

    return;
}

int badge_transfer_clear(void)
{
    memset(&l_tBadge, 0, sizeof(l_tBadge));
    l_tBadge.state = BADGE_TRANSFER_IDLE;
    return RT_EOK;
}

int badge_transfer_cancel(void)
{
    l_tBadge.state = BADGE_TRANSFER_IDLE;
    return RT_EOK;
}

void AGENTPETBLE_Init(void)
{
    memset(&l_tAgentStatus, 0, sizeof(l_tAgentStatus));
    l_tAgentStatus.bConnected = true;
    l_tAgentStatus.bHasSnapshot = true;
    l_tAgentStatus.ulGeneration = 1U;
    l_tAgentStatus.ulAcceptedFrameCount = 1U;
    l_tAgentStatus.tSnapshot.ucAggregateState = AGENTPET_STATE_RUNNING;
    l_tAgentStatus.tSnapshot.ucSessionCount = 2U;
    l_tAgentStatus.tSnapshot.aSessions[0].ucState = AGENTPET_STATE_RUNNING;
    l_tAgentStatus.tSnapshot.aSessions[0].ucProvider = 2U;
    l_tAgentStatus.tSnapshot.aSessions[0].ucSource = 1U;
    l_tAgentStatus.tSnapshot.aSessions[0].ucFlags = AGENTPET_TASK_FLAG_ACTIVE;
    l_tAgentStatus.tSnapshot.aSessions[0].ulTaskHash = 0x1234ABCDUL;
    l_tAgentStatus.tSnapshot.aSessions[0].usAgeSeconds = 5U;
    l_tAgentStatus.tSnapshot.aSessions[1].ucState = AGENTPET_STATE_NEEDS_INPUT;
    l_tAgentStatus.tSnapshot.aSessions[1].ucProvider = 1U;
    l_tAgentStatus.tSnapshot.aSessions[1].ucSource = 2U;
    l_tAgentStatus.tSnapshot.aSessions[1].ucFlags = AGENTPET_TASK_FLAG_APPROVAL;
    l_tAgentStatus.tSnapshot.aSessions[1].ulTaskHash = 0x5F320001UL;
    l_tAgentStatus.tSnapshot.aSessions[1].usAgeSeconds = 12U;
    l_bAgentInitialized = true;
    return;
}

bool AGENTPETBLE_RegisterService(void)
{
    if (!l_bAgentInitialized)
    {
        AGENTPETBLE_Init();
    }

    return true;
}

void AGENTPETBLE_SetConnected(bool bConnected)
{
    if (!l_bAgentInitialized)
    {
        AGENTPETBLE_Init();
    }

    l_tAgentStatus.bConnected = bConnected;
    return;
}

bool AGENTPETBLE_GetStatus(AGENTPET_BLE_STATUS *pStatus)
{
    if (NULL == pStatus)
    {
        return false;
    }

    if (!l_bAgentInitialized)
    {
        AGENTPETBLE_Init();
    }

    *pStatus = l_tAgentStatus;
    return true;
}

void AGENTPETBLE_NotifyMerit(void)
{
    return;
}

/***************************
 * audio_server_get_max_volume: 获取PC模拟器音量上限
 * 参数：无
 * 返回值：模拟器支持的最大音量等级
 ***************************/
uint8_t audio_server_get_max_volume(void)
{
    return PCSIM_VOLUME_MAX;
}
