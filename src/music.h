#ifndef MUSIC_H
#define MUSIC_H

#include <stdbool.h>
#include <stddef.h>

#define MUSIC_MAX_TRACKS 128
#define MUSIC_MAX_PATH    256

typedef struct {
char path[MUSIC_MAX_PATH];
char filename[MUSIC_MAX_PATH];
} music_track_t;

bool music_scan(void);

size_t music_get_track_count(void);

const music_track_t *music_get_track(size_t index);

#endif
