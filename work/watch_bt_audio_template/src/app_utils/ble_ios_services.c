/*
 * iOS BLE services bridge for the Huangshan watch template.
 *
 * Subscribes to the SDK ANCS and AMS data services. The services themselves
 * handle Apple's ANCS/AMS GATT procedures after the BLE link is bonded.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <string.h>
#include <stdlib.h>

#include "bf0_sibles.h"
#include "bf0_ble_ancs.h"
#include "bf0_ble_ams.h"
#include "data_service_subscriber.h"
#include "ancs_service.h"
#include "ams_service.h"
#include "ble_ios_services.h"

#define LOG_TAG "ios_svc"
#include "log.h"

typedef struct
{
    datac_handle_t ancs_handle;
    datac_handle_t ams_handle;
    uint32_t ancs_count;
    uint32_t ams_count;
    uint32_t last_uid;
    uint8_t last_category;
    char last_app[BLE_IOS_TEXT_APP_LEN];
    char last_title[BLE_IOS_TEXT_TITLE_LEN];
    char last_message[BLE_IOS_TEXT_MESSAGE_LEN];
    char player[BLE_IOS_TEXT_MEDIA_LEN];
    char playback[BLE_IOS_TEXT_MEDIA_LEN];
    char artist[BLE_IOS_TEXT_MEDIA_LEN];
    char album[BLE_IOS_TEXT_MEDIA_LEN];
    char track[BLE_IOS_TEXT_MEDIA_LEN];
    char duration[BLE_IOS_TEXT_MEDIA_LEN];
    char volume[BLE_IOS_TEXT_MEDIA_LEN];
} ble_ios_env_t;

static ble_ios_env_t g_ble_ios_env;

static ble_ios_env_t *ble_ios_env(void)
{
    return &g_ble_ios_env;
}

static void ios_copy_text(char *dst, size_t dst_len, const uint8_t *src, uint16_t src_len)
{
    size_t copy_len;

    if (!dst || dst_len == 0)
    {
        return;
    }

    if (!src || src_len == 0)
    {
        dst[0] = '\0';
        return;
    }

    copy_len = src_len;
    if (copy_len >= dst_len)
    {
        copy_len = dst_len - 1;
    }

    rt_memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';
}

void ble_ios_services_get_snapshot(ble_ios_services_snapshot_t *snapshot)
{
    rt_base_t level;
    ble_ios_env_t *env = ble_ios_env();

    if (!snapshot)
    {
        return;
    }

    level = rt_hw_interrupt_disable();
    snapshot->ancs_count = env->ancs_count;
    snapshot->ams_count = env->ams_count;
    snapshot->last_uid = env->last_uid;
    snapshot->last_category = env->last_category;
    rt_strncpy(snapshot->last_app, env->last_app, sizeof(snapshot->last_app) - 1);
    rt_strncpy(snapshot->last_title, env->last_title, sizeof(snapshot->last_title) - 1);
    rt_strncpy(snapshot->last_message, env->last_message, sizeof(snapshot->last_message) - 1);
    rt_strncpy(snapshot->player, env->player, sizeof(snapshot->player) - 1);
    rt_strncpy(snapshot->playback, env->playback, sizeof(snapshot->playback) - 1);
    rt_strncpy(snapshot->artist, env->artist, sizeof(snapshot->artist) - 1);
    rt_strncpy(snapshot->album, env->album, sizeof(snapshot->album) - 1);
    rt_strncpy(snapshot->track, env->track, sizeof(snapshot->track) - 1);
    rt_strncpy(snapshot->duration, env->duration, sizeof(snapshot->duration) - 1);
    rt_strncpy(snapshot->volume, env->volume, sizeof(snapshot->volume) - 1);
    rt_hw_interrupt_enable(level);

    snapshot->last_app[sizeof(snapshot->last_app) - 1] = '\0';
    snapshot->last_title[sizeof(snapshot->last_title) - 1] = '\0';
    snapshot->last_message[sizeof(snapshot->last_message) - 1] = '\0';
    snapshot->player[sizeof(snapshot->player) - 1] = '\0';
    snapshot->playback[sizeof(snapshot->playback) - 1] = '\0';
    snapshot->artist[sizeof(snapshot->artist) - 1] = '\0';
    snapshot->album[sizeof(snapshot->album) - 1] = '\0';
    snapshot->track[sizeof(snapshot->track) - 1] = '\0';
    snapshot->duration[sizeof(snapshot->duration) - 1] = '\0';
    snapshot->volume[sizeof(snapshot->volume) - 1] = '\0';
}

#ifdef BSP_USING_ANCS_SVC
static void ble_ios_config_ancs(void)
{
    ble_ios_env_t *env = ble_ios_env();
    ancs_service_config_t config;
    rt_err_t ret;

    rt_memset(&config, 0, sizeof(config));
    config.command = ANCS_SERVICE_SET_ATTRIBUTE_MASK;
    config.data.attr_mask = BLE_ANCS_NOTIFICATION_ATTR_ID_MASK_ALL;
    ret = datac_config(env->ancs_handle, sizeof(config), (uint8_t *)&config);
    LOG_I("ANCS attr mask ret=%d", ret);

    rt_memset(&config, 0, sizeof(config));
    config.command = ANCS_SERVICE_SET_CATEGORY_MASK;
    config.data.cate_mask = BLE_ANCS_CATEGORY_ID_MASK_ALL;
    ret = datac_config(env->ancs_handle, sizeof(config), (uint8_t *)&config);
    LOG_I("ANCS category mask ret=%d", ret);

    rt_memset(&config, 0, sizeof(config));
    config.command = ANCS_SERVICE_ENABLE_CCCD;
    config.data.enable_cccd = 1;
    ret = datac_config(env->ancs_handle, sizeof(config), (uint8_t *)&config);
    LOG_I("ANCS cccd ret=%d", ret);
}

static int ble_ios_ancs_callback(data_callback_arg_t *arg)
{
    ble_ios_env_t *env = ble_ios_env();

    if (arg->msg_id == MSG_SERVICE_SUBSCRIBE_RSP)
    {
        data_subscribe_rsp_t *rsp = (data_subscribe_rsp_t *)arg->data;
        LOG_I("ANCS subscribe ret=%d", rsp ? rsp->result : -1);
        if (rsp && rsp->result == 0)
        {
            ble_ios_config_ancs();
        }
    }
    else if (arg->msg_id == MSG_SERVICE_DATA_NTF_IND)
    {
        ancs_service_noti_attr_t *noti = (ancs_service_noti_attr_t *)arg->data;
        ble_ancs_attr_value_t *attr;

        if (!noti || arg->data_len < sizeof(ancs_service_noti_attr_t))
        {
            return 0;
        }

        env->ancs_count++;
        env->last_uid = noti->noti_uid;
        env->last_category = noti->cate_id;
        env->last_app[0] = '\0';
        env->last_title[0] = '\0';
        env->last_message[0] = '\0';

        attr = &noti->value[0];
        for (uint8_t i = 0; i < noti->attr_count; i++)
        {
            if ((uint8_t *)attr >= arg->data + arg->data_len)
            {
                break;
            }

            switch (attr->attr_id)
            {
            case BLE_ANCS_NOTIFICATION_ATTR_ID_APP_ID:
                ios_copy_text(env->last_app, sizeof(env->last_app), attr->data, attr->len);
                break;
            case BLE_ANCS_NOTIFICATION_ATTR_ID_TITLE:
                ios_copy_text(env->last_title, sizeof(env->last_title), attr->data, attr->len);
                break;
            case BLE_ANCS_NOTIFICATION_ATTR_ID_MESSAGE:
                ios_copy_text(env->last_message, sizeof(env->last_message), attr->data, attr->len);
                break;
            default:
                break;
            }

            attr = (ble_ancs_attr_value_t *)((uint8_t *)attr + sizeof(ble_ancs_attr_value_t) + attr->len);
        }

        LOG_I("ANCS #%lu cate=%d uid=%lu app=%s title=%s msg=%s",
              (unsigned long)env->ancs_count,
              env->last_category,
              (unsigned long)env->last_uid,
              env->last_app,
              env->last_title,
              env->last_message);
    }

    return 0;
}
#endif

#ifdef BSP_USING_AMS_SVC
static void ble_ios_config_ams(void)
{
    ble_ios_env_t *env = ble_ios_env();
    ams_service_config_t config;
    rt_err_t ret;

    rt_memset(&config, 0, sizeof(config));
    config.command = AMS_SERVICE_SET_PLAYER_ATTRIBUTE_MASK;
    config.data.player_mask = BLE_AMS_PLAYER_ATTR_ID_ALL_MASK;
    ret = datac_config(env->ams_handle, sizeof(config), (uint8_t *)&config);
    LOG_I("AMS player mask ret=%d", ret);

    rt_memset(&config, 0, sizeof(config));
    config.command = AMS_SERVICE_SET_QUEUE_ATTRIBUTE_MASK;
    config.data.queue_mask = BLE_AMS_QUEUE_ATTR_ID_ALL_MASK;
    ret = datac_config(env->ams_handle, sizeof(config), (uint8_t *)&config);
    LOG_I("AMS queue mask ret=%d", ret);

    rt_memset(&config, 0, sizeof(config));
    config.command = AMS_SERVICE_SET_TRACK_ATTRIBUTE_MASK;
    config.data.track_mask = BLE_AMS_TRACK_ATTR_ID_ALL_MASK;
    ret = datac_config(env->ams_handle, sizeof(config), (uint8_t *)&config);
    LOG_I("AMS track mask ret=%d", ret);

    rt_memset(&config, 0, sizeof(config));
    config.command = AMS_SERVICE_ENABLE_CCCD;
    config.data.enable_cccd = 1;
    ret = datac_config(env->ams_handle, sizeof(config), (uint8_t *)&config);
    LOG_I("AMS cccd ret=%d", ret);
}

static void ble_ios_store_ams_attr(ble_ams_entity_attr_value_t *value)
{
    ble_ios_env_t *env = ble_ios_env();

    switch (value->entity_id)
    {
    case BLE_AMS_ENTITY_ID_PLAYER:
        if (value->attr_id == BLE_AMS_PLAYER_ATTR_ID_NAME)
        {
            ios_copy_text(env->player, sizeof(env->player), value->value, value->len);
        }
        else if (value->attr_id == BLE_AMS_PLAYER_ATTR_ID_PB_INFO)
        {
            ios_copy_text(env->playback, sizeof(env->playback), value->value, value->len);
        }
        else if (value->attr_id == BLE_AMS_PLAYER_ATTR_ID_VOL)
        {
            ios_copy_text(env->volume, sizeof(env->volume), value->value, value->len);
        }
        break;
    case BLE_AMS_ENTITY_ID_TRACK:
        if (value->attr_id == BLE_AMS_TRACK_ATTR_ID_ARTIST)
        {
            ios_copy_text(env->artist, sizeof(env->artist), value->value, value->len);
        }
        else if (value->attr_id == BLE_AMS_TRACK_ATTR_ID_ALBUM)
        {
            ios_copy_text(env->album, sizeof(env->album), value->value, value->len);
        }
        else if (value->attr_id == BLE_AMS_TRACK_ATTR_ID_TILTE)
        {
            ios_copy_text(env->track, sizeof(env->track), value->value, value->len);
        }
        else if (value->attr_id == BLE_AMS_TRACK_ATTR_ID_DURATION)
        {
            ios_copy_text(env->duration, sizeof(env->duration), value->value, value->len);
        }
        break;
    default:
        break;
    }
}

static int ble_ios_ams_callback(data_callback_arg_t *arg)
{
    ble_ios_env_t *env = ble_ios_env();

    if (arg->msg_id == MSG_SERVICE_SUBSCRIBE_RSP)
    {
        data_subscribe_rsp_t *rsp = (data_subscribe_rsp_t *)arg->data;
        LOG_I("AMS subscribe ret=%d", rsp ? rsp->result : -1);
        if (rsp && rsp->result == 0)
        {
            ble_ios_config_ams();
        }
    }
    else if (arg->msg_id == MSG_SERVICE_DATA_NTF_IND)
    {
        ble_ams_entity_attr_value_t *value = (ble_ams_entity_attr_value_t *)arg->data;

        if (!value || arg->data_len < sizeof(ble_ams_entity_attr_value_t))
        {
            return 0;
        }

        env->ams_count++;
        ble_ios_store_ams_attr(value);
        LOG_I("AMS #%lu entity=%d attr=%d player=%s track=%s artist=%s",
              (unsigned long)env->ams_count,
              value->entity_id,
              value->attr_id,
              env->player,
              env->track,
              env->artist);
    }

    return 0;
}

rt_err_t ble_ios_services_send_ams_cmd(uint8_t cmd)
{
    ble_ios_env_t *env = ble_ios_env();
    ams_service_config_t config;

    if (env->ams_handle == DATA_CLIENT_INVALID_HANDLE)
    {
        return -RT_ERROR;
    }

    rt_memset(&config, 0, sizeof(config));
    config.command = AMS_SERVICE_SEND_REMOTE_COMMAND;
    config.data.remote_cmd = (ble_ams_cmd_t)cmd;
    return datac_config(env->ams_handle, sizeof(config), (uint8_t *)&config);
}
#endif

static int ble_ios_services_init(void)
{
    ble_ios_env_t *env = ble_ios_env();

    env->ancs_handle = DATA_CLIENT_INVALID_HANDLE;
    env->ams_handle = DATA_CLIENT_INVALID_HANDLE;

#ifdef BSP_USING_ANCS_SVC
    env->ancs_handle = datac_open();
    RT_ASSERT(DATA_CLIENT_INVALID_HANDLE != env->ancs_handle);
    datac_subscribe(env->ancs_handle, "ANCS", ble_ios_ancs_callback, 0);
#endif

#ifdef BSP_USING_AMS_SVC
    env->ams_handle = datac_open();
    RT_ASSERT(DATA_CLIENT_INVALID_HANDLE != env->ams_handle);
    datac_subscribe(env->ams_handle, "AMS", ble_ios_ams_callback, 0);
#endif

    LOG_I("iOS services bridge init ancs=%d ams=%d", env->ancs_handle, env->ams_handle);
    return RT_EOK;
}
INIT_APP_EXPORT(ble_ios_services_init);

__ROM_USED void iossvc(int argc, char **argv)
{
    ble_ios_env_t *env = ble_ios_env();

    if (argc < 2 || strcmp(argv[1], "status") == 0)
    {
        rt_kprintf("ANCS count:%lu cate:%d uid:%lu app:%s title:%s msg:%s\n",
                   (unsigned long)env->ancs_count,
                   env->last_category,
                   (unsigned long)env->last_uid,
                   env->last_app,
                   env->last_title,
                   env->last_message);
        rt_kprintf("AMS count:%lu player:%s track:%s artist:%s album:%s playback:%s\n",
                   (unsigned long)env->ams_count,
                   env->player,
                   env->track,
                   env->artist,
                   env->album,
                   env->playback);
    }
#ifdef BSP_USING_AMS_SVC
    else if (strcmp(argv[1], "play") == 0)
    {
        rt_kprintf("AMS play ret:%d\n", ble_ios_services_send_ams_cmd(BLE_AMS_CMD_PLAY));
    }
    else if (strcmp(argv[1], "pause") == 0)
    {
        rt_kprintf("AMS pause ret:%d\n", ble_ios_services_send_ams_cmd(BLE_AMS_CMD_PAUSE));
    }
    else if (strcmp(argv[1], "toggle") == 0)
    {
        rt_kprintf("AMS toggle ret:%d\n", ble_ios_services_send_ams_cmd(BLE_AMS_CMD_TOGGLE_PLAY_PAUSE));
    }
    else if (strcmp(argv[1], "next") == 0)
    {
        rt_kprintf("AMS next ret:%d\n", ble_ios_services_send_ams_cmd(BLE_AMS_CMD_NEXT));
    }
    else if (strcmp(argv[1], "prev") == 0)
    {
        rt_kprintf("AMS prev ret:%d\n", ble_ios_services_send_ams_cmd(BLE_AMS_CMD_PREV));
    }
    else if (strcmp(argv[1], "volup") == 0)
    {
        rt_kprintf("AMS volup ret:%d\n", ble_ios_services_send_ams_cmd(BLE_AMS_CMD_VOL_UP));
    }
    else if (strcmp(argv[1], "voldown") == 0)
    {
        rt_kprintf("AMS voldown ret:%d\n", ble_ios_services_send_ams_cmd(BLE_AMS_CMD_VOL_DOWN));
    }
    else if (strcmp(argv[1], "cmd") == 0 && argc >= 3)
    {
        int cmd = atoi(argv[2]);
        if (cmd >= 0 && cmd < BLE_AMS_CMD_TOTAL)
        {
            rt_kprintf("AMS cmd %d ret:%d\n", cmd, ble_ios_services_send_ams_cmd((uint8_t)cmd));
        }
        else
        {
            rt_kprintf("AMS cmd out of range: %d\n", cmd);
        }
    }
#endif
}
MSH_CMD_EXPORT(iossvc, iOS ANCS/AMS service command);
