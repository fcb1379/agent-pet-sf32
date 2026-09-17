#ifndef BIKE_RECORDER_H
#define BIKE_RECORDER_H

#include <stdbool.h>
#include <stdint.h>

#include "bike_gpx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* BIKE_RECORDER_STATUS: 异步轨迹记录服务状态。 */
typedef enum _BIKE_RECORDER_STATUS
{
    BIKE_RECORDER_STATUS_IDLE = 0,
    BIKE_RECORDER_STATUS_WAITING_FIX,
    BIKE_RECORDER_STATUS_RECORDING,
    BIKE_RECORDER_STATUS_PAUSED,
    BIKE_RECORDER_STATUS_SAVED,
    BIKE_RECORDER_STATUS_ERROR
} BIKE_RECORDER_STATUS;

/* BIKE_RECORDER_SNAPSHOT: 提供给 UI 的轨迹记录快照。
 * 成员说明：
 *   - eStatus: 记录服务状态
 *   - eError: GPX 层最后错误
 *   - ulPointCount: 已落盘轨迹点数
 *   - ulDroppedMessageCount: 队列拥塞时丢弃的消息数
 *   - aFilePath: 当前或最近保存的轨迹路径
 */
typedef struct _BIKE_RECORDER_SNAPSHOT
{
    BIKE_RECORDER_STATUS eStatus;
    BIKE_GPX_ERROR eError;
    uint32_t ulPointCount;
    uint32_t ulDroppedMessageCount;
    char aFilePath[BIKE_GPX_PATH_MAX];
} BIKE_RECORDER_SNAPSHOT;

bool BIKE_RECORDER_Init(void);
bool BIKE_RECORDER_Start(void);
bool BIKE_RECORDER_Pause(void);
bool BIKE_RECORDER_Resume(void);
bool BIKE_RECORDER_Stop(void);
bool BIKE_RECORDER_SubmitPoint(const BIKE_GNSS_DATA *pGnss);
bool BIKE_RECORDER_GetSnapshot(BIKE_RECORDER_SNAPSHOT *pSnapshot);

#ifdef __cplusplus
}
#endif

#endif /* BIKE_RECORDER_H */
