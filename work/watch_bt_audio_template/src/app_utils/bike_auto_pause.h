#ifndef BIKE_AUTO_PAUSE_H
#define BIKE_AUTO_PAUSE_H

#include <stdbool.h>
#include <stdint.h>

#include "bike_ride_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BIKE_AUTO_PAUSE_DELAY_MS (5000U)
#define BIKE_AUTO_RESUME_DELAY_MS (2000U)
#define BIKE_AUTO_RESUME_HYSTERESIS_CENTI_KPH (100U)

/* BIKE_AUTO_PAUSE_ACTION: 自动暂停状态机请求服务层执行的动作。 */
typedef enum _BIKE_AUTO_PAUSE_ACTION
{
    BIKE_AUTO_PAUSE_ACTION_NONE = 0,
    BIKE_AUTO_PAUSE_ACTION_PAUSE,
    BIKE_AUTO_PAUSE_ACTION_RESUME
} BIKE_AUTO_PAUSE_ACTION;

/* BIKE_AUTO_PAUSE_CONTROLLER: 自动暂停防抖状态。
 * 成员说明：
 *   - bLowSpeedTiming: 正在计算低速持续时间
 *   - bResumeSpeedTiming: 正在计算恢复速度持续时间
 *   - ulLowSpeedStartMs: 低速计时起点
 *   - ulResumeSpeedStartMs: 恢复速度计时起点
 */
typedef struct _BIKE_AUTO_PAUSE_CONTROLLER
{
    bool bLowSpeedTiming;
    bool bResumeSpeedTiming;
    uint32_t ulLowSpeedStartMs;
    uint32_t ulResumeSpeedStartMs;
} BIKE_AUTO_PAUSE_CONTROLLER;

void BIKE_AUTO_PAUSE_Init(BIKE_AUTO_PAUSE_CONTROLLER *pController);
BIKE_AUTO_PAUSE_ACTION BIKE_AUTO_PAUSE_Update(
    BIKE_AUTO_PAUSE_CONTROLLER *pController,
    bool bEnabled,
    bool bAutoPaused,
    BIKE_RIDE_MODE eRideMode,
    bool bFixValid,
    uint16_t usSpeedCentiKph,
    uint16_t usPauseThresholdCentiKph,
    uint32_t ulNowMs);

#ifdef __cplusplus
}
#endif

#endif /* BIKE_AUTO_PAUSE_H */
