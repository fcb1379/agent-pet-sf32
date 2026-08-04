#ifndef LOCAL_MUSIC_PLAYER_H
#define LOCAL_MUSIC_PLAYER_H

#include <stdint.h>

#define LOCAL_MUSIC_PATH_LENGTH (96U)

typedef enum _LOCAL_MUSIC_STATE
{
    LOCAL_MUSIC_STATE_IDLE = 0,
    LOCAL_MUSIC_STATE_PLAYING,
    LOCAL_MUSIC_STATE_PAUSED,
    LOCAL_MUSIC_STATE_SUSPENDED,
    LOCAL_MUSIC_STATE_ENDED,
    LOCAL_MUSIC_STATE_ERROR,
} LOCAL_MUSIC_STATE;

typedef struct _LOCAL_MUSIC_SNAPSHOT
{
    LOCAL_MUSIC_STATE eState;
    char aPath[LOCAL_MUSIC_PATH_LENGTH];
    uint32_t ulLastCallback;
} LOCAL_MUSIC_SNAPSHOT;

int local_music_get_snapshot(LOCAL_MUSIC_SNAPSHOT *pSnapshot);
int local_music_play_file(const char *path, uint32_t loop);
int local_music_stop(void);
int local_music_pause(void);
int local_music_resume(void);

#endif /* LOCAL_MUSIC_PLAYER_H */
