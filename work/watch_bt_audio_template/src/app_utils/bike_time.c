#include "bike_time.h"

#include <stddef.h>

#define BIKE_TIME_TIMEZONE_MIN (-720)
#define BIKE_TIME_TIMEZONE_MAX (840)

/* BikeTime_IsLeapYear: 判断公历闰年。
 * 参数：
 *   - usYear: 完整年份
 * 返回值：闰年返回 true
 */
static bool BikeTime_IsLeapYear(uint16_t usYear)
{
    return ((0U == (usYear % 4U)) && (0U != (usYear % 100U))) ||
           (0U == (usYear % 400U));
}

/* BikeTime_GetMonthDays: 获取指定年月的天数。
 * 参数：
 *   - usYear: 完整年份
 *   - ucMonth: 月份 1~12
 * 返回值：月份有效时返回天数，否则返回 0
 */
static uint8_t BikeTime_GetMonthDays(uint16_t usYear, uint8_t ucMonth)
{
    static const uint8_t l_aMonthDays[12] =
    {
        31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U
    };
    uint8_t ucDays;

    if ((1U > ucMonth) || (12U < ucMonth))
    {
        return 0U;
    }

    ucDays = l_aMonthDays[ucMonth - 1U];
    if ((2U == ucMonth) && BikeTime_IsLeapYear(usYear))
    {
        ucDays = 29U;
    }

    return ucDays;
}

/* BikeTime_ShiftDay: 将日历日期前移或后移一天。
 * 参数：
 *   - pTime: 待修改时间
 *   - lDirection: -1 前移，+1 后移
 * 返回值：成功返回 true，否则返回 false
 */
static bool BikeTime_ShiftDay(BIKE_LOCAL_TIME *pTime, int32_t lDirection)
{
    uint8_t ucMonthDays;

    if ((NULL == pTime) || ((-1 != lDirection) && (1 != lDirection)))
    {
        return false;
    }

    if (0 < lDirection)
    {
        ucMonthDays = BikeTime_GetMonthDays(pTime->usYear, pTime->ucMonth);
        if (pTime->ucDay < ucMonthDays)
        {
            pTime->ucDay++;
        }
        else
        {
            pTime->ucDay = 1U;
            if (12U > pTime->ucMonth)
            {
                pTime->ucMonth++;
            }
            else
            {
                pTime->ucMonth = 1U;
                pTime->usYear++;
            }
        }
    }
    else if (1U < pTime->ucDay)
    {
        pTime->ucDay--;
    }
    else
    {
        if (1U < pTime->ucMonth)
        {
            pTime->ucMonth--;
        }
        else
        {
            if (2000U >= pTime->usYear)
            {
                return false;
            }
            pTime->ucMonth = 12U;
            pTime->usYear--;
        }
        pTime->ucDay = BikeTime_GetMonthDays(pTime->usYear, pTime->ucMonth);
    }

    return true;
}

/* BIKE_TIME_ConvertUtc: 将 GNSS UTC 转换为配置时区的本地日历时间。
 * 参数：
 *   - pGnss: 带有效日期时间的 GNSS 数据
 *   - sTimezoneMinutes: UTC 偏移分钟，范围 -720~840
 *   - pLocalTime: 输出本地时间
 * 返回值：成功返回 true，否则返回 false
 */
bool BIKE_TIME_ConvertUtc(const BIKE_GNSS_DATA *pGnss, int16_t sTimezoneMinutes,
                          BIKE_LOCAL_TIME *pLocalTime)
{
    int32_t lMinuteOfDay;
    uint8_t ucMonthDays;

    if ((NULL == pGnss) || (NULL == pLocalTime) ||
            (BIKE_TIME_TIMEZONE_MIN > sTimezoneMinutes) ||
            (BIKE_TIME_TIMEZONE_MAX < sTimezoneMinutes) ||
            (2000U > pGnss->usYear) || (2099U < pGnss->usYear) ||
            (24U <= pGnss->ucHour) || (60U <= pGnss->ucMinute) ||
            (60U <= pGnss->ucSecond))
    {
        return false;
    }

    ucMonthDays = BikeTime_GetMonthDays(pGnss->usYear, pGnss->ucMonth);
    if ((0U == ucMonthDays) || (1U > pGnss->ucDay) || (ucMonthDays < pGnss->ucDay))
    {
        return false;
    }

    pLocalTime->usYear = pGnss->usYear;
    pLocalTime->ucMonth = pGnss->ucMonth;
    pLocalTime->ucDay = pGnss->ucDay;
    pLocalTime->ucSecond = pGnss->ucSecond;
    lMinuteOfDay = ((int32_t)pGnss->ucHour * 60) + pGnss->ucMinute + sTimezoneMinutes;
    if (0 > lMinuteOfDay)
    {
        lMinuteOfDay += 24 * 60;
        if (!BikeTime_ShiftDay(pLocalTime, -1))
        {
            return false;
        }
    }
    else if ((24 * 60) <= lMinuteOfDay)
    {
        lMinuteOfDay -= 24 * 60;
        if (!BikeTime_ShiftDay(pLocalTime, 1))
        {
            return false;
        }
    }

    pLocalTime->ucHour = (uint8_t)(lMinuteOfDay / 60);
    pLocalTime->ucMinute = (uint8_t)(lMinuteOfDay % 60);

    return true;
}
