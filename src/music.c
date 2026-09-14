#include "music.h"
#include "sdcard.h"

#include "esp_log.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <stdint.h>

static const char *TAG = "MUSIC";

static music_track_t tracks[MUSIC_MAX_TRACKS];
static size_t track_count = 0;

/* -------------------------------------------------------------------------- */
/* String helpers                                                             */
/* -------------------------------------------------------------------------- */

static void copy_string(
char *dest,
size_t dest_size,
const char *src
)
{
if (dest == NULL || dest_size == 0) {
return;
}

  
if (src == NULL) {
    dest[0] = '\0';
    return;
}

snprintf(
    dest,
    dest_size,
    "%s",
    src
);
  

}

static void trim_string(char *text)
{
if (text == NULL) {
return;
}

  
size_t len = strlen(text);

while (
    len > 0 &&
    (
        text[len - 1] == ' ' ||
        text[len - 1] == '\r' ||
        text[len - 1] == '\n' ||
        text[len - 1] == '\t'
    )
) {
    text[len - 1] = '\0';
    len--;
}


size_t start = 0;

while (
    text[start] == ' ' ||
    text[start] == '\t'
) {
    start++;
}


if (start > 0) {
    memmove(
        text,
        text + start,
        strlen(text + start) + 1
    );
}
  

}

/* -------------------------------------------------------------------------- */
/* MP3 detection                                                              */
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

return strcasecmp(
    extension,
    ".mp3"
) == 0;
  

}

/* -------------------------------------------------------------------------- */
/* Default metadata                                                           */
/* -------------------------------------------------------------------------- */

static void set_default_metadata(
music_track_t *track
)
{
copy_string(
track->title,
sizeof(track->title),
track->filename
);

  
char *extension =
    strrchr(
        track->title,
        '.'
    );


if (
    extension != NULL &&
    strcasecmp(
        extension,
        ".mp3"
    ) == 0
) {
    *extension = '\0';
}


trim_string(
    track->title
);


copy_string(
    track->artist,
    sizeof(track->artist),
    "Unknown Artist"
);


copy_string(
    track->album,
    sizeof(track->album),
    "Unknown Album"
);
  

}

/* -------------------------------------------------------------------------- */
/* Big-endian helper                                                          */
/* -------------------------------------------------------------------------- */

static uint32_t read_be32(
const uint8_t *data
)
{
return
((uint32_t)data[0] << 24) |
((uint32_t)data[1] << 16) |
((uint32_t)data[2] << 8) |
(uint32_t)data[3];
}

/* -------------------------------------------------------------------------- */
/* ID3 text frame                                                             */
/* -------------------------------------------------------------------------- */

static void read_text_frame(
FILE *file,
uint32_t frame_size,
char *output,
size_t output_size
)
{
if (
file == NULL ||
output == NULL ||
output_size == 0 ||
frame_size == 0 ||
frame_size > 1024
) {
if (file != NULL && frame_size > 0) {
fseek(
file,
(long)frame_size,
SEEK_CUR
);
}

  
    return;
}


uint8_t buffer[1025];


if (
    fread(
        buffer,
        1,
        frame_size,
        file
    ) != frame_size
) {
    return;
}


uint8_t encoding =
    buffer[0];


/* UTF-8 */
if (encoding == 3) {

    size_t length =
        frame_size - 1;

    if (
        length >= output_size
    ) {
        length = output_size - 1;
    }

    memcpy(
        output,
        &buffer[1],
        length
    );

    output[length] = '\0';

    trim_string(
        output
    );

    return;
}


/* ISO-8859-1 / ASCII */
if (encoding == 0) {

    size_t length =
        frame_size - 1;

    if (
        length >= output_size
    ) {
        length = output_size - 1;
    }

    memcpy(
        output,
        &buffer[1],
        length
    );

    output[length] = '\0';

    trim_string(
        output
    );

    return;
}


/*
 * Basic UTF-16 support.
 *
 * This intentionally handles ordinary ASCII characters from
 * UTF-16 metadata. Full Unicode conversion can be added later.
 */
if (
    encoding == 1 ||
    encoding == 2
) {

    size_t pos = 1;
    size_t out = 0;

    bool little_endian = false;


    if (
        encoding == 1 &&
        frame_size >= 3
    ) {

        if (
            buffer[1] == 0xFF &&
            buffer[2] == 0xFE
        ) {
            little_endian = true;
            pos = 3;

        } else if (
            buffer[1] == 0xFE &&
            buffer[2] == 0xFF
        ) {
            little_endian = false;
            pos = 3;
        }
    }


    while (
        pos + 1 < frame_size &&
        out + 1 < output_size
    ) {

        uint16_t ch;


        if (little_endian) {

            ch =
                ((uint16_t)buffer[pos + 1] << 8) |
                buffer[pos];

        } else {

            ch =
                ((uint16_t)buffer[pos] << 8) |
                buffer[pos + 1];
        }


        pos += 2;


        if (ch == 0) {
            break;
        }


        if (ch < 0x80) {
            output[out++] =
                (char)ch;
        } else {
            output[out++] = '?';
        }
    }


    output[out] = '\0';

    trim_string(
        output
    );
}
  

}

/* -------------------------------------------------------------------------- */
/* ID3 metadata                                                               */
/* -------------------------------------------------------------------------- */

static void read_id3_metadata(
music_track_t *track
)
{
FILE *file =
fopen(
track->path,
"rb"
);

  
if (file == NULL) {

    ESP_LOGW(
        TAG,
        "Could not open MP3 for metadata: %s",
        track->path
    );

    return;
}


uint8_t header[10];


if (
    fread(
        header,
        1,
        sizeof(header),
        file
    ) != sizeof(header)
) {

    fclose(file);
    return;
}


/*
 * No ID3 tag.
 */
if (
    header[0] != 'I' ||
    header[1] != 'D' ||
    header[2] != '3'
) {

    fclose(file);
    return;
}


uint8_t version_major =
    header[3];


if (
    version_major != 3 &&
    version_major != 4
) {

    ESP_LOGW(
        TAG,
        "Unsupported ID3 version %u: %s",
        version_major,
        track->path
    );

    fclose(file);
    return;
}


/*
 * ID3v2 tag size is a syncsafe integer.
 */
uint32_t tag_size =
    ((uint32_t)(header[6] & 0x7F) << 21) |
    ((uint32_t)(header[7] & 0x7F) << 14) |
    ((uint32_t)(header[8] & 0x7F) << 7) |
    (uint32_t)(header[9] & 0x7F);


uint32_t processed = 0;


while (
    processed + 10 <= tag_size
) {

    uint8_t frame_header[10];


    if (
        fread(
            frame_header,
            1,
            sizeof(frame_header),
            file
        ) != sizeof(frame_header)
    ) {
        break;
    }


    processed += 10;


    /*
     * Padding.
     */
    if (
        frame_header[0] == 0
    ) {
        break;
    }


    char frame_id[5];

    frame_id[0] = frame_header[0];
    frame_id[1] = frame_header[1];
    frame_id[2] = frame_header[2];
    frame_id[3] = frame_header[3];
    frame_id[4] = '\0';


    uint32_t frame_size =
        read_be32(
            &frame_header[4]
        );


    if (
        frame_size >
        tag_size - processed
    ) {
        break;
    }


    if (
        frame_size == 0
    ) {
        continue;
    }


    if (
        strcmp(
            frame_id,
            "TIT2"
        ) == 0
    ) {

        read_text_frame(
            file,
            frame_size,
            track->title,
            sizeof(track->title)
        );

    } else if (
        strcmp(
            frame_id,
            "TPE1"
        ) == 0
    ) {

        read_text_frame(
            file,
            frame_size,
            track->artist,
            sizeof(track->artist)
        );

    } else if (
        strcmp(
            frame_id,
            "TALB"
        ) == 0
    ) {

        read_text_frame(
            file,
            frame_size,
            track->album,
            sizeof(track->album)
        );

    } else {

        fseek(
            file,
            (long)frame_size,
            SEEK_CUR
        );
    }


    processed += frame_size;
}


fclose(file);


if (
    track->title[0] == '\0'
) {

    set_default_metadata(
        track
    );
}


if (
    track->artist[0] == '\0'
) {

    copy_string(
        track->artist,
        sizeof(track->artist),
        "Unknown Artist"
    );
}


if (
    track->album[0] == '\0'
) {

    copy_string(
        track->album,
        sizeof(track->album),
        "Unknown Album"
    );
}
  

}

/* -------------------------------------------------------------------------- */
/* Recursive scanner                                                          */
/* -------------------------------------------------------------------------- */

static void scan_directory(
const char *path
)
{
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


ESP_LOGI(
    TAG,
    "Scanning directory: %s",
    path
);


struct dirent *entry;


while (
    (entry = readdir(dir)) != NULL
) {

    if (
        strcmp(
            entry->d_name,
            "."
        ) == 0 ||
        strcmp(
            entry->d_name,
            ".."
        ) == 0
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
        written >=
            (int)sizeof(full_path)
    ) {

        ESP_LOGW(
            TAG,
            "Path too long, skipping: %s",
            entry->d_name
        );

        continue;
    }


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


    /* ------------------------------------------------------------------ */
    /* Directory                                                          */
    /* ------------------------------------------------------------------ */

    if (
        S_ISDIR(
            file_stat.st_mode
        )
    ) {

        ESP_LOGI(
            TAG,
            "DIR : %s",
            full_path
        );


        if (
            track_count <
            MUSIC_MAX_TRACKS
        ) {

            scan_directory(
                full_path
            );
        }


        continue;
    }


    /* ------------------------------------------------------------------ */
    /* File                                                                */
    /* ------------------------------------------------------------------ */

    if (
        S_ISREG(
            file_stat.st_mode
        )
    ) {

        ESP_LOGI(
            TAG,
            "FILE: %s",
            full_path
        );
    }


    /* ------------------------------------------------------------------ */
    /* MP3                                                                 */
    /* ------------------------------------------------------------------ */

    if (
        S_ISREG(
            file_stat.st_mode
        ) &&
        has_mp3_extension(
            entry->d_name
        )
    ) {

        if (
            track_count >=
            MUSIC_MAX_TRACKS
        ) {

            ESP_LOGW(
                TAG,
                "Maximum track count reached"
            );

            break;
        }


        music_track_t *track =
            &tracks[track_count];


        memset(
            track,
            0,
            sizeof(*track)
        );


        copy_string(
            track->path,
            sizeof(track->path),
            full_path
        );


        copy_string(
            track->filename,
            sizeof(track->filename),
            entry->d_name
        );


        set_default_metadata(
            track
        );


        /*
         * Try to read ID3 metadata.
         */
        read_id3_metadata(
            track
        );


        ESP_LOGI(
            TAG,
            "MP3 FOUND #%u",
            (unsigned)track_count
        );

        ESP_LOGI(
            TAG,
            "  Title : %s",
            track->title
        );

        ESP_LOGI(
            TAG,
            "  Artist: %s",
            track->artist
        );

        ESP_LOGI(
            TAG,
            "  Album : %s",
            track->album
        );

        ESP_LOGI(
            TAG,
            "  Path  : %s",
            track->path
        );


        track_count++;
    }
}


closedir(dir);
  

}

/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

bool music_scan(void)
{
if (
!sdcard_is_mounted()
) {

  
    ESP_LOGE(
        TAG,
        "Cannot scan: SD card is not mounted"
    );

    return false;
}


track_count = 0;


memset(
    tracks,
    0,
    sizeof(tracks)
);


ESP_LOGI(
    TAG,
    "================================"
);

ESP_LOGI(
    TAG,
    "Scanning music library"
);

ESP_LOGI(
    TAG,
    "Starting at /sdcard"
);

ESP_LOGI(
    TAG,
    "================================"
);


scan_directory(
    "/sdcard"
);


ESP_LOGI(
    TAG,
    "================================"
);

ESP_LOGI(
    TAG,
    "Music scan complete"
);

ESP_LOGI(
    TAG,
    "MP3 files found: %u",
    (unsigned)track_count
);

ESP_LOGI(
    TAG,
    "================================"
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

const music_track_t *music_get_track(
size_t index
)
{
if (
index >= track_count
) {
return NULL;
}

  
return &tracks[index];
  

}
