#ifndef LOCAL_MUSIC_PLAYER_H
#define LOCAL_MUSIC_PLAYER_H

#include <stdint.h>

int local_music_play_file(const char *path, uint32_t loop);
int local_music_stop(void);
int local_music_pause(void);
int local_music_resume(void);

#endif /* LOCAL_MUSIC_PLAYER_H */
