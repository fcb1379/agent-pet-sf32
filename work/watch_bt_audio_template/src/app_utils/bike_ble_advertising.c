#include "bike_ble_advertising.h"

#include <stddef.h>

#define BIKE_BLE_AD_TYPE_INCOMPLETE_UUID16 (0x02U)
#define BIKE_BLE_AD_TYPE_COMPLETE_UUID16 (0x03U)
#define BIKE_BLE_AD_TYPE_SERVICE_DATA_UUID16 (0x16U)
#define BIKE_BLE_UUID_HEART_RATE (0x180DU)
#define BIKE_BLE_UUID_CSC (0x1816U)
#define BIKE_BLE_UUID_CYCLING_POWER (0x1818U)

/* BikeBleAdv_ServiceMaskFromUuid: 将标准 16 位服务 UUID 转为码表服务位。
 * 参数：
 *   - usUuid: Bluetooth SIG 16 位服务 UUID
 * 返回值：匹配的 BIKE_BLE_SERVICE_* 位；不支持时返回 0
 */
static uint8_t BikeBleAdv_ServiceMaskFromUuid(uint16_t usUuid)
{
    uint8_t ucMask;

    ucMask = 0U;
    if (BIKE_BLE_UUID_HEART_RATE == usUuid)
    {
        ucMask = BIKE_BLE_SERVICE_HEART_RATE;
    }
    else if (BIKE_BLE_UUID_CSC == usUuid)
    {
        ucMask = BIKE_BLE_SERVICE_CSC;
    }
    else if (BIKE_BLE_UUID_CYCLING_POWER == usUuid)
    {
        ucMask = BIKE_BLE_SERVICE_POWER;
    }

    return ucMask;
}

/* BIKE_BLE_ADV_GetServiceMask: 安全解析 BLE 广播 AD 结构中的骑行传感器服务。
 * 参数：
 *   - pData: 广播或扫描响应数据
 *   - usLength: 数据长度
 * 返回值：匹配的 BIKE_BLE_SERVICE_* 位；格式错误或无目标服务时返回 0
 */
uint8_t BIKE_BLE_ADV_GetServiceMask(const uint8_t *pData, uint16_t usLength)
{
    uint16_t usOffset;
    uint16_t usFieldEnd;
    uint16_t usUuidOffset;
    uint16_t usUuid;
    uint8_t ucFieldLength;
    uint8_t ucFieldType;
    uint8_t ucMask;

    if ((NULL == pData) || (0U == usLength))
    {
        return 0U;
    }

    usOffset = 0U;
    ucMask = 0U;
    while (usOffset < usLength)
    {
        ucFieldLength = pData[usOffset];
        if (0U == ucFieldLength)
        {
            break;
        }
        usFieldEnd = (uint16_t)(usOffset + 1U + ucFieldLength);
        if ((usFieldEnd <= usOffset) || (usLength < usFieldEnd) ||
            (2U > ucFieldLength))
        {
            return 0U;
        }

        ucFieldType = pData[usOffset + 1U];
        if ((BIKE_BLE_AD_TYPE_INCOMPLETE_UUID16 == ucFieldType) ||
            (BIKE_BLE_AD_TYPE_COMPLETE_UUID16 == ucFieldType))
        {
            if (0U != ((ucFieldLength - 1U) & 1U))
            {
                return 0U;
            }
            usUuidOffset = (uint16_t)(usOffset + 2U);
            while ((uint16_t)(usUuidOffset + 1U) < usFieldEnd)
            {
                usUuid = (uint16_t)pData[usUuidOffset] |
                         ((uint16_t)pData[usUuidOffset + 1U] << 8U);
                ucMask |= BikeBleAdv_ServiceMaskFromUuid(usUuid);
                usUuidOffset = (uint16_t)(usUuidOffset + 2U);
            }
        }
        else if ((BIKE_BLE_AD_TYPE_SERVICE_DATA_UUID16 == ucFieldType) &&
                 (3U <= ucFieldLength))
        {
            usUuid = (uint16_t)pData[usOffset + 2U] |
                     ((uint16_t)pData[usOffset + 3U] << 8U);
            ucMask |= BikeBleAdv_ServiceMaskFromUuid(usUuid);
        }

        usOffset = usFieldEnd;
    }

    return ucMask;
}
