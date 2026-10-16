#ifndef PLAYER_H
#define PLAYER_H

#include <stdbool.h>
#include <stdint.h>

#define PLAYER_MAX_VOLUME 20

typedef enum {
    REPEAT_OFF,
    REPEAT_ALL,
    REPEAT_ONE
} player_repeat_t;

typedef struct {
    int track;              // library track index, -1 if none
    bool playing;

    int queue_position;     // 0-based position in the play order
    int queue_length;

    uint32_t position_ms;
    uint32_t duration_ms;

    int volume;             // 0 .. PLAYER_MAX_VOLUME
    bool shuffle;
    player_repeat_t repeat;

    /*
     * Increments whenever anything other than
     * position_ms changes, so the UI knows when a
     * full redraw is needed.
     */
    uint32_t version;
} player_status_t;

/*
 * Starts the playback task. Call after library_scan().
 */
void player_init(void);

/*
 * Replaces the queue with `count` library track indexes
 * and starts playing tracks[start].
 */
void player_play_queue(const int *tracks, int count, int start);

void player_toggle_pause(void);
void player_next(void);
void player_previous(void);

/*
 * Jumps forward (positive) or back (negative).
 */
void player_seek(int seconds);

void player_change_volume(int delta);
void player_toggle_shuffle(void);
void player_cycle_repeat(void);

void player_get_status(player_status_t *status);

#endif
