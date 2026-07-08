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
#include "ulog.h"

#ifdef AUDIO_USING_MANAGER
#include "audio_server.h"
#endif

#define BT_AUDIO_READY 1

typedef struct
{
    bt_notify_device_mac_t addr;
    uint8_t is_a2dp_connected;
    uint8_t is_abs_enabled;
} watch_bt_audio_t;

static watch_bt_audio_t g_watch_bt_audio;
static rt_mailbox_t g_watch_bt_audio_mb;

extern uint8_t bt_open_bt_request(void);
extern void bt_av_snk_open(void);

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
            }
            LOG_I("watch bt audio: A2DP connected");
            break;
        }
        case BT_NOTIFY_A2DP_PROFILE_DISCONNECTED:
        {
            bt_notify_profile_state_info_t *info = (bt_notify_profile_state_info_t *)data;
            g_watch_bt_audio.is_a2dp_connected = 0;
            LOG_I("watch bt audio: A2DP disconnected %d", info ? info->res : -1);
            break;
        }
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

    if (RT_EOK == rt_mb_recv(g_watch_bt_audio_mb, (rt_uint32_t *)&value, 8000) && value == BT_AUDIO_READY)
    {
        const char *local_name = "Huangshan-Watch";
        bt_interface_set_local_name(strlen(local_name), (void *)local_name);
        bt_interface_register_av_snk_sdp();
        bt_av_snk_open();
        bt_interface_open_avrcp();
        bt_open_bt_request();
        LOG_I("watch bt audio: stack ready, name=%s", local_name);
    }
    else
    {
        LOG_E("watch bt audio: stack init timeout");
    }

    while (1)
    {
        rt_thread_mdelay(15000);
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

__ROM_USED void btaudio(int argc, char **argv)
{
    if (argc < 2)
    {
        rt_kprintf("btaudio status|discover|open|clear|vol <0-15>\n");
        return;
    }

    if (strcmp(argv[1], "status") == 0)
    {
        rt_kprintf("A2DP: %s, abs_vol: %s\n",
                   g_watch_bt_audio.is_a2dp_connected ? "connected" : "disconnected",
                   g_watch_bt_audio.is_abs_enabled ? "enabled" : "disabled");
    }
    else if (strcmp(argv[1], "discover") == 0)
    {
        bt_open_bt_request();
        rt_kprintf("Classic BT inquiry/page scan enabled\n");
    }
    else if (strcmp(argv[1], "open") == 0)
    {
        bt_interface_register_av_snk_sdp();
        bt_av_snk_open();
        bt_interface_open_avrcp();
        bt_open_bt_request();
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
        audio_server_set_private_volume(AUDIO_TYPE_BT_MUSIC, local_vol);
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
