#ifndef BIKE_SERVICE_H
#define BIKE_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#include "bike_nmea.h"
#include "bike_recorder.h"
#include "bike_ride_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/* BIKE_GNSS_PORT_STATUS: GNSS 串口端口状态。 */
typedef enum _BIKE_GNSS_PORT_STATUS
{
    BIKE_GNSS_PORT_DISABLED = 0,
    BIKE_GNSS_PORT_SEARCHING,
    BIKE_GNSS_PORT_READY,
    BIKE_GNSS_PORT_ERROR
} BIKE_GNSS_PORT_STATUS;

/* BIKE_SERVICE_SNAPSHOT: GNSS 服务提供给 UI 的线程安全快照。
 * 成员说明：
 *   - ePortStatus: UART2 端口状态
 *   - ulLastUpdateMs: 最近一次通过校验的 GGA/RMC 时间戳
 *   - ulAcceptedCount: 已接受的 GGA/RMC 语句数
 *   - ulChecksumErrorCount: 校验错误语句数
 *   - ulOverflowCount: 超长语句数
 *   - tGnss: 最近定位数据
 *   - tRide: 本次骑行统计
 *   - tRecorder: GPX 轨迹记录状态
 *   - bRtcSynchronized: RTC 是否已被有效 GNSS 时间校准
 *   - bAutoPaused: 当前是否由低速自动暂停
 */
typedef struct _BIKE_SERVICE_SNAPSHOT
{
    BIKE_GNSS_PORT_STATUS ePortStatus;
    uint32_t ulLastUpdateMs;
    uint32_t ulAcceptedCount;
    uint32_t ulChecksumErrorCount;
    uint32_t ulOverflowCount;
    BIKE_GNSS_DATA tGnss;
    BIKE_RIDE_STATE tRide;
    BIKE_RECORDER_SNAPSHOT tRecorder;
    bool bRtcSynchronized;
    bool bAutoPaused;
} BIKE_SERVICE_SNAPSHOT;

bool BIKE_SERVICE_GetSnapshot(BIKE_SERVICE_SNAPSHOT *pSnapshot);
bool BIKE_SERVICE_StartRide(void);
bool BIKE_SERVICE_PauseRide(void);
bool BIKE_SERVICE_StopRide(void);

#ifdef __cplusplus
}
#endif

#endif /* BIKE_SERVICE_H */
