/*
 * Persistent product settings for the Huangshan watch template.
 *
 * The SDK share_prefs layer stores values in the dedicated prefdb FlashDB
 * partition added by this project. Keep this module as the single owner of
 * product preferences so BLE commands, shell commands, and future UI controls
 * share one policy surface.
 */

#include <rtthread.h>
#include <stdlib.h>
#include <string.h>

#include "audio_server.h"
#include "share_prefs.h"
#include "watch_settings.h"

#define DBG_TAG "watch.settings"
#define DBG_LVL DBG_INFO
#include "log.h"

/*
 * share_prefs in SDK v2.4.0 copies max(31, strlen(name)) bytes when opening.
 * Use an exact 31-byte name to avoid reading past the source string.
 */
#define WATCH_SETTINGS_PREF_NAME "huangshan_watch_runtime_pref_v1"
#define WATCH_SETTINGS_KEY_LOCAL_VOL "local_vol"
#define WATCH_SETTINGS_KEY_BT_VOL "bt_vol"
#define WATCH_SETTINGS_KEY_ROUTE "route"
#define WATCH_SETTINGS_KEY_TZ_OFFSET "tz_offset"
#define WATCH_SETTINGS_KEY_TIME_SYNC "time_sync"
#define WATCH_SETTINGS_KEY_ALARM_ENABLED "alarm_enabled"
#define WATCH_SETTINGS_KEY_ALARM_HOUR "alarm_hour"
#define WATCH_SETTINGS_KEY_ALARM_MINUTE "alarm_minute"
#define WATCH_SETTINGS_KEY_ALARM_REPEAT "alarm_repeat"
#define WATCH_SETTINGS_KEY_ALARM_PRESENT "alarm_present"
#define WATCH_SETTINGS_ALARM_REPEAT_ALL (0x7FU)

typedef struct
{
    uint8_t ready;
    uint8_t storage_ready;
    share_prefs_t *prefs;
    struct rt_mutex lock;
    watch_settings_snapshot_t value;
} watch_settings_env_t;

static watch_settings_env_t g_watch_settings;

static uint8_t watch_settings_clamp_volume(int32_t volume)
{
    uint8_t max_vol = audio_server_get_max_volume();

    if (volume < 0)
    {
        return 0;
    }
    if ((uint32_t)volume > max_vol)
    {
        return max_vol;
    }

    return (uint8_t)volume;
}

static watch_audio_route_t watch_settings_clamp_route(int32_t route)
{
    switch (route)
    {
    case WATCH_AUDIO_ROUTE_SPEAKER_ONLY:
        return WATCH_AUDIO_ROUTE_SPEAKER_ONLY;
    default:
        return WATCH_AUDIO_ROUTE_SPEAKER_ONLY;
    }
}

static int16_t watch_settings_clamp_timezone(int32_t offset_minutes)
{
    if (offset_minutes < -840)
    {
        return -840;
    }
    if (offset_minutes > 840)
    {
        return 840;
    }
    return (int16_t)offset_minutes;
}

static uint8_t watch_settings_clamp_alarm_hour(int32_t hour)
{
    return hour < 0 ? 0 : (hour > 23 ? 23 : (uint8_t)hour);
}

static uint8_t watch_settings_clamp_alarm_minute(int32_t minute)
{
    return minute < 0 ? 0 : (minute > 59 ? 59 : (uint8_t)minute);
}

static void watch_settings_load_defaults(watch_settings_snapshot_t *value)
{
    value->local_volume = watch_settings_clamp_volume(WATCH_SETTINGS_DEFAULT_LOCAL_VOLUME);
    value->bt_volume = watch_settings_clamp_volume(WATCH_SETTINGS_DEFAULT_BT_VOLUME);
    value->route = WATCH_AUDIO_ROUTE_SPEAKER_ONLY;
    value->timezone_offset_minutes = 0;
    value->last_time_sync_epoch = 0;
    value->alarm_enabled = 0;
    value->alarm_hour = 7;
    value->alarm_minute = 0;
    value->alarm_repeat_mask = WATCH_SETTINGS_ALARM_REPEAT_ALL;
    value->alarm_present = 1U;
}

static rt_err_t watch_settings_save_int_locked(const char *key, int32_t value)
{
    if (!g_watch_settings.storage_ready || !g_watch_settings.prefs)
    {
        return -RT_ERROR;
    }

    return share_prefs_set_int(g_watch_settings.prefs, key, value);
}

rt_err_t watch_settings_apply_audio(void)
{
    watch_settings_snapshot_t snapshot;

    watch_settings_get_snapshot(&snapshot);
    audio_server_set_private_volume(AUDIO_TYPE_LOCAL_MUSIC, snapshot.local_volume);
    audio_server_set_private_volume(AUDIO_TYPE_BT_MUSIC, snapshot.bt_volume);

    LOG_I("applied audio local=%d bt=%d route=%d",
          snapshot.local_volume,
          snapshot.bt_volume,
          snapshot.route);
    return RT_EOK;
}

rt_err_t watch_settings_get_snapshot(watch_settings_snapshot_t *snapshot)
{
    if (!snapshot)
    {
        return -RT_EINVAL;
    }

    rt_mutex_take(&g_watch_settings.lock, RT_WAITING_FOREVER);
    *snapshot = g_watch_settings.value;
    rt_mutex_release(&g_watch_settings.lock);

    return RT_EOK;
}

uint8_t watch_settings_get_local_volume(void)
{
    watch_settings_snapshot_t snapshot;

    watch_settings_get_snapshot(&snapshot);
    return snapshot.local_volume;
}

uint8_t watch_settings_get_bt_volume(void)
{
    watch_settings_snapshot_t snapshot;

    watch_settings_get_snapshot(&snapshot);
    return snapshot.bt_volume;
}

watch_audio_route_t watch_settings_get_route(void)
{
    watch_settings_snapshot_t snapshot;

    watch_settings_get_snapshot(&snapshot);
    return snapshot.route;
}

rt_err_t watch_settings_set_local_volume(uint8_t volume)
{
    rt_err_t ret;

    volume = watch_settings_clamp_volume(volume);
    rt_mutex_take(&g_watch_settings.lock, RT_WAITING_FOREVER);
    g_watch_settings.value.local_volume = volume;
    ret = watch_settings_save_int_locked(WATCH_SETTINGS_KEY_LOCAL_VOL, volume);
    rt_mutex_release(&g_watch_settings.lock);

    audio_server_set_private_volume(AUDIO_TYPE_LOCAL_MUSIC, volume);
    LOG_I("local volume=%d save=%d", volume, ret);
    return ret == RT_EOK ? RT_EOK : ret;
}

rt_err_t watch_settings_set_bt_volume(uint8_t volume)
{
    rt_err_t ret;

    volume = watch_settings_clamp_volume(volume);
    rt_mutex_take(&g_watch_settings.lock, RT_WAITING_FOREVER);
    g_watch_settings.value.bt_volume = volume;
    ret = watch_settings_save_int_locked(WATCH_SETTINGS_KEY_BT_VOL, volume);
    rt_mutex_release(&g_watch_settings.lock);

    audio_server_set_private_volume(AUDIO_TYPE_BT_MUSIC, volume);
    LOG_I("bt volume=%d save=%d", volume, ret);
    return ret == RT_EOK ? RT_EOK : ret;
}

rt_err_t watch_settings_set_route(watch_audio_route_t route)
{
    rt_err_t ret;

    route = watch_settings_clamp_route(route);
    rt_mutex_take(&g_watch_settings.lock, RT_WAITING_FOREVER);
    g_watch_settings.value.route = route;
    ret = watch_settings_save_int_locked(WATCH_SETTINGS_KEY_ROUTE, route);
    rt_mutex_release(&g_watch_settings.lock);

    LOG_I("route=%d save=%d", route, ret);
    return ret == RT_EOK ? RT_EOK : ret;
}

rt_err_t watch_settings_set_time_sync(int16_t timezone_offset_minutes, uint32_t utc_epoch)
{
    rt_err_t timezone_ret;
    rt_err_t epoch_ret;

    timezone_offset_minutes = watch_settings_clamp_timezone(timezone_offset_minutes);
    rt_mutex_take(&g_watch_settings.lock, RT_WAITING_FOREVER);
    g_watch_settings.value.timezone_offset_minutes = timezone_offset_minutes;
    g_watch_settings.value.last_time_sync_epoch = utc_epoch;
    timezone_ret = watch_settings_save_int_locked(WATCH_SETTINGS_KEY_TZ_OFFSET, timezone_offset_minutes);
    epoch_ret = watch_settings_save_int_locked(WATCH_SETTINGS_KEY_TIME_SYNC, (int32_t)utc_epoch);
    rt_mutex_release(&g_watch_settings.lock);

    LOG_I("time sync tz=%d epoch=%lu save=%d/%d", timezone_offset_minutes,
          (unsigned long)utc_epoch, timezone_ret, epoch_ret);
    return (timezone_ret == RT_EOK && epoch_ret == RT_EOK) ? RT_EOK : -RT_ERROR;
}

rt_err_t watch_settings_set_alarm(uint8_t enabled, uint8_t hour, uint8_t minute)
{
    rt_err_t enabled_ret;
    rt_err_t hour_ret;
    rt_err_t minute_ret;

    hour = watch_settings_clamp_alarm_hour(hour);
    minute = watch_settings_clamp_alarm_minute(minute);
    rt_mutex_take(&g_watch_settings.lock, RT_WAITING_FOREVER);
    g_watch_settings.value.alarm_enabled = enabled ? 1 : 0;
    g_watch_settings.value.alarm_hour = hour;
    g_watch_settings.value.alarm_minute = minute;
    enabled_ret = watch_settings_save_int_locked(WATCH_SETTINGS_KEY_ALARM_ENABLED,
                                                 g_watch_settings.value.alarm_enabled);
    hour_ret = watch_settings_save_int_locked(WATCH_SETTINGS_KEY_ALARM_HOUR, hour);
    minute_ret = watch_settings_save_int_locked(WATCH_SETTINGS_KEY_ALARM_MINUTE, minute);
    rt_mutex_release(&g_watch_settings.lock);

    LOG_I("alarm enabled=%d time=%02d:%02d save=%d/%d/%d", enabled ? 1 : 0,
          hour, minute, enabled_ret, hour_ret, minute_ret);
    return (enabled_ret == RT_EOK && hour_ret == RT_EOK && minute_ret == RT_EOK) ?
           RT_EOK : -RT_ERROR;
}

rt_err_t watch_settings_set_alarm_repeat(uint8_t repeat_mask)
{
    rt_err_t tRet;

    repeat_mask &= WATCH_SETTINGS_ALARM_REPEAT_ALL;
    rt_mutex_take(&g_watch_settings.lock, RT_WAITING_FOREVER);
    g_watch_settings.value.alarm_repeat_mask = repeat_mask;
    tRet = watch_settings_save_int_locked(WATCH_SETTINGS_KEY_ALARM_REPEAT,
                                          repeat_mask);
    rt_mutex_release(&g_watch_settings.lock);

    LOG_I("alarm repeat=0x%02x save=%d", repeat_mask, tRet);
    return tRet;
}

rt_err_t watch_settings_set_alarm_present(uint8_t present)
{
    rt_err_t tRet;

    present = (0U != present) ? 1U : 0U;
    rt_mutex_take(&g_watch_settings.lock, RT_WAITING_FOREVER);
    g_watch_settings.value.alarm_present = present;
    tRet = watch_settings_save_int_locked(WATCH_SETTINGS_KEY_ALARM_PRESENT,
                                          present);
    rt_mutex_release(&g_watch_settings.lock);

    LOG_I("alarm present=%u save=%d", present, tRet);
    return tRet;
}

rt_err_t watch_settings_reset(void)
{
    watch_settings_snapshot_t defaults;
    rt_err_t ret = RT_EOK;

    watch_settings_load_defaults(&defaults);

    rt_mutex_take(&g_watch_settings.lock, RT_WAITING_FOREVER);
    g_watch_settings.value = defaults;
    if (g_watch_settings.storage_ready && g_watch_settings.prefs)
    {
        share_prefs_remove(g_watch_settings.prefs, WATCH_SETTINGS_KEY_LOCAL_VOL);
        share_prefs_remove(g_watch_settings.prefs, WATCH_SETTINGS_KEY_BT_VOL);
        share_prefs_remove(g_watch_settings.prefs, WATCH_SETTINGS_KEY_ROUTE);
        share_prefs_remove(g_watch_settings.prefs, WATCH_SETTINGS_KEY_TZ_OFFSET);
        share_prefs_remove(g_watch_settings.prefs, WATCH_SETTINGS_KEY_TIME_SYNC);
        share_prefs_remove(g_watch_settings.prefs, WATCH_SETTINGS_KEY_ALARM_ENABLED);
        share_prefs_remove(g_watch_settings.prefs, WATCH_SETTINGS_KEY_ALARM_HOUR);
        share_prefs_remove(g_watch_settings.prefs, WATCH_SETTINGS_KEY_ALARM_MINUTE);
        share_prefs_remove(g_watch_settings.prefs, WATCH_SETTINGS_KEY_ALARM_REPEAT);
        share_prefs_remove(g_watch_settings.prefs, WATCH_SETTINGS_KEY_ALARM_PRESENT);
    }
    else
    {
        ret = -RT_ERROR;
    }
    rt_mutex_release(&g_watch_settings.lock);

    watch_settings_apply_audio();
    LOG_I("reset ret=%d local=%d bt=%d route=%d",
          ret,
          defaults.local_volume,
          defaults.bt_volume,
          defaults.route);
    return ret;
}

static int watch_settings_init(void)
{
    watch_settings_snapshot_t defaults;

    if (g_watch_settings.ready)
    {
        return RT_EOK;
    }

    rt_mutex_init(&g_watch_settings.lock, "wset", RT_IPC_FLAG_FIFO);
    watch_settings_load_defaults(&defaults);
    g_watch_settings.value = defaults;

    g_watch_settings.prefs = share_prefs_open(WATCH_SETTINGS_PREF_NAME, SHAREPREFS_MODE_PRIVATE);
    if (g_watch_settings.prefs)
    {
        g_watch_settings.storage_ready = 1;
        g_watch_settings.value.local_volume =
            watch_settings_clamp_volume(share_prefs_get_int(g_watch_settings.prefs,
                                                            WATCH_SETTINGS_KEY_LOCAL_VOL,
                                                            defaults.local_volume));
        g_watch_settings.value.bt_volume =
            watch_settings_clamp_volume(share_prefs_get_int(g_watch_settings.prefs,
                                                            WATCH_SETTINGS_KEY_BT_VOL,
                                                            defaults.bt_volume));
        g_watch_settings.value.route =
            watch_settings_clamp_route(share_prefs_get_int(g_watch_settings.prefs,
                                                           WATCH_SETTINGS_KEY_ROUTE,
                                                           defaults.route));
        g_watch_settings.value.timezone_offset_minutes =
            watch_settings_clamp_timezone(share_prefs_get_int(g_watch_settings.prefs,
                                                              WATCH_SETTINGS_KEY_TZ_OFFSET,
                                                              defaults.timezone_offset_minutes));
        g_watch_settings.value.last_time_sync_epoch = (uint32_t)share_prefs_get_int(g_watch_settings.prefs,
                                                                                     WATCH_SETTINGS_KEY_TIME_SYNC,
                                                                                     defaults.last_time_sync_epoch);
        g_watch_settings.value.alarm_enabled = share_prefs_get_int(g_watch_settings.prefs,
                                                                    WATCH_SETTINGS_KEY_ALARM_ENABLED,
                                                                    defaults.alarm_enabled) ? 1 : 0;
        g_watch_settings.value.alarm_hour = watch_settings_clamp_alarm_hour(
            share_prefs_get_int(g_watch_settings.prefs, WATCH_SETTINGS_KEY_ALARM_HOUR,
                                 defaults.alarm_hour));
        g_watch_settings.value.alarm_minute = watch_settings_clamp_alarm_minute(
            share_prefs_get_int(g_watch_settings.prefs, WATCH_SETTINGS_KEY_ALARM_MINUTE,
                                 defaults.alarm_minute));
        g_watch_settings.value.alarm_repeat_mask =
            (uint8_t)share_prefs_get_int(g_watch_settings.prefs,
                                         WATCH_SETTINGS_KEY_ALARM_REPEAT,
                                         defaults.alarm_repeat_mask) &
            WATCH_SETTINGS_ALARM_REPEAT_ALL;
        g_watch_settings.value.alarm_present =
            (0 != share_prefs_get_int(g_watch_settings.prefs,
                                      WATCH_SETTINGS_KEY_ALARM_PRESENT,
                                      defaults.alarm_present)) ? 1U : 0U;
    }
    else
    {
        LOG_W("persistent storage unavailable, using defaults");
    }

    g_watch_settings.ready = 1;
    watch_settings_apply_audio();
    LOG_I("ready storage=%d local=%d bt=%d route=%d",
          g_watch_settings.storage_ready,
          g_watch_settings.value.local_volume,
          g_watch_settings.value.bt_volume,
          g_watch_settings.value.route);
    return RT_EOK;
}
INIT_APP_EXPORT(watch_settings_init);

static void wsettings(int argc, char **argv)
{
    watch_settings_snapshot_t snapshot;

    if (argc < 2 || strcmp(argv[1], "status") == 0)
    {
        watch_settings_get_snapshot(&snapshot);
        rt_kprintf("settings storage=%d local_vol=%d bt_vol=%d route=%d\n",
                   g_watch_settings.storage_ready,
                   snapshot.local_volume,
                   snapshot.bt_volume,
                   snapshot.route);
        rt_kprintf("usage: wsettings localvol <0-15> | btvol <0-15> | route speaker | apply | reset\n");
        return;
    }

    if (strcmp(argv[1], "localvol") == 0 && argc >= 3)
    {
        rt_kprintf("local volume ret=%d\n", watch_settings_set_local_volume((uint8_t)atoi(argv[2])));
    }
    else if (strcmp(argv[1], "btvol") == 0 && argc >= 3)
    {
        rt_kprintf("bt volume ret=%d\n", watch_settings_set_bt_volume((uint8_t)atoi(argv[2])));
    }
    else if (strcmp(argv[1], "route") == 0 && argc >= 3 && strcmp(argv[2], "speaker") == 0)
    {
        rt_kprintf("route ret=%d\n", watch_settings_set_route(WATCH_AUDIO_ROUTE_SPEAKER_ONLY));
    }
    else if (strcmp(argv[1], "apply") == 0)
    {
        rt_kprintf("apply ret=%d\n", watch_settings_apply_audio());
    }
    else if (strcmp(argv[1], "reset") == 0)
    {
        rt_kprintf("reset ret=%d\n", watch_settings_reset());
    }
    else
    {
        rt_kprintf("unknown settings command\n");
    }
}
MSH_CMD_EXPORT(wsettings, watch persistent settings command);
