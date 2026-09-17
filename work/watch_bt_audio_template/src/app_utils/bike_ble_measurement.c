#include "bike_ble_measurement.h"

#include <stddef.h>
#include <string.h>

#define BIKE_BLE_HR_FLAG_VALUE_16BIT (0x01U)
#define BIKE_BLE_HR_FLAG_ENERGY_PRESENT (0x08U)
#define BIKE_BLE_HR_FLAG_RR_PRESENT (0x10U)
#define BIKE_BLE_CSC_KNOWN_FLAGS (BIKE_CSC_WHEEL_DATA_PRESENT | \
                                  BIKE_CSC_CRANK_DATA_PRESENT)
#define BIKE_BLE_POWER_KNOWN_FLAGS (0x1FFFU)
#define BIKE_BLE_POWER_PEDAL_BALANCE (0x0001U)
#define BIKE_BLE_POWER_ACCUMULATED_TORQUE (0x0004U)
#define BIKE_BLE_POWER_WHEEL_REVOLUTIONS (0x0010U)
#define BIKE_BLE_POWER_CRANK_REVOLUTIONS (0x0020U)
#define BIKE_BLE_POWER_EXTREME_FORCE (0x0040U)
#define BIKE_BLE_POWER_EXTREME_TORQUE (0x0080U)
#define BIKE_BLE_POWER_EXTREME_ANGLES (0x0100U)
#define BIKE_BLE_POWER_TOP_DEAD_SPOT (0x0200U)
#define BIKE_BLE_POWER_BOTTOM_DEAD_SPOT (0x0400U)
#define BIKE_BLE_POWER_ACCUMULATED_ENERGY (0x0800U)

/* BikeBleMeas_Read16: 读取小端 16 位整数。
 * 参数：
 *   - pData: 至少两个字节的输入
 * 返回值：小端整数
 */
static uint16_t BikeBleMeas_Read16(const uint8_t *pData)
{
    return (uint16_t)pData[0] | ((uint16_t)pData[1] << 8U);
}

/* BikeBleMeas_Read32: 读取小端 32 位整数。
 * 参数：
 *   - pData: 至少四个字节的输入
 * 返回值：小端整数
 */
static uint32_t BikeBleMeas_Read32(const uint8_t *pData)
{
    return (uint32_t)pData[0] |
           ((uint32_t)pData[1] << 8U) |
           ((uint32_t)pData[2] << 16U) |
           ((uint32_t)pData[3] << 24U);
}

/* BIKE_BLE_MEAS_ParseHeartRate: 安全解析 Heart Rate Measurement。
 * 参数：
 *   - pData: GATT characteristic value
 *   - usLength: 数据长度
 *   - pHeartRateBpm: 输出心率
 * 返回值：完整且合法返回 true，否则返回 false
 */
bool BIKE_BLE_MEAS_ParseHeartRate(const uint8_t *pData, uint16_t usLength,
                                  uint16_t *pHeartRateBpm)
{
    uint16_t usOffset;
    uint8_t ucFlags;

    if ((NULL == pData) || (NULL == pHeartRateBpm) || (2U > usLength))
    {
        return false;
    }
    ucFlags = pData[0];
    usOffset = 1U;
    if (0U != (ucFlags & BIKE_BLE_HR_FLAG_VALUE_16BIT))
    {
        if (2U > (uint16_t)(usLength - usOffset))
        {
            return false;
        }
        *pHeartRateBpm = BikeBleMeas_Read16(&pData[usOffset]);
        usOffset = (uint16_t)(usOffset + 2U);
    }
    else
    {
        *pHeartRateBpm = pData[usOffset];
        usOffset++;
    }

    if (0U != (ucFlags & BIKE_BLE_HR_FLAG_ENERGY_PRESENT))
    {
        if (2U > (uint16_t)(usLength - usOffset))
        {
            return false;
        }
        usOffset = (uint16_t)(usOffset + 2U);
    }
    if (0U != (ucFlags & BIKE_BLE_HR_FLAG_RR_PRESENT))
    {
        if ((2U > (uint16_t)(usLength - usOffset)) ||
            (0U != ((usLength - usOffset) & 1U)))
        {
            return false;
        }

        return true;
    }

    return usOffset == usLength;
}

/* BIKE_BLE_MEAS_ParseCsc: 安全解析 CSC Measurement。
 * 参数：
 *   - pData: GATT characteristic value
 *   - usLength: 数据长度
 *   - pMeasurement: 输出的累计轮/曲柄数据
 * 返回值：完整且仅含已知字段返回 true，否则返回 false
 */
bool BIKE_BLE_MEAS_ParseCsc(const uint8_t *pData, uint16_t usLength,
                           BIKE_CSC_MEASUREMENT *pMeasurement)
{
    uint16_t usOffset;
    uint8_t ucFlags;

    if ((NULL == pData) || (NULL == pMeasurement) || (1U > usLength))
    {
        return false;
    }
    (void)memset(pMeasurement, 0, sizeof(*pMeasurement));
    ucFlags = pData[0];
    if (0U != (ucFlags & (uint8_t)(~BIKE_BLE_CSC_KNOWN_FLAGS)))
    {
        return false;
    }
    pMeasurement->ucFlags = ucFlags;
    usOffset = 1U;

    if (0U != (ucFlags & BIKE_CSC_WHEEL_DATA_PRESENT))
    {
        if (6U > (uint16_t)(usLength - usOffset))
        {
            return false;
        }
        pMeasurement->ulCumulativeWheelRevolutions =
            BikeBleMeas_Read32(&pData[usOffset]);
        usOffset = (uint16_t)(usOffset + 4U);
        pMeasurement->usLastWheelEventTime =
            BikeBleMeas_Read16(&pData[usOffset]);
        usOffset = (uint16_t)(usOffset + 2U);
    }
    if (0U != (ucFlags & BIKE_CSC_CRANK_DATA_PRESENT))
    {
        if (4U > (uint16_t)(usLength - usOffset))
        {
            return false;
        }
        pMeasurement->usCumulativeCrankRevolutions =
            BikeBleMeas_Read16(&pData[usOffset]);
        usOffset = (uint16_t)(usOffset + 2U);
        pMeasurement->usLastCrankEventTime =
            BikeBleMeas_Read16(&pData[usOffset]);
        usOffset = (uint16_t)(usOffset + 2U);
    }

    return usOffset == usLength;
}

/* BIKE_BLE_MEAS_ParseCyclingPower: 安全解析 Cycling Power Measurement。
 * 参数：
 *   - pData: GATT characteristic value
 *   - usLength: 数据长度
 *   - pPowerWatts: 输出瞬时功率，单位 W
 * 返回值：完整且仅含标准字段返回 true，否则返回 false
 */
bool BIKE_BLE_MEAS_ParseCyclingPower(const uint8_t *pData,
                                    uint16_t usLength,
                                    int16_t *pPowerWatts)
{
    uint16_t usFlags;
    uint16_t usOffset;
    uint16_t usPowerRaw;
    int32_t lPowerWatts;

    if ((NULL == pData) || (NULL == pPowerWatts) || (4U > usLength))
    {
        return false;
    }
    usFlags = BikeBleMeas_Read16(pData);
    if (0U != (usFlags & (uint16_t)(~BIKE_BLE_POWER_KNOWN_FLAGS)))
    {
        return false;
    }

    usOffset = 4U;
    if (0U != (usFlags & BIKE_BLE_POWER_PEDAL_BALANCE))
    {
        usOffset = (uint16_t)(usOffset + 1U);
    }
    if (0U != (usFlags & BIKE_BLE_POWER_ACCUMULATED_TORQUE))
    {
        usOffset = (uint16_t)(usOffset + 2U);
    }
    if (0U != (usFlags & BIKE_BLE_POWER_WHEEL_REVOLUTIONS))
    {
        usOffset = (uint16_t)(usOffset + 6U);
    }
    if (0U != (usFlags & BIKE_BLE_POWER_CRANK_REVOLUTIONS))
    {
        usOffset = (uint16_t)(usOffset + 4U);
    }
    if (0U != (usFlags & BIKE_BLE_POWER_EXTREME_FORCE))
    {
        usOffset = (uint16_t)(usOffset + 4U);
    }
    if (0U != (usFlags & BIKE_BLE_POWER_EXTREME_TORQUE))
    {
        usOffset = (uint16_t)(usOffset + 4U);
    }
    if (0U != (usFlags & BIKE_BLE_POWER_EXTREME_ANGLES))
    {
        usOffset = (uint16_t)(usOffset + 3U);
    }
    if (0U != (usFlags & BIKE_BLE_POWER_TOP_DEAD_SPOT))
    {
        usOffset = (uint16_t)(usOffset + 2U);
    }
    if (0U != (usFlags & BIKE_BLE_POWER_BOTTOM_DEAD_SPOT))
    {
        usOffset = (uint16_t)(usOffset + 2U);
    }
    if (0U != (usFlags & BIKE_BLE_POWER_ACCUMULATED_ENERGY))
    {
        usOffset = (uint16_t)(usOffset + 2U);
    }
    if (usOffset != usLength)
    {
        return false;
    }

    usPowerRaw = BikeBleMeas_Read16(&pData[2]);
    lPowerWatts = (int32_t)usPowerRaw;
    if (INT16_MAX < usPowerRaw)
    {
        lPowerWatts -= 65536L;
    }
    *pPowerWatts = (int16_t)lPowerWatts;

    return true;
}

/* BIKE_BLE_MEAS_ParseBatteryLevel: 校验标准 Battery Level 值。
 * 参数：
 *   - pData: GATT characteristic value
 *   - usLength: 数据长度，标准值必须正好一个字节
 *   - pBatteryPercent: 输出电量百分比
 * 返回值：0~100 的单字节值返回 true，否则返回 false
 */
bool BIKE_BLE_MEAS_ParseBatteryLevel(const uint8_t *pData,
                                     uint16_t usLength,
                                     uint8_t *pBatteryPercent)
{
    if ((NULL == pData) || (NULL == pBatteryPercent) || (1U != usLength) ||
        (100U < pData[0]))
    {
        return false;
    }
    *pBatteryPercent = pData[0];

    return true;
}
