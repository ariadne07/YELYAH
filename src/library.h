#ifndef LIBRARY_H
#define LIBRARY_H

/*
 * Music library, built by scanning the SD card.
 *
 * Expected layout:
 *
 *   /sdcard/Music/<Artist>/<Album>/<NN Title>.wav
 *
 * Files placed directly in an artist folder go in
 * "Unknown Album"; files directly in /Music go under
 * "Unknown Artist" / "Unknown Album". A leading track
 * number in the file name sets the order and is
 * dropped from the displayed title.
 */

#define LIBRARY_MUSIC_DIR   "/sdcard/Music"

#define LIBRARY_MAX_TRACKS  100
#define LIBRARY_MAX_ARTISTS 32
#define LIBRARY_MAX_ALBUMS  48

#define LIBRARY_NAME_LEN    32
#define LIBRARY_PATH_LEN    128

typedef struct {
    char path[LIBRARY_PATH_LEN];
    char title[LIBRARY_NAME_LEN];
    int artist;
    int album;
} library_track_t;

/*
 * Scans the SD card. Safe to call again to rescan.
 */
void library_scan(void);

int library_track_count(void);
const library_track_t *library_track(int index);

int library_artist_count(void);
const char *library_artist_name(int index);

int library_album_count(void);
const char *library_album_name(int index);
int library_album_artist(int index);

/*
 * These fill `out` with indexes and return how many
 * were written (at most `max`).
 */
int library_albums_for_artist(int artist, int *out, int max);
int library_tracks_for_album(int album, int *out, int max);

#endif
