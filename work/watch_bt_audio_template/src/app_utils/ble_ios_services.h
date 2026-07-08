#ifndef __BLE_IOS_SERVICES_H
#define __BLE_IOS_SERVICES_H

#include <rtthread.h>

#define BLE_IOS_TEXT_APP_LEN      32
#define BLE_IOS_TEXT_TITLE_LEN    96
#define BLE_IOS_TEXT_MESSAGE_LEN  160
#define BLE_IOS_TEXT_MEDIA_LEN    96

typedef struct
{
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
} ble_ios_services_snapshot_t;

void ble_ios_services_get_snapshot(ble_ios_services_snapshot_t *snapshot);
rt_err_t ble_ios_services_send_ams_cmd(uint8_t cmd);

#endif
