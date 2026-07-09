#ifndef BT_AUDIO_SINK_H
#define BT_AUDIO_SINK_H

#include <rtdef.h>

typedef enum
{
    BT_AUDIO_SINK_STATE_STARTING = 0,
    BT_AUDIO_SINK_STATE_READY,
    BT_AUDIO_SINK_STATE_CONNECTED,
    BT_AUDIO_SINK_STATE_STREAMING,
    BT_AUDIO_SINK_STATE_ERROR,
} bt_audio_sink_state_t;

typedef struct
{
    bt_audio_sink_state_t state;
    int16_t last_error;
    uint32_t disconnect_count;
    uint32_t recovery_count;
    uint32_t last_event_tick;
} bt_audio_sink_health_t;

rt_bool_t bt_audio_sink_is_connected(void);
rt_bool_t bt_audio_sink_is_streaming(void);
void bt_audio_sink_get_health(bt_audio_sink_health_t *health);
int bt_audio_sink_request_recovery(void);

#endif /* BT_AUDIO_SINK_H */
