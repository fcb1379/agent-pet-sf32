#include "bike_auto_pause.h"

#include <stddef.h>
#include <string.h>

/* BIKE_AUTO_PAUSE_Init: 清除自动暂停防抖状态。
 * 参数：
 *   - pController: 状态机对象
 * 返回值：无
 */
void BIKE_AUTO_PAUSE_Init(BIKE_AUTO_PAUSE_CONTROLLER *pController)
{
    if (NULL != pController)
    {
        (void)memset(pController, 0, sizeof(*pController));
    }

    return;
}

/* BikeAutoPause_GetResumeThreshold: 计算带回差的自动恢复阈值。
 * 参数：
 *   - usPauseThresholdCentiKph: 自动暂停阈值
 * 返回值：自动恢复阈值，饱和到 UINT16_MAX
 */
static uint16_t BikeAutoPause_GetResumeThreshold(uint16_t usPauseThresholdCentiKph)
{
    uint32_t ulResumeThreshold;

    ulResumeThreshold = (uint32_t)usPauseThresholdCentiKph +
                        BIKE_AUTO_RESUME_HYSTERESIS_CENTI_KPH;
    if (UINT16_MAX < ulResumeThreshold)
    {
        ulResumeThreshold = UINT16_MAX;
    }

    return (uint16_t)ulResumeThreshold;
}

/* BIKE_AUTO_PAUSE_Update: 根据定位速度和骑行状态执行带回差的防抖判定。
 * 参数：
 *   - pController: 状态机对象
 *   - bEnabled: 自动暂停功能是否启用
 *   - bAutoPaused: 当前暂停是否由自动状态机触发
 *   - eRideMode: 当前骑行状态
 *   - bFixValid: 当前定位是否有效
 *   - usSpeedCentiKph: 当前速度，单位 0.01 km/h
 *   - usPauseThresholdCentiKph: 自动暂停阈值，单位 0.01 km/h
 *   - ulNowMs: 当前单调时钟毫秒数
 * 返回值：服务层应执行的暂停、恢复或无动作
 */
BIKE_AUTO_PAUSE_ACTION BIKE_AUTO_PAUSE_Update(
    BIKE_AUTO_PAUSE_CONTROLLER *pController,
    bool bEnabled,
    bool bAutoPaused,
    BIKE_RIDE_MODE eRideMode,
    bool bFixValid,
    uint16_t usSpeedCentiKph,
    uint16_t usPauseThresholdCentiKph,
    uint32_t ulNowMs)
{
    uint16_t usResumeThreshold;

    if (NULL == pController)
    {
        return BIKE_AUTO_PAUSE_ACTION_NONE;
    }

    if (!bEnabled)
    {
        BIKE_AUTO_PAUSE_Init(pController);
        return bAutoPaused ? BIKE_AUTO_PAUSE_ACTION_RESUME :
               BIKE_AUTO_PAUSE_ACTION_NONE;
    }
    if (BIKE_RIDE_MODE_STOPPED == eRideMode)
    {
        BIKE_AUTO_PAUSE_Init(pController);
        return BIKE_AUTO_PAUSE_ACTION_NONE;
    }
    if (!bFixValid)
    {
        BIKE_AUTO_PAUSE_Init(pController);
        return BIKE_AUTO_PAUSE_ACTION_NONE;
    }

    if ((BIKE_RIDE_MODE_RUNNING == eRideMode) && (!bAutoPaused))
    {
        pController->bResumeSpeedTiming = false;
        if (usPauseThresholdCentiKph >= usSpeedCentiKph)
        {
            if (!pController->bLowSpeedTiming)
            {
                pController->bLowSpeedTiming = true;
                pController->ulLowSpeedStartMs = ulNowMs;
            }
            else if (BIKE_AUTO_PAUSE_DELAY_MS <=
                     (ulNowMs - pController->ulLowSpeedStartMs))
            {
                BIKE_AUTO_PAUSE_Init(pController);
                return BIKE_AUTO_PAUSE_ACTION_PAUSE;
            }
        }
        else
        {
            pController->bLowSpeedTiming = false;
        }
        return BIKE_AUTO_PAUSE_ACTION_NONE;
    }

    pController->bLowSpeedTiming = false;
    if ((BIKE_RIDE_MODE_PAUSED != eRideMode) || (!bAutoPaused))
    {
        pController->bResumeSpeedTiming = false;
        return BIKE_AUTO_PAUSE_ACTION_NONE;
    }

    usResumeThreshold = BikeAutoPause_GetResumeThreshold(usPauseThresholdCentiKph);
    if (usResumeThreshold <= usSpeedCentiKph)
    {
        if (!pController->bResumeSpeedTiming)
        {
            pController->bResumeSpeedTiming = true;
            pController->ulResumeSpeedStartMs = ulNowMs;
        }
        else if (BIKE_AUTO_RESUME_DELAY_MS <=
                 (ulNowMs - pController->ulResumeSpeedStartMs))
        {
            BIKE_AUTO_PAUSE_Init(pController);
            return BIKE_AUTO_PAUSE_ACTION_RESUME;
        }
    }
    else
    {
        pController->bResumeSpeedTiming = false;
    }

    return BIKE_AUTO_PAUSE_ACTION_NONE;
}
