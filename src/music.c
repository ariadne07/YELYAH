#include "music.h"
#include "sdcard.h"

#include "esp_log.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static const char *TAG = "MUSIC";

static music_track_t tracks[MUSIC_MAX_TRACKS];
static size_t track_count = 0;

/* -------------------------------------------------------------------------- */
/* Helpers                                                                    */
/* -------------------------------------------------------------------------- */

static bool has_mp3_extension(const char *filename)
{
if (filename == NULL) {
return false;
}

  
const char *extension =
    strrchr(filename, '.');

if (extension == NULL) {
    return false;
}

return strcasecmp(extension, ".mp3") == 0;
  

}

/* -------------------------------------------------------------------------- */
/* Recursive directory scanner                                                */
/* -------------------------------------------------------------------------- */

static void scan_directory(const char *path)
{
if (track_count >= MUSIC_MAX_TRACKS) {
return;
}

  
DIR *dir =
    opendir(path);

if (dir == NULL) {
    ESP_LOGE(
        TAG,
        "Could not open directory: %s",
        path
    );

    return;
}

struct dirent *entry;

while (
    (entry = readdir(dir)) != NULL &&
    track_count < MUSIC_MAX_TRACKS
) {
    if (
        strcmp(entry->d_name, ".") == 0 ||
        strcmp(entry->d_name, "..") == 0
    ) {
        continue;
    }


    char full_path[MUSIC_MAX_PATH];

    int written =
        snprintf(
            full_path,
            sizeof(full_path),
            "%s/%s",
            path,
            entry->d_name
        );

    if (
        written < 0 ||
        written >= sizeof(full_path)
    ) {
        ESP_LOGW(
            TAG,
            "Path too long, skipping: %s",
            entry->d_name
        );

        continue;
    }


    /*
     * Wokwi/FAT should normally provide d_type, but checking with
     * stat() makes this work reliably on the real filesystem too.
     */
    struct stat file_stat;

    if (
        stat(
            full_path,
            &file_stat
        ) != 0
    ) {
        ESP_LOGW(
            TAG,
            "Could not stat: %s",
            full_path
        );

        continue;
    }


    if (
        S_ISDIR(file_stat.st_mode)
    ) {
        scan_directory(full_path);
        continue;
    }


    if (
        !S_ISREG(file_stat.st_mode)
    ) {
        continue;
    }


    if (
        !has_mp3_extension(entry->d_name)
    ) {
        continue;
    }


    music_track_t *track =
        &tracks[track_count];


    memset(
        track,
        0,
        sizeof(*track)
    );


    snprintf(
        track->path,
        sizeof(track->path),
        "%s",
        full_path
    );


    snprintf(
        track->filename,
        sizeof(track->filename),
        "%s",
        entry->d_name
    );


    ESP_LOGI(
        TAG,
        "Found track %u: %s",
        (unsigned)track_count,
        track->path
    );


    track_count++;
}


closedir(dir);
  

}

/* -------------------------------------------------------------------------- */
/* Public scanner                                                             */
/* -------------------------------------------------------------------------- */

bool music_scan(void)
{
if (!sdcard_is_mounted()) {
ESP_LOGE(
TAG,
"Cannot scan: SD card is not mounted"
);

  
    return false;
}


ESP_LOGI(
    TAG,
    "Scanning music library"
);


track_count = 0;

memset(
    tracks,
    0,
    sizeof(tracks)
);


scan_directory(
    "/sdcard"
);


ESP_LOGI(
    TAG,
    "Music scan complete: %u MP3 file(s)",
    (unsigned)track_count
);


return true;
  

}

/* -------------------------------------------------------------------------- */
/* Track access                                                               */
/* -------------------------------------------------------------------------- */

size_t music_get_track_count(void)
{
return track_count;
}

const music_track_t *music_get_track(size_t index)
{
if (index >= track_count) {
return NULL;
}

  
return &tracks[index];
  

}
