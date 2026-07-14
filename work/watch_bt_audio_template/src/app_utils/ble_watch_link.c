/*
 * BLE phone-link foundation for the Huangshan watch template.
 *
 * Provides a connectable advertisement and a tiny custom GATT service that can
 * be exercised with tools such as nRF Connect or LightBlue before the phone app
 * protocol, ANCS, and AMS are layered on top.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <string.h>
#include <stdlib.h>

#include "bf0_ble_gap.h"
#include "bf0_ble_ams.h"
#include "bf0_sibles.h"
#include "bf0_sibles_internal.h"
#include "bf0_sibles_advertising.h"
#include "bf0_sibles_serial_trans_service.h"
#include "ble_connection_manager.h"
#include "ble_ios_services.h"
#include "badge_transfer.h"
#include "bt_audio_sink.h"
#include "local_music_player.h"
#include "watch_settings.h"

#define LOG_TAG "ble_link"
#include "log.h"

#define BLE_LINK_ADV_NAME "Huangshan-Watch-BLE"
#define BLE_LINK_NOTIFY_INTERVAL_MS 5000

enum ble_link_att_list
{
    BLE_LINK_SVC,
    BLE_LINK_CHAR,
    BLE_LINK_CHAR_VALUE,
    BLE_LINK_CCCD,
    BLE_LINK_ATT_NB
};

#define BLE_LINK_SVC_UUID \
    { 0x48, 0x53, 0x57, 0x41, 0x54, 0x43, 0x48, 0x5f, \
      0x4c, 0x49, 0x4e, 0x4b, 0x00, 0x00, 0x00, 0x01 }

#define BLE_LINK_CHAR_UUID \
    { 0x48, 0x53, 0x57, 0x41, 0x54, 0x43, 0x48, 0x5f, \
      0x4c, 0x49, 0x4e, 0x4b, 0x00, 0x00, 0x00, 0x02 }

#define UUID_16_LE(x) { ((uint8_t)((x) & 0xff)), ((uint8_t)((x) >> 8)) }

typedef struct
{
    uint8_t is_power_on;
    uint8_t is_connected;
    uint8_t conn_idx;
    uint8_t notify_enabled;
    uint16_t mtu;
    uint16_t conn_interval;
    uint32_t rx_count;
    uint32_t tx_count;
    char last_payload[64];
    sibles_hdl srv_handle;
    rt_mailbox_t mb_handle;
    rt_timer_t notify_timer;
} ble_link_env_t;

static ble_link_env_t g_ble_link_env;
static uint8_t g_ble_link_svc_uuid[ATT_UUID_128_LEN] = BLE_LINK_SVC_UUID;

BLE_GATT_SERVICE_DEFINE_128(ble_link_att_db)
{
    BLE_GATT_SERVICE_DECLARE(BLE_LINK_SVC, UUID_16_LE(0x2800), BLE_GATT_PERM_READ_ENABLE),
    BLE_GATT_CHAR_DECLARE(BLE_LINK_CHAR, UUID_16_LE(0x2803), BLE_GATT_PERM_READ_ENABLE),
    BLE_GATT_CHAR_VALUE_DECLARE(BLE_LINK_CHAR_VALUE, BLE_LINK_CHAR_UUID,
                                BLE_GATT_PERM_READ_ENABLE | BLE_GATT_PERM_WRITE_REQ_ENABLE |
                                BLE_GATT_PERM_WRITE_COMMAND_ENABLE | BLE_GATT_PERM_NOTIFY_ENABLE,
                                BLE_GATT_VALUE_PERM_UUID_128 | BLE_GATT_VALUE_PERM_RI_ENABLE,
                                sizeof(g_ble_link_env.last_payload)),
    BLE_GATT_DESCRIPTOR_DECLARE(BLE_LINK_CCCD, UUID_16_LE(0x2902),
                                BLE_GATT_PERM_READ_ENABLE | BLE_GATT_PERM_WRITE_REQ_ENABLE,
                                BLE_GATT_VALUE_PERM_RI_ENABLE, 2),
};

SIBLES_ADVERTISING_CONTEXT_DECLAR(g_ble_link_adv_context);

static ble_link_env_t *ble_link_env(void)
{
    return &g_ble_link_env;
}

static uint8_t ble_link_adv_event(uint8_t event, void *context, void *data)
{
    (void)context;

    switch (event)
    {
    case SIBLES_ADV_EVT_ADV_STARTED:
    {
        sibles_adv_evt_startted_t *evt = (sibles_adv_evt_startted_t *)data;
        LOG_I("BLE adv started status=%d mode=%d", evt->status, evt->adv_mode);
        break;
    }
    case SIBLES_ADV_EVT_ADV_STOPPED:
    {
        sibles_adv_evt_stopped_t *evt = (sibles_adv_evt_stopped_t *)data;
        LOG_I("BLE adv stopped reason=%d mode=%d", evt->reason, evt->adv_mode);
        break;
    }
    default:
        break;
    }

    return 0;
}

static void ble_link_advertising_start(void)
{
    sibles_advertising_para_t para = {0};
    const char *local_name = BLE_LINK_ADV_NAME;
    uint8_t manu_data[] = { 0x48, 0x53, 0x57, 0x01 };
    ble_gap_dev_name_t *dev_name;
    uint8_t ret;

    dev_name = rt_malloc(sizeof(ble_gap_dev_name_t) + strlen(local_name));
    if (dev_name)
    {
        dev_name->len = strlen(local_name);
        rt_memcpy(dev_name->name, local_name, dev_name->len);
        ble_gap_set_dev_name(dev_name);
        rt_free(dev_name);
    }

    para.own_addr_type = GAPM_STATIC_ADDR;
    para.config.adv_mode = SIBLES_ADV_CONNECT_MODE;
    para.config.mode_config.conn_config.duration = 0;
    para.config.mode_config.conn_config.interval = 0x30;
    para.config.max_tx_pwr = 0x7F;
    para.config.is_auto_restart = 1;

    para.rsp_data.completed_name = rt_malloc(rt_strlen(local_name) + sizeof(sibles_adv_type_name_t));
    if (para.rsp_data.completed_name)
    {
        para.rsp_data.completed_name->name_len = rt_strlen(local_name);
        rt_memcpy(para.rsp_data.completed_name->name, local_name, para.rsp_data.completed_name->name_len);
    }

    para.adv_data.manufacturer_data = rt_malloc(sizeof(sibles_adv_type_manufacturer_data_t) + sizeof(manu_data));
    if (para.adv_data.manufacturer_data)
    {
        para.adv_data.manufacturer_data->company_id = SIG_SIFLI_COMPANY_ID;
        para.adv_data.manufacturer_data->data_len = sizeof(manu_data);
        rt_memcpy(para.adv_data.manufacturer_data->additional_data, manu_data, sizeof(manu_data));
    }

    para.evt_handler = ble_link_adv_event;
    ret = sibles_advertising_init(g_ble_link_adv_context, &para);
    if (ret == SIBLES_ADV_NO_ERR)
    {
        sibles_advertising_start(g_ble_link_adv_context);
    }
    else
    {
        LOG_E("BLE adv init failed %d", ret);
    }

    if (para.rsp_data.completed_name)
    {
        rt_free(para.rsp_data.completed_name);
    }
    if (para.adv_data.manufacturer_data)
    {
        rt_free(para.adv_data.manufacturer_data);
    }
}

static uint8_t *ble_link_gatts_get_cbk(uint8_t conn_idx, uint8_t idx, uint16_t *len)
{
    ble_link_env_t *env = ble_link_env();

    (void)conn_idx;
    *len = 0;

    switch (idx)
    {
    case BLE_LINK_CHAR_VALUE:
        *len = strlen(env->last_payload);
        return (uint8_t *)env->last_payload;
    default:
        return NULL;
    }
}

static void ble_link_notify(const char *text)
{
    ble_link_env_t *env = ble_link_env();
    sibles_value_t value;

    if (!env->is_connected || !env->notify_enabled || !env->srv_handle)
    {
        return;
    }

    value.hdl = env->srv_handle;
    value.idx = BLE_LINK_CHAR_VALUE;
    value.len = strlen(text);
    value.value = (uint8_t *)text;

    if (sibles_write_value(env->conn_idx, &value) == value.len)
    {
        env->tx_count++;
    }
}

static int ble_link_ams_cmd_from_name(const char *name, uint8_t *cmd)
{
    if (strcmp(name, "play") == 0)
    {
        *cmd = BLE_AMS_CMD_PLAY;
    }
    else if (strcmp(name, "pause") == 0)
    {
        *cmd = BLE_AMS_CMD_PAUSE;
    }
    else if (strcmp(name, "toggle") == 0)
    {
        *cmd = BLE_AMS_CMD_TOGGLE_PLAY_PAUSE;
    }
    else if (strcmp(name, "next") == 0)
    {
        *cmd = BLE_AMS_CMD_NEXT;
    }
    else if (strcmp(name, "prev") == 0)
    {
        *cmd = BLE_AMS_CMD_PREV;
    }
    else if (strcmp(name, "volup") == 0)
    {
        *cmd = BLE_AMS_CMD_VOL_UP;
    }
    else if (strcmp(name, "voldown") == 0)
    {
        *cmd = BLE_AMS_CMD_VOL_DOWN;
    }
    else
    {
        return -RT_ERROR;
    }

    return RT_EOK;
}

static void ble_link_handle_command(const char *payload)
{
    ble_link_env_t *env = ble_link_env();
    ble_ios_services_snapshot_t ios;
    char cmd[64];
    char *argv[4];
    char *token;
    char *saveptr = NULL;
    int argc = 0;
    char rsp[96];

    rt_strncpy(cmd, payload, sizeof(cmd) - 1);
    cmd[sizeof(cmd) - 1] = '\0';

    token = strtok_r(cmd, " \r\n\t", &saveptr);
    while (token && argc < (int)(sizeof(argv) / sizeof(argv[0])))
    {
        argv[argc++] = token;
        token = strtok_r(NULL, " \r\n\t", &saveptr);
    }

    if (argc == 0)
    {
        ble_link_notify("err:empty");
        return;
    }

    if (strcmp(argv[0], "ping") == 0)
    {
        ble_link_notify("ok:pong");
    }
    else if (strcmp(argv[0], "status") == 0)
    {
        watch_settings_snapshot_t settings;

        ble_ios_services_get_snapshot(&ios);
        watch_settings_get_snapshot(&settings);
        rt_snprintf(rsp, sizeof(rsp), "st:ble=%d,bt=%d,str=%d,an=%lu,am=%lu,lv=%d,bv=%d,rt=%d",
                    env->is_connected,
                    bt_audio_sink_is_connected(),
                    bt_audio_sink_is_streaming(),
                    (unsigned long)ios.ancs_count,
                    (unsigned long)ios.ams_count,
                    settings.local_volume,
                    settings.bt_volume,
                    settings.route);
        ble_link_notify(rsp);
    }
    else if (strcmp(argv[0], "settings") == 0)
    {
        watch_settings_snapshot_t settings;

        watch_settings_get_snapshot(&settings);
        rt_snprintf(rsp, sizeof(rsp), "settings:local=%d,bt=%d,route=%d",
                    settings.local_volume,
                    settings.bt_volume,
                    settings.route);
        ble_link_notify(rsp);
    }
    else if (strcmp(argv[0], "health") == 0)
    {
        bt_audio_sink_health_t health;

        bt_audio_sink_get_health(&health);
        rt_snprintf(rsp, sizeof(rsp), "health:bt=%d,err=%d,dc=%lu,rc=%lu,t=%lu",
                    health.state,
                    health.last_error,
                    (unsigned long)health.disconnect_count,
                    (unsigned long)health.recovery_count,
                    (unsigned long)health.last_event_tick);
        ble_link_notify(rsp);
    }
    else if (strcmp(argv[0], "badge") == 0)
    {
        badge_transfer_snapshot_t badge;

        if (argc >= 2 && strcmp(argv[1], "clear") == 0)
        {
            int ret = badge_transfer_clear();

            rt_snprintf(rsp, sizeof(rsp), "badge:clear:%d", ret);
            ble_link_notify(rsp);
            return;
        }

        if (argc >= 2 && strcmp(argv[1], "cancel") == 0)
        {
            int ret = badge_transfer_cancel();

            rt_snprintf(rsp, sizeof(rsp), "badge:cancel:%d", ret);
            ble_link_notify(rsp);
            return;
        }

        badge_transfer_get_snapshot(&badge);
        rt_snprintf(rsp, sizeof(rsp), "badge:s=%d,img=%d,rx=%lu,total=%lu,gen=%lu,act=%lu,err=%d",
                    badge.state,
                    badge.image_available,
                    (unsigned long)badge.received,
                    (unsigned long)badge.total,
                    (unsigned long)badge.generation,
                    (unsigned long)badge.last_activity_tick,
                    badge.last_error);
        ble_link_notify(rsp);
    }
    else if (strcmp(argv[0], "recover") == 0 && argc >= 2 && strcmp(argv[1], "bt") == 0)
    {
        int ret = bt_audio_sink_request_recovery();

        rt_snprintf(rsp, sizeof(rsp), "recover:bt:%d", ret);
        ble_link_notify(rsp);
    }
    else if (strcmp(argv[0], "set") == 0 && argc >= 3)
    {
        int ret = -RT_ERROR;

        if (strcmp(argv[1], "localvol") == 0)
        {
            ret = watch_settings_set_local_volume((uint8_t)atoi(argv[2]));
        }
        else if (strcmp(argv[1], "btvol") == 0)
        {
            ret = watch_settings_set_bt_volume((uint8_t)atoi(argv[2]));
        }
        else if (strcmp(argv[1], "route") == 0 && strcmp(argv[2], "speaker") == 0)
        {
            ret = watch_settings_set_route(WATCH_AUDIO_ROUTE_SPEAKER_ONLY);
        }

        rt_snprintf(rsp, sizeof(rsp), "set:%s:%d", argv[1], ret);
        ble_link_notify(rsp);
    }
    else if (strcmp(argv[0], "reset") == 0 && argc >= 2 && strcmp(argv[1], "settings") == 0)
    {
        int ret = watch_settings_reset();

        rt_snprintf(rsp, sizeof(rsp), "reset:settings:%d", ret);
        ble_link_notify(rsp);
    }
    else if (strcmp(argv[0], "local") == 0 && argc >= 2)
    {
        int ret = -RT_ERROR;

        if (strcmp(argv[1], "play") == 0)
        {
            ret = local_music_play_file(argc >= 3 ? argv[2] : NULL, 0);
        }
        else if (strcmp(argv[1], "stop") == 0)
        {
            ret = local_music_stop();
        }
        else if (strcmp(argv[1], "pause") == 0)
        {
            ret = local_music_pause();
        }
        else if (strcmp(argv[1], "resume") == 0)
        {
            ret = local_music_resume();
        }

        rt_snprintf(rsp, sizeof(rsp), "local:%s:%d", argv[1], ret);
        ble_link_notify(rsp);
    }
    else if (strcmp(argv[0], "ams") == 0 && argc >= 2)
    {
        uint8_t ams_cmd;
        int ret;

        if (ble_link_ams_cmd_from_name(argv[1], &ams_cmd) == RT_EOK)
        {
            ret = ble_ios_services_send_ams_cmd(ams_cmd);
            rt_snprintf(rsp, sizeof(rsp), "ams:%s:%d", argv[1], ret);
        }
        else
        {
            rt_snprintf(rsp, sizeof(rsp), "err:ams:%s", argv[1]);
        }
        ble_link_notify(rsp);
    }
    else
    {
        rt_snprintf(rsp, sizeof(rsp), "echo:%s", payload);
        ble_link_notify(rsp);
    }
}

static uint8_t ble_link_gatts_set_cbk(uint8_t conn_idx, sibles_set_cbk_t *para)
{
    ble_link_env_t *env = ble_link_env();

    switch (para->idx)
    {
    case BLE_LINK_CHAR_VALUE:
    {
        uint16_t copy_len = para->len;
        if (copy_len >= sizeof(env->last_payload))
        {
            copy_len = sizeof(env->last_payload) - 1;
        }

        rt_memcpy(env->last_payload, para->value, copy_len);
        env->last_payload[copy_len] = '\0';
        env->rx_count++;
        LOG_I("BLE write conn=%d len=%d payload=%s", conn_idx, para->len, env->last_payload);

        ble_link_handle_command(env->last_payload);
        break;
    }
    case BLE_LINK_CCCD:
        env->notify_enabled = para->value[0] & 0x01;
        LOG_I("BLE notify %s", env->notify_enabled ? "enabled" : "disabled");
        if (env->notify_enabled)
        {
            rt_timer_start(env->notify_timer);
            ble_link_notify("watch-ble-ready");
        }
        else
        {
            rt_timer_stop(env->notify_timer);
        }
        break;
    default:
        break;
    }

    return 0;
}

static void ble_link_service_init(void)
{
    ble_link_env_t *env = ble_link_env();
    BLE_GATT_SERVICE_INIT_128(svc, ble_link_att_db, BLE_LINK_ATT_NB,
                              BLE_GATT_SERVICE_PERM_NOAUTH |
                              BLE_GATT_SERVICE_PERM_UUID_128 |
                              BLE_GATT_SERVICE_PERM_MULTI_LINK,
                              g_ble_link_svc_uuid);

    env->srv_handle = sibles_register_svc_128(&svc);
    if (env->srv_handle)
    {
        sibles_register_cbk(env->srv_handle, ble_link_gatts_get_cbk, ble_link_gatts_set_cbk);
        rt_strncpy(env->last_payload, "watch-ble-ready", sizeof(env->last_payload) - 1);
        LOG_I("BLE link service registered");
    }
    else
    {
        LOG_E("BLE link service register failed");
    }
}

static void ble_link_notify_timeout(void *parameter)
{
    ble_link_env_t *env = ble_link_env();
    char text[32];

    (void)parameter;
    if (env->notify_enabled)
    {
        rt_snprintf(text, sizeof(text), "tick:%lu", (unsigned long)rt_tick_get());
        ble_link_notify(text);
        rt_timer_start(env->notify_timer);
    }
}

static int ble_link_event_handler(uint16_t event_id, uint8_t *data, uint16_t len, uint32_t context)
{
    ble_link_env_t *env = ble_link_env();

    (void)len;
    (void)context;

    switch (event_id)
    {
    case BLE_POWER_ON_IND:
        if (env->mb_handle)
        {
            rt_mb_send(env->mb_handle, BLE_POWER_ON_IND);
        }
        break;
    case BLE_GAP_CONNECTED_IND:
    {
        ble_gap_connect_ind_t *ind = (ble_gap_connect_ind_t *)data;

        env->is_connected = 1;
        env->conn_idx = ind->conn_idx;
        env->conn_interval = ind->con_interval;
        LOG_I("BLE connected conn=%d interval=%d", env->conn_idx, env->conn_interval);
        break;
    }
    case BLE_GAP_DISCONNECTED_IND:
    {
        ble_gap_disconnected_ind_t *ind = (ble_gap_disconnected_ind_t *)data;
        env->is_connected = 0;
        env->notify_enabled = 0;
        rt_timer_stop(env->notify_timer);
        LOG_I("BLE disconnected reason=%d", ind->reason);
        break;
    }
    case SIBLES_MTU_EXCHANGE_IND:
    {
        sibles_mtu_exchange_ind_t *ind = (sibles_mtu_exchange_ind_t *)data;
        env->mtu = ind->mtu;
        LOG_I("BLE MTU=%d", env->mtu);
        break;
    }
    case CONNECTION_MANAGER_BOND_AUTH_INFOR:
    {
        connection_manager_bond_ack_infor_t *info = (connection_manager_bond_ack_infor_t *)data;
        connection_manager_bond_ack_reply(info->conn_idx, info->request, true);
        break;
    }
    default:
        break;
    }

    return 0;
}
BLE_EVENT_REGISTER(ble_link_event_handler, NULL);

static void ble_link_thread(void *parameter)
{
    ble_link_env_t *env = ble_link_env();

    (void)parameter;
    while (1)
    {
        uint32_t value;
        rt_mb_recv(env->mb_handle, (rt_uint32_t *)&value, RT_WAITING_FOREVER);
        if (value == BLE_POWER_ON_IND && !env->is_power_on)
        {
            env->is_power_on = 1;
            env->mtu = 23;
            connection_manager_set_bond_ack(BOND_PENDING);
            connection_manager_set_bond_cnf_iocap(GAP_IO_CAP_NO_INPUT_NO_OUTPUT);
            ble_link_service_init();
            ble_serial_tran_init();
            ble_link_advertising_start();
            LOG_I("BLE link ready name=%s", BLE_LINK_ADV_NAME);
        }
    }
}

static int ble_link_init(void)
{
    ble_link_env_t *env = ble_link_env();
    rt_thread_t tid;

    env->mb_handle = rt_mb_create("blelink", 8, RT_IPC_FLAG_FIFO);
    RT_ASSERT(env->mb_handle);

    env->notify_timer = rt_timer_create("blenfy",
                                        ble_link_notify_timeout,
                                        RT_NULL,
                                        rt_tick_from_millisecond(BLE_LINK_NOTIFY_INTERVAL_MS),
                                        RT_TIMER_FLAG_SOFT_TIMER);
    RT_ASSERT(env->notify_timer);

    tid = rt_thread_create("blelink",
                           ble_link_thread,
                           RT_NULL,
                           4096,
                           23,
                           10);
    if (tid)
    {
        rt_thread_startup(tid);
        return RT_EOK;
    }

    return -RT_ERROR;
}
INIT_APP_EXPORT(ble_link_init);

__ROM_USED void blelink(int argc, char **argv)
{
    ble_link_env_t *env = ble_link_env();

    if (argc < 2 || strcmp(argv[1], "status") == 0)
    {
        rt_kprintf("BLE power:%d conn:%d idx:%d mtu:%d notify:%d rx:%lu tx:%lu last:%s\n",
                   env->is_power_on,
                   env->is_connected,
                   env->conn_idx,
                   env->mtu,
                   env->notify_enabled,
                   (unsigned long)env->rx_count,
                   (unsigned long)env->tx_count,
                   env->last_payload);
        rt_kprintf("BLE writes: ping | status | health | badge [clear] | recover bt | settings | set localvol|btvol <0-15> | set route speaker | reset settings | local play|stop|pause|resume | ams play|pause|toggle|next|prev|volup|voldown\n");
    }
    else if (strcmp(argv[1], "adv") == 0)
    {
        ble_link_advertising_start();
        rt_kprintf("BLE advertising requested: %s\n", BLE_LINK_ADV_NAME);
    }
    else if (strcmp(argv[1], "notify") == 0)
    {
        const char *text = argc >= 3 ? argv[2] : "watch-notify";
        ble_link_notify(text);
        rt_kprintf("BLE notify requested: %s\n", text);
    }
    else if (strcmp(argv[1], "clear") == 0)
    {
        connection_manager_delete_all_bond();
        rt_kprintf("BLE bonded devices cleared\n");
    }
}
MSH_CMD_EXPORT(blelink, watch BLE link command);
