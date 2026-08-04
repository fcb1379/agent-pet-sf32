/*
 * Minimal A2DP sink bring-up for the Huangshan watch template.
 *
 * This is intentionally close to SiFli-SDK example/bt/music_sink so the
 * Bluetooth/audio stack can be validated before product UI integration.
 */

#include <rtthread.h>
#include <string.h>
#include <stdlib.h>

#include "bf0_sibles.h"
#include "bt_connection_manager.h"
#include "bts2_app_inc.h"
#include "bt_audio_sink.h"
#include "local_music_player.h"
#include "watch_settings.h"
#include "ulog.h"

#ifdef AUDIO_USING_MANAGER
#include "audio_server.h"
#endif

#define BT_AUDIO_READY 1
#define BT_AUDIO_RECOVER 2
#define BT_AUDIO_READY_WAIT_MS 8000

typedef struct
{
    bt_notify_device_mac_t addr;
    uint8_t is_a2dp_connected;
    uint8_t is_a2dp_streaming;
    uint8_t is_abs_enabled;
    uint8_t stack_ready;
    int16_t last_error;
    uint32_t disconnect_count;
    uint32_t recovery_count;
    uint32_t last_event_tick;
} watch_bt_audio_t;

static watch_bt_audio_t g_watch_bt_audio;
static rt_mailbox_t g_watch_bt_audio_mb;

extern void bt_av_snk_open(void);
extern uint8_t bt_open_bt_request(void);

static int watch_bt_audio_event_handle(uint16_t type, uint16_t event_id, uint8_t *data, uint16_t data_len)
{
    (void)data_len;

    if (type == BT_NOTIFY_COMMON)
    {
        if (event_id == BT_NOTIFY_COMMON_BT_STACK_READY)
        {
            rt_mb_send(g_watch_bt_audio_mb, BT_AUDIO_READY);
        }
    }
    else if (type == BT_NOTIFY_A2DP)
    {
        switch (event_id)
        {
        case BT_NOTIFY_A2DP_PROFILE_CONNECTED:
        {
            bt_notify_profile_state_info_t *profile_info = (bt_notify_profile_state_info_t *)data;
            if (profile_info && profile_info->res == BTS2_SUCC)
            {
                g_watch_bt_audio.addr = profile_info->mac;
                g_watch_bt_audio.is_a2dp_connected = 1;
                g_watch_bt_audio.is_a2dp_streaming = 0;
                g_watch_bt_audio.last_error = 0;
                g_watch_bt_audio.last_event_tick = rt_tick_get();
            }
            LOG_I("watch bt audio: A2DP connected");
            break;
        }
        case BT_NOTIFY_A2DP_PROFILE_DISCONNECTED:
        {
            bt_notify_profile_state_info_t *info = (bt_notify_profile_state_info_t *)data;
            g_watch_bt_audio.is_a2dp_connected = 0;
            g_watch_bt_audio.is_a2dp_streaming = 0;
            g_watch_bt_audio.disconnect_count++;
            g_watch_bt_audio.last_error = info ? info->res : -RT_ERROR;
            g_watch_bt_audio.last_event_tick = rt_tick_get();
            if (g_watch_bt_audio_mb)
            {
                rt_mb_send(g_watch_bt_audio_mb, BT_AUDIO_RECOVER);
            }
            LOG_I("watch bt audio: A2DP disconnected %d", info ? info->res : -1);
            break;
        }
        case BT_NOTIFY_A2DP_START_IND:
            g_watch_bt_audio.is_a2dp_streaming = 1;
            g_watch_bt_audio.last_event_tick = rt_tick_get();
            local_music_stop();
            LOG_I("watch bt audio: A2DP stream started, local music stopped");
            break;
        case BT_NOTIFY_A2DP_SUSPEND_IND:
            g_watch_bt_audio.is_a2dp_streaming = 0;
            g_watch_bt_audio.last_event_tick = rt_tick_get();
            LOG_I("watch bt audio: A2DP stream suspended");
            break;
        default:
            break;
        }
    }
    else if (type == BT_NOTIFY_AVRCP)
    {
        switch (event_id)
        {
        case BT_NOTIFY_AVRCP_PROFILE_CONNECTED:
        {
            bt_notify_profile_state_info_t *profile_info = (bt_notify_profile_state_info_t *)data;
            if (profile_info)
            {
                bt_interface_set_avrcp_role_ext(&profile_info->mac, AVRCP_CT);
            }
            LOG_I("watch bt audio: AVRCP connected");
            break;
        }
        case BT_NOTIFY_AVRCP_PROFILE_DISCONNECTED:
        {
            bt_notify_profile_state_info_t *info = (bt_notify_profile_state_info_t *)data;
            g_watch_bt_audio.is_abs_enabled = 0;
            LOG_I("watch bt audio: AVRCP disconnected %d", info ? info->res : -1);
            break;
        }
        case BT_NOTIFY_AVRCP_VOLUME_CHANGED_REGISTER:
            g_watch_bt_audio.is_abs_enabled = 1;
            break;
        case BT_NOTIFY_AVRCP_ABSOLUTE_VOLUME:
        {
#ifdef AUDIO_USING_MANAGER
            uint8_t *volume = (uint8_t *)data;
            uint8_t local_vol = bt_interface_avrcp_abs_vol_2_local_vol(*volume, audio_server_get_max_volume());
            audio_server_set_private_volume(AUDIO_TYPE_BT_MUSIC, local_vol);
#endif
            break;
        }
        default:
            break;
        }
    }

    return 0;
}

static void watch_bt_audio_thread(void *parameter)
{
    (void)parameter;

    uint32_t value = 0;

    g_watch_bt_audio_mb = rt_mb_create("wbt_audio", 8, RT_IPC_FLAG_FIFO);
    RT_ASSERT(g_watch_bt_audio_mb);

    bt_interface_register_bt_event_notify_callback(watch_bt_audio_event_handle);

    sifli_ble_enable();

    while (1)
    {
        rt_err_t ret = rt_mb_recv(g_watch_bt_audio_mb,
                                  (rt_uint32_t *)&value,
                                  rt_tick_from_millisecond(BT_AUDIO_READY_WAIT_MS));

        if (ret != RT_EOK)
        {
            if (!g_watch_bt_audio.stack_ready)
            {
                g_watch_bt_audio.last_error = -RT_ETIMEOUT;
                g_watch_bt_audio.last_event_tick = rt_tick_get();
                LOG_E("watch bt audio: stack init still waiting");
            }
            continue;
        }

        if (value == BT_AUDIO_READY && !g_watch_bt_audio.stack_ready)
        {
#ifndef AGENT_PET_DISABLE_CLASSIC_BT_AUDIO
            const char *local_name = "Huangshan-Watch";
#endif

            g_watch_bt_audio.stack_ready = 1;
            g_watch_bt_audio.last_error = 0;
            g_watch_bt_audio.last_event_tick = rt_tick_get();
#ifdef AGENT_PET_DISABLE_CLASSIC_BT_AUDIO
            LOG_I("watch bt audio: BLE stack ready, classic audio disabled");
#else
            bt_interface_set_local_name(strlen(local_name), (void *)local_name);
            bt_interface_register_av_snk_sdp();
            bt_av_snk_open();
            bt_interface_open_avrcp();
            watch_settings_apply_audio();
            (void)bt_open_bt_request();
            LOG_I("watch bt audio: stack ready, name=%s", local_name);
#endif
        }
#ifndef AGENT_PET_DISABLE_CLASSIC_BT_AUDIO
        else if (value == BT_AUDIO_RECOVER && g_watch_bt_audio.stack_ready)
        {
            g_watch_bt_audio.recovery_count++;
            g_watch_bt_audio.last_event_tick = rt_tick_get();
            (void)bt_open_bt_request();
            LOG_I("watch bt audio: recovery scan requested, count=%lu",
                  (unsigned long)g_watch_bt_audio.recovery_count);
        }
#endif
    }
}

static int watch_bt_audio_init(void)
{
    rt_thread_t tid = rt_thread_create("wbt_audio",
                                       watch_bt_audio_thread,
                                       RT_NULL,
                                       4096,
                                       22,
                                       10);
    if (tid)
    {
        rt_thread_startup(tid);
        return RT_EOK;
    }

    return -RT_ERROR;
}
INIT_APP_EXPORT(watch_bt_audio_init);

rt_bool_t bt_audio_sink_is_connected(void)
{
    return g_watch_bt_audio.is_a2dp_connected ? RT_TRUE : RT_FALSE;
}

rt_bool_t bt_audio_sink_is_streaming(void)
{
    return g_watch_bt_audio.is_a2dp_streaming ? RT_TRUE : RT_FALSE;
}

void bt_audio_sink_get_health(bt_audio_sink_health_t *health)
{
    if (!health)
    {
        return;
    }

    health->last_error = g_watch_bt_audio.last_error;
    health->disconnect_count = g_watch_bt_audio.disconnect_count;
    health->recovery_count = g_watch_bt_audio.recovery_count;
    health->last_event_tick = g_watch_bt_audio.last_event_tick;

    if (g_watch_bt_audio.is_a2dp_streaming)
    {
        health->state = BT_AUDIO_SINK_STATE_STREAMING;
    }
    else if (g_watch_bt_audio.is_a2dp_connected)
    {
        health->state = BT_AUDIO_SINK_STATE_CONNECTED;
    }
    else if (g_watch_bt_audio.stack_ready)
    {
        health->state = BT_AUDIO_SINK_STATE_READY;
    }
    else if (g_watch_bt_audio.last_error)
    {
        health->state = BT_AUDIO_SINK_STATE_ERROR;
    }
    else
    {
        health->state = BT_AUDIO_SINK_STATE_STARTING;
    }
}

int bt_audio_sink_request_recovery(void)
{
    if (!g_watch_bt_audio_mb || !g_watch_bt_audio.stack_ready)
    {
        return -RT_EBUSY;
    }

    return rt_mb_send(g_watch_bt_audio_mb, BT_AUDIO_RECOVER);
}

__ROM_USED void btaudio(int argc, char **argv)
{
    if (argc < 2)
    {
        rt_kprintf("btaudio status|recover|discover|open|clear|vol <0-15>\n");
        return;
    }

    if (strcmp(argv[1], "status") == 0)
    {
        bt_audio_sink_health_t health;

        bt_audio_sink_get_health(&health);
        rt_kprintf("A2DP: %s, stream: %s, abs_vol: %s\n",
                   g_watch_bt_audio.is_a2dp_connected ? "connected" : "disconnected",
                   g_watch_bt_audio.is_a2dp_streaming ? "started" : "stopped",
                   g_watch_bt_audio.is_abs_enabled ? "enabled" : "disabled");
        rt_kprintf("BT music volume=%d/%d\n",
                   watch_settings_get_bt_volume(),
                   audio_server_get_max_volume());
        rt_kprintf("health state=%d err=%d disconnects=%lu recoveries=%lu last_tick=%lu\n",
                   health.state,
                   health.last_error,
                   (unsigned long)health.disconnect_count,
                   (unsigned long)health.recovery_count,
                   (unsigned long)health.last_event_tick);
    }
    else if (strcmp(argv[1], "recover") == 0)
    {
        rt_kprintf("BT recovery request=%d\n", bt_audio_sink_request_recovery());
    }
    else if (strcmp(argv[1], "discover") == 0)
    {
        (void)bt_open_bt_request();
        rt_kprintf("Classic BT inquiry/page scan enabled\n");
    }
    else if (strcmp(argv[1], "open") == 0)
    {
        bt_interface_register_av_snk_sdp();
        bt_av_snk_open();
        bt_interface_open_avrcp();
        (void)bt_open_bt_request();
        rt_kprintf("A2DP sink/AVRCP opened and scan enabled\n");
    }
    else if (strcmp(argv[1], "clear") == 0)
    {
#ifdef BSP_BT_CONNECTION_MANAGER
        bt_cm_delete_bonded_devs();
        rt_kprintf("BT bonded devices cleared\n");
#endif
    }
    else if (strcmp(argv[1], "vol") == 0 && argc >= 3)
    {
#ifdef AUDIO_USING_MANAGER
        uint8_t local_vol = (uint8_t)atoi(argv[2]);
        uint8_t max_vol = audio_server_get_max_volume();
        if (local_vol > max_vol)
        {
            local_vol = max_vol;
        }
        watch_settings_set_bt_volume(local_vol);
        if (g_watch_bt_audio.is_abs_enabled)
        {
            uint8_t abs_vol = bt_interface_avrcp_local_vol_2_abs_vol(local_vol, max_vol);
            bt_interface_avrcp_set_absolute_volume_as_ct_role(abs_vol);
        }
        rt_kprintf("BT music volume=%d/%d\n", local_vol, max_vol);
#endif
    }
}
MSH_CMD_EXPORT(btaudio, watch BT audio command);
