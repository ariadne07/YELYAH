#include "library.h"

#include "esp_log.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "LIBRARY";

typedef struct {
    char name[LIBRARY_NAME_LEN];
    int artist;
} album_t;

static library_track_t tracks[LIBRARY_MAX_TRACKS];
static int track_count = 0;

static char artists[LIBRARY_MAX_ARTISTS][LIBRARY_NAME_LEN];
static int artist_count = 0;

static album_t albums[LIBRARY_MAX_ALBUMS];
static int album_count = 0;


/*
 * --------------------------------------------------
 * NAME TABLES
 * --------------------------------------------------
 */

static int find_or_add_artist(const char *name)
{
    for (int i = 0; i < artist_count; i++) {
        if (strcmp(artists[i], name) == 0) {
            return i;
        }
    }

    if (artist_count >= LIBRARY_MAX_ARTISTS) {
        return -1;
    }

    snprintf(artists[artist_count], LIBRARY_NAME_LEN, "%s", name);

    return artist_count++;
}


static int find_or_add_album(const char *name, int artist)
{
    for (int i = 0; i < album_count; i++) {
        if (albums[i].artist == artist &&
            strcmp(albums[i].name, name) == 0) {
            return i;
        }
    }

    if (album_count >= LIBRARY_MAX_ALBUMS) {
        return -1;
    }

    snprintf(albums[album_count].name, LIBRARY_NAME_LEN, "%s", name);
    albums[album_count].artist = artist;

    return album_count++;
}


/*
 * --------------------------------------------------
 * FILE HELPERS
 * --------------------------------------------------
 */

static bool is_wav_file(const char *file_name)
{
    size_t len = strlen(file_name);

    return len > 4 &&
           strcasecmp(file_name + len - 4, ".wav") == 0;
}


/*
 * "03 - Night Drive.wav" -> "Night Drive"
 */
static void title_from_file_name(const char *file_name, char *title)
{
    const char *start = file_name;

    while (isdigit((unsigned char)*start)) {
        start++;
    }

    /*
     * Only strip separators if there was a track number.
     */
    if (start != file_name) {
        while (*start == ' ' || *start == '-' ||
               *start == '_' || *start == '.') {
            start++;
        }
    }

    if (*start == '\0') {
        start = file_name;
    }

    snprintf(title, LIBRARY_NAME_LEN, "%s", start);

    /*
     * Drop the extension.
     */
    char *dot = strrchr(title, '.');

    if (dot != NULL && strcasecmp(dot, ".wav") == 0) {
        *dot = '\0';
    }
}


static void add_track(
    const char *path,
    const char *file_name,
    const char *artist_name,
    const char *album_name
)
{
    if (track_count >= LIBRARY_MAX_TRACKS) {
        ESP_LOGW(TAG, "Track limit reached, skipping %s", path);
        return;
    }

    if (strlen(path) >= LIBRARY_PATH_LEN) {
        ESP_LOGW(TAG, "Path too long, skipping %s", path);
        return;
    }

    int artist = find_or_add_artist(artist_name);
    int album = (artist >= 0) ? find_or_add_album(album_name, artist) : -1;

    if (artist < 0 || album < 0) {
        ESP_LOGW(TAG, "Artist/album limit reached, skipping %s", path);
        return;
    }

    library_track_t *track = &tracks[track_count++];

    snprintf(track->path, LIBRARY_PATH_LEN, "%s", path);
    title_from_file_name(file_name, track->title);
    track->artist = artist;
    track->album = album;
}


/*
 * Scans one directory level.
 *
 * depth 0 = /Music          (subfolders are artists)
 * depth 1 = /Music/Artist   (subfolders are albums)
 * depth 2 = /Music/Artist/Album
 */
static void scan_dir(
    const char *dir_path,
    int depth,
    const char *artist_name,
    const char *album_name
)
{
    DIR *dir = opendir(dir_path);

    if (dir == NULL) {
        if (depth == 0) {
            ESP_LOGW(TAG, "No %s folder on the SD card", dir_path);
        }
        return;
    }

    struct dirent *entry;
    char path[LIBRARY_PATH_LEN];

    while ((entry = readdir(dir)) != NULL) {

        if (entry->d_name[0] == '.') {
            continue;
        }

        int written = snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);

        if (written < 0 || written >= (int)sizeof(path)) {
            ESP_LOGW(TAG, "Path too long, skipping %s", entry->d_name);
            continue;
        }

        if (entry->d_type == DT_DIR) {

            if (depth == 0) {
                scan_dir(path, 1, entry->d_name, "Unknown Album");
            } else if (depth == 1) {
                scan_dir(path, 2, artist_name, entry->d_name);
            }

        } else if (is_wav_file(entry->d_name)) {

            add_track(path, entry->d_name, artist_name, album_name);
        }
    }

    closedir(dir);
}


/*
 * --------------------------------------------------
 * SORTING
 * --------------------------------------------------
 */

static int compare_tracks(const void *a, const void *b)
{
    const library_track_t *ta = a;
    const library_track_t *tb = b;

    int result = strcasecmp(artists[ta->artist], artists[tb->artist]);

    if (result == 0) {
        result = strcasecmp(albums[ta->album].name, albums[tb->album].name);
    }

    if (result == 0) {
        result = strcasecmp(ta->path, tb->path);
    }

    return result;
}


/*
 * Sorts tracks by artist, album, then file name, and
 * rebuilds the artist/album tables in that same order
 * so every list in the UI comes out alphabetical.
 */
static void sort_library(void)
{
    qsort(tracks, track_count, sizeof(library_track_t), compare_tracks);

    static char old_artists[LIBRARY_MAX_ARTISTS][LIBRARY_NAME_LEN];
    static album_t old_albums[LIBRARY_MAX_ALBUMS];

    memcpy(old_artists, artists, sizeof(artists));
    memcpy(old_albums, albums, sizeof(albums));

    artist_count = 0;
    album_count = 0;

    for (int i = 0; i < track_count; i++) {

        library_track_t *track = &tracks[i];

        const char *album_name = old_albums[track->album].name;

        track->artist = find_or_add_artist(old_artists[track->artist]);
        track->album = find_or_add_album(album_name, track->artist);
    }
}


/*
 * --------------------------------------------------
 * PUBLIC FUNCTIONS
 * --------------------------------------------------
 */

void library_scan(void)
{
    track_count = 0;
    artist_count = 0;
    album_count = 0;

    scan_dir(LIBRARY_MUSIC_DIR, 0, "Unknown Artist", "Unknown Album");

    sort_library();

    ESP_LOGI(
        TAG,
        "Found %d tracks, %d artists, %d albums",
        track_count,
        artist_count,
        album_count
    );
}


int library_track_count(void)
{
    return track_count;
}


const library_track_t *library_track(int index)
{
    if (index < 0 || index >= track_count) {
        return NULL;
    }

    return &tracks[index];
}


int library_artist_count(void)
{
    return artist_count;
}


const char *library_artist_name(int index)
{
    if (index < 0 || index >= artist_count) {
        return "";
    }

    return artists[index];
}


int library_album_count(void)
{
    return album_count;
}


const char *library_album_name(int index)
{
    if (index < 0 || index >= album_count) {
        return "";
    }

    return albums[index].name;
}


int library_album_artist(int index)
{
    if (index < 0 || index >= album_count) {
        return -1;
    }

    return albums[index].artist;
}


int library_albums_for_artist(int artist, int *out, int max)
{
    int count = 0;

    for (int i = 0; i < album_count && count < max; i++) {
        if (albums[i].artist == artist) {
            out[count++] = i;
        }
    }

    return count;
}


int library_tracks_for_album(int album, int *out, int max)
{
    int count = 0;

    for (int i = 0; i < track_count && count < max; i++) {
        if (tracks[i].album == album) {
            out[count++] = i;
        }
    }

    return count;
}
