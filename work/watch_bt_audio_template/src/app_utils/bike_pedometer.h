#ifndef BIKE_PEDOMETER_H
#define BIKE_PEDOMETER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* BIKE_PEDOMETER_STATUS: 板载计步传感器运行状态。 */
typedef enum _BIKE_PEDOMETER_STATUS
{
    BIKE_PEDOMETER_STATUS_DISABLED = 0,
    BIKE_PEDOMETER_STATUS_SEARCHING,
    BIKE_PEDOMETER_STATUS_READY,
    BIKE_PEDOMETER_STATUS_ERROR
} BIKE_PEDOMETER_STATUS;

/* BIKE_PEDOMETER_ACCUMULATOR: 将 16 位硬件计步值扩展为饱和 32 位累计值。
 * 成员说明：
 *   - ulTotalSteps: 累计步数，达到 UINT32_MAX 后保持饱和
 *   - usPreviousRaw: 上一次通过基线更新的硬件原始计数
 *   - bInitialized: 是否已有硬件计数基线
 */
typedef struct _BIKE_PEDOMETER_ACCUMULATOR
{
    uint32_t ulTotalSteps;
    uint16_t usPreviousRaw;
    bool bInitialized;
} BIKE_PEDOMETER_ACCUMULATOR;

/* BIKE_PEDOMETER_SNAPSHOT: UI 可读取的板载计步器一致性快照。
 * 成员说明：
 *   - eStatus: 初始化和采样状态
 *   - ulStepCount: 本次上电后的扩展累计步数
 *   - ulLastUpdateMs: 最近有效采样的单调时间戳
 *   - ulReadErrorCount: I2C 读取失败或异常跳变的累计次数
 *   - usRawStepCount: 最近一次硬件 16 位原始计数
 */
typedef struct _BIKE_PEDOMETER_SNAPSHOT
{
    BIKE_PEDOMETER_STATUS eStatus;
    uint32_t ulStepCount;
    uint32_t ulLastUpdateMs;
    uint32_t ulReadErrorCount;
    uint16_t usRawStepCount;
} BIKE_PEDOMETER_SNAPSHOT;

void BIKE_PEDOMETER_ResetAccumulator(BIKE_PEDOMETER_ACCUMULATOR *pAccumulator);
bool BIKE_PEDOMETER_UpdateAccumulator(BIKE_PEDOMETER_ACCUMULATOR *pAccumulator,
                                      uint16_t usRawStepCount,
                                      uint16_t usMaximumDelta);

#ifndef BIKE_PEDOMETER_HOST_BUILD
bool BIKE_PEDOMETER_Init(void);
bool BIKE_PEDOMETER_GetSnapshot(BIKE_PEDOMETER_SNAPSHOT *pSnapshot);
#endif

#ifdef __cplusplus
}
#endif

#endif /* BIKE_PEDOMETER_H */
