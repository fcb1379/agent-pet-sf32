#ifndef BIKE_POWER_H
#define BIKE_POWER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* BIKE_POWER_STATUS: 板载电池电压采样服务运行状态。 */
typedef enum _BIKE_POWER_STATUS
{
    BIKE_POWER_STATUS_DISABLED = 0,
    BIKE_POWER_STATUS_SEARCHING,
    BIKE_POWER_STATUS_READY,
    BIKE_POWER_STATUS_ERROR
} BIKE_POWER_STATUS;

/* BIKE_POWER_FILTER: 电池电压低通与百分比回差状态。
 * 成员说明：
 *   - ulFilteredVoltageDeciMv: 一阶低通后的电压，单位 0.1 mV
 *   - ucDisplayedPercent: 已通过 2% 回差的显示百分比，范围 0~100
 *   - bInitialized: 是否已经接收首个有效电压样本
 */
typedef struct _BIKE_POWER_FILTER
{
    uint32_t ulFilteredVoltageDeciMv;
    uint8_t ucDisplayedPercent;
    bool bInitialized;
} BIKE_POWER_FILTER;

/* BIKE_POWER_SNAPSHOT: UI 可读取的电池与外部供电一致性快照。
 * 成员说明：
 *   - eStatus: ADC 初始化和采样状态
 *   - ulVoltageDeciMv: 低通后的电池电压，单位 0.1 mV
 *   - ulLastUpdateMs: 最近有效 ADC 样本的单调时间戳
 *   - ulAdcErrorCount: ADC 使能、采样或关闭失败累计次数
 *   - ulChargeErrorCount: 充电状态设备读取失败累计次数
 *   - ucPercent: 按 X-TRACK 3.3~4.1 V 模型估算的电量，范围 0~100
 *   - bExternalPower: 充电输入或 USB 电源是否存在
 *   - bFull: 充电器是否报告充满
 *   - bChargeStatusValid: 外部供电和充满状态是否有效
 */
typedef struct _BIKE_POWER_SNAPSHOT
{
    BIKE_POWER_STATUS eStatus;
    uint32_t ulVoltageDeciMv;
    uint32_t ulLastUpdateMs;
    uint32_t ulAdcErrorCount;
    uint32_t ulChargeErrorCount;
    uint8_t ucPercent;
    bool bExternalPower;
    bool bFull;
    bool bChargeStatusValid;
} BIKE_POWER_SNAPSHOT;

void BIKE_POWER_ResetFilter(BIKE_POWER_FILTER *pFilter);
uint8_t BIKE_POWER_VoltageToPercent(uint32_t ulVoltageDeciMv);
bool BIKE_POWER_UpdateFilter(BIKE_POWER_FILTER *pFilter,
                             uint32_t ulVoltageDeciMv,
                             uint32_t *pFilteredVoltageDeciMv,
                             uint8_t *pPercent);

#ifndef BIKE_POWER_HOST_BUILD
bool BIKE_POWER_Init(void);
bool BIKE_POWER_GetSnapshot(BIKE_POWER_SNAPSHOT *pSnapshot);
#endif

#ifdef __cplusplus
}
#endif

#endif /* BIKE_POWER_H */
