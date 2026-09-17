#ifndef BIKE_GPX_H
#define BIKE_GPX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "bike_nmea.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BIKE_GPX_PATH_MAX (128U)

/* BIKE_GPX_STATE: GPX 文件生命周期状态。 */
typedef enum _BIKE_GPX_STATE
{
    BIKE_GPX_STATE_IDLE = 0,
    BIKE_GPX_STATE_ACTIVE,
    BIKE_GPX_STATE_PAUSED,
    BIKE_GPX_STATE_COMPLETE,
    BIKE_GPX_STATE_ERROR
} BIKE_GPX_STATE;

/* BIKE_GPX_ERROR: GPX 记录器最后一次错误。 */
typedef enum _BIKE_GPX_ERROR
{
    BIKE_GPX_ERROR_NONE = 0,
    BIKE_GPX_ERROR_ARGUMENT,
    BIKE_GPX_ERROR_TIME,
    BIKE_GPX_ERROR_NO_SPACE,
    BIKE_GPX_ERROR_PATH,
    BIKE_GPX_ERROR_OPEN,
    BIKE_GPX_ERROR_WRITE,
    BIKE_GPX_ERROR_SYNC,
    BIKE_GPX_ERROR_RENAME,
    BIKE_GPX_ERROR_RECOVERY
} BIKE_GPX_ERROR;

/* BIKE_GPX_RECOVERY_RESULT: 启动时未关闭轨迹的恢复结果。 */
typedef enum _BIKE_GPX_RECOVERY_RESULT
{
    BIKE_GPX_RECOVERY_NONE = 0,
    BIKE_GPX_RECOVERY_DONE,
    BIKE_GPX_RECOVERY_ERROR
} BIKE_GPX_RECOVERY_RESULT;

/* BIKE_GPX_WRITER: 固定容量 GPX 流式写入状态。
 * 成员说明：
 *   - eState/eError: 当前状态和最后错误
 *   - lFileDescriptor: 当前临时文件描述符，未打开时为 -1
 *   - ulPointCount: 已持久化的轨迹点数量
 *   - udLastUtcKey: 上一轨迹点 UTC 排序键
 *   - lPreviousLatitudeE7/lPreviousLongitudeE7: 上一轨迹点坐标
 *   - bHasPreviousPoint: 是否已有轨迹点
 *   - lCandidateLatitudeE7/lCandidateLongitudeE7: 跳点后的重定位候选坐标
 *   - bHasRecoveryCandidate: 是否等待第二点确认新轨迹段
 *   - aPartPath/aFinalPath: 临时文件和最终文件路径
 */
typedef struct _BIKE_GPX_WRITER
{
    BIKE_GPX_STATE eState;
    BIKE_GPX_ERROR eError;
    int32_t lFileDescriptor;
    uint32_t ulPointCount;
    uint64_t udLastUtcKey;
    int32_t lPreviousLatitudeE7;
    int32_t lPreviousLongitudeE7;
    bool bHasPreviousPoint;
    int32_t lCandidateLatitudeE7;
    int32_t lCandidateLongitudeE7;
    bool bHasRecoveryCandidate;
    char aPartPath[BIKE_GPX_PATH_MAX];
    char aFinalPath[BIKE_GPX_PATH_MAX];
} BIKE_GPX_WRITER;

void BIKE_GPX_Init(BIKE_GPX_WRITER *pWriter);
bool BIKE_GPX_Start(BIKE_GPX_WRITER *pWriter, const char *pDirectory,
                    const BIKE_GNSS_DATA *pGnss);
bool BIKE_GPX_Append(BIKE_GPX_WRITER *pWriter, const BIKE_GNSS_DATA *pGnss);
bool BIKE_GPX_Pause(BIKE_GPX_WRITER *pWriter);
bool BIKE_GPX_Resume(BIKE_GPX_WRITER *pWriter);
bool BIKE_GPX_Stop(BIKE_GPX_WRITER *pWriter);
BIKE_GPX_RECOVERY_RESULT BIKE_GPX_Recover(const char *pDirectory,
                                          char *pRecoveredPath,
                                          size_t ulRecoveredPathSize);

#ifdef __cplusplus
}
#endif

#endif /* BIKE_GPX_H */
