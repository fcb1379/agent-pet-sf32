#ifndef BIKE_TIME_H
#define BIKE_TIME_H

#include <stdbool.h>
#include <stdint.h>

#include "bike_nmea.h"

#ifdef __cplusplus
extern "C" {
#endif

/* BIKE_LOCAL_TIME: 应用时区后的本地日历时间。 */
typedef struct _BIKE_LOCAL_TIME
{
    uint16_t usYear;
    uint8_t ucMonth;
    uint8_t ucDay;
    uint8_t ucHour;
    uint8_t ucMinute;
    uint8_t ucSecond;
} BIKE_LOCAL_TIME;

bool BIKE_TIME_ConvertUtc(const BIKE_GNSS_DATA *pGnss, int16_t sTimezoneMinutes,
                          BIKE_LOCAL_TIME *pLocalTime);

#ifdef __cplusplus
}
#endif

#endif /* BIKE_TIME_H */
