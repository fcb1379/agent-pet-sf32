#include "bike_speed_source.h"

#include <stddef.h>
#include <string.h>

#define BIKE_SPEED_GNSS_MAX_CM_PER_SEC (6000U)
#define BIKE_SPEED_CSC_MAX_CENTI_KPH (20000U)

/* BIKE_SPEED_Select: CSC 有效时优先采用轮速，否则回退到 GNSS 地速。
 * 参数：
 *   - pInput: GNSS 和 CSC 候选速度
 *   - pSelection: 输出的统一速度样本
 * 返回值：无
 */
void BIKE_SPEED_Select(const BIKE_SPEED_INPUT *pInput,
                       BIKE_SPEED_SELECTION *pSelection)
{
    uint32_t ulSpeedCentiKph;

    if (NULL == pSelection)
    {
        return;
    }
    (void)memset(pSelection, 0, sizeof(*pSelection));
    if (NULL == pInput)
    {
        return;
    }

    if (pInput->bCscValid &&
        (BIKE_SPEED_CSC_MAX_CENTI_KPH >= pInput->usCscSpeedCentiKph))
    {
        pSelection->eSource = BIKE_SPEED_SOURCE_CSC;
        pSelection->bValid = true;
        pSelection->usSpeedCentiKph = pInput->usCscSpeedCentiKph;
        return;
    }

    if (pInput->bGnssValid &&
        (BIKE_SPEED_GNSS_MAX_CM_PER_SEC >= pInput->ulGnssSpeedCmPerSec))
    {
        ulSpeedCentiKph = (pInput->ulGnssSpeedCmPerSec * 36U + 5U) / 10U;
        pSelection->eSource = BIKE_SPEED_SOURCE_GNSS;
        pSelection->bValid = true;
        pSelection->usSpeedCentiKph = (uint16_t)ulSpeedCentiKph;
    }

    return;
}
