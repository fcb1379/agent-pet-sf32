#ifndef RTTHREAD_H
#define RTTHREAD_H

#include <stdio.h>
#include <string.h>

#include "rtdef.h"

#define rt_snprintf snprintf
#define rt_strlen strlen
#define rt_strncmp strncmp
#define rt_strncpy strncpy
#define RT_TICK_PER_SECOND (1000U)

typedef uint32_t rt_tick_t;

rt_tick_t rt_tick_get(void);
static inline rt_tick_t rt_tick_from_millisecond(uint32_t milliseconds)
{
    return milliseconds;
}

#endif
