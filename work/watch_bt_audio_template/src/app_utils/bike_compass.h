#ifndef BIKE_COMPASS_H
#define BIKE_COMPASS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* BIKE_COMPASS_STATUS: 板载磁力计和电子罗盘运行状态。 */
typedef enum _BIKE_COMPASS_STATUS
{
    BIKE_COMPASS_STATUS_DISABLED = 0,
    BIKE_COMPASS_STATUS_SEARCHING,
    BIKE_COMPASS_STATUS_CALIBRATING,
    BIKE_COMPASS_STATUS_READY,
    BIKE_COMPASS_STATUS_ERROR
} BIKE_COMPASS_STATUS;

/* BIKE_COMPASS_CALIBRATION: 水平面硬铁偏移运行时校准状态。
 * 成员说明：
 *   - lMinimumX/lMaximumX: X 轴已观测原始计数范围
 *   - lMinimumY/lMaximumY: Y 轴已观测原始计数范围
 *   - ulSampleCount: 已接受样本数，达到 UINT32_MAX 后保持饱和
 *   - bInitialized: 是否已经接收首个样本
 */
typedef struct _BIKE_COMPASS_CALIBRATION
{
    int32_t lMinimumX;
    int32_t lMaximumX;
    int32_t lMinimumY;
    int32_t lMaximumY;
    uint32_t ulSampleCount;
    bool bInitialized;
} BIKE_COMPASS_CALIBRATION;

/* BIKE_COMPASS_SNAPSHOT: UI 可读取的板载电子罗盘一致性快照。
 * 成员说明：
 *   - eStatus: 初始化、校准和采样状态
 *   - lXMilliGauss/lYMilliGauss/lZMilliGauss: 最近三轴磁场，单位 mG
 *   - usHeadingDeg10: 传感器 X/Y 平面的磁航向，单位 0.1 度
 *   - ucCalibrationPercent: 水平面校准进度，范围 0~100
 *   - ulLastUpdateMs: 最近有效采样的单调时间戳
 *   - ulReadErrorCount: I2C 事务失败累计次数
 */
typedef struct _BIKE_COMPASS_SNAPSHOT
{
    BIKE_COMPASS_STATUS eStatus;
    int32_t lXMilliGauss;
    int32_t lYMilliGauss;
    int32_t lZMilliGauss;
    uint16_t usHeadingDeg10;
    uint8_t ucCalibrationPercent;
    uint32_t ulLastUpdateMs;
    uint32_t ulReadErrorCount;
} BIKE_COMPASS_SNAPSHOT;

void BIKE_COMPASS_ResetCalibration(BIKE_COMPASS_CALIBRATION *pCalibration);
bool BIKE_COMPASS_UpdateCalibration(BIKE_COMPASS_CALIBRATION *pCalibration,
                                    int32_t lRawX, int32_t lRawY,
                                    uint32_t ulMinimumSpan,
                                    uint32_t ulMinimumSamples,
                                    int32_t *pCorrectedX,
                                    int32_t *pCorrectedY);
uint8_t BIKE_COMPASS_GetCalibrationPercent(
    const BIKE_COMPASS_CALIBRATION *pCalibration, uint32_t ulMinimumSpan);
bool BIKE_COMPASS_CalculateHeadingDeg10(int32_t lX, int32_t lY,
                                        uint16_t *pHeadingDeg10);

#ifndef BIKE_COMPASS_HOST_BUILD
bool BIKE_COMPASS_Init(void);
bool BIKE_COMPASS_GetSnapshot(BIKE_COMPASS_SNAPSHOT *pSnapshot);
#endif

#ifdef __cplusplus
}
#endif

#endif /* BIKE_COMPASS_H */
