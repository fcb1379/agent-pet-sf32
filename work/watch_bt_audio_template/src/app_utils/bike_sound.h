#ifndef BIKE_SOUND_H
#define BIKE_SOUND_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* BIKE_SOUND_EVENT: X-TRACK 提示音事件。 */
typedef enum _BIKE_SOUND_EVENT
{
    BIKE_SOUND_EVENT_STARTUP = 0,
    BIKE_SOUND_EVENT_ERROR,
    BIKE_SOUND_EVENT_CONNECT,
    BIKE_SOUND_EVENT_DISCONNECT,
    BIKE_SOUND_EVENT_UNSTABLE,
    BIKE_SOUND_EVENT_CHARGE_START,
    BIKE_SOUND_EVENT_CHARGE_END,
    BIKE_SOUND_EVENT_NO_OPERATION,
    BIKE_SOUND_EVENT_COUNT
} BIKE_SOUND_EVENT;

/* BIKE_SOUND_NODE: 一段方波或静音提示音。
 * 成员说明：
 *   - usFrequencyHz: 方波频率，0 表示静音
 *   - usDurationMs: 持续时间，单位毫秒
 */
typedef struct _BIKE_SOUND_NODE
{
    uint16_t usFrequencyHz;
    uint16_t usDurationMs;
} BIKE_SOUND_NODE;

const BIKE_SOUND_NODE *BIKE_SOUND_GetPattern(BIKE_SOUND_EVENT eEvent,
                                              uint8_t *pNodeCount);
const char *BIKE_SOUND_GetEventName(BIKE_SOUND_EVENT eEvent);
bool BIKE_SOUND_Request(BIKE_SOUND_EVENT eEvent);

#ifdef __cplusplus
}
#endif

#endif /* BIKE_SOUND_H */
