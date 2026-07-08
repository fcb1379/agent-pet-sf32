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

static void watch_settings_load_defaults(watch_settings_snapshot_t *value)
{
    value->local_volume = watch_settings_clamp_volume(WATCH_SETTINGS_DEFAULT_LOCAL_VOLUME);
    value->bt_volume = watch_settings_clamp_volume(WATCH_SETTINGS_DEFAULT_BT_VOLUME);
    value->route = WATCH_AUDIO_ROUTE_SPEAKER_ONLY;
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
        rt_kprintf("usage: wsettings localvol <0-15> | btvol <0-15> | route speaker | apply\n");
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
    else
    {
        rt_kprintf("unknown settings command\n");
    }
}
MSH_CMD_EXPORT(wsettings, watch persistent settings command);
