#include "player.h"
#include "audio_out.h"
#include "library.h"

#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

/*
 * minimp3 (public domain): header-only MP3 decoder.
 * Layers I/II are compiled out; only MP3 is needed.
 */
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "third_party/minimp3.h"

static const char *TAG = "PLAYER";

/*
 * minimp3 keeps ~16 KB of scratch space on the stack
 * while decoding a frame.
 */
#define PLAYER_TASK_STACK (32 * 1024)

/*
 * "Previous" restarts the current track instead of
 * going back if we're further in than this.
 */
#define RESTART_THRESHOLD_MS 3000


/*
 * --------------------------------------------------
 * SHARED STATE (guarded by `lock`)
 * --------------------------------------------------
 *
 * The UI task changes this state; the player task
 * notices the requests and does the file work.
 */

static SemaphoreHandle_t lock = NULL;
static TaskHandle_t player_task_handle = NULL;

static int queue[LIBRARY_MAX_TRACKS];   // library track indexes
static int order[LIBRARY_MAX_TRACKS];   // play order (indexes into queue)
static int queue_length = 0;
static int queue_position = 0;          // index into order

static int current_track = -1;
static bool playing = false;

static uint32_t position_ms = 0;
static uint32_t duration_ms = 0;

static int volume = 12;
static bool shuffle = false;
static player_repeat_t repeat = REPEAT_OFF;

static uint32_t version = 0;

static bool load_requested = false;
static int32_t seek_request_ms = -1;


/*
 * --------------------------------------------------
 * PLAYER TASK STATE (only touched by the task)
 * --------------------------------------------------
 */

/*
 * Compressed-data buffer. minimp3 confirms it has found
 * a real frame by checking that more frames follow, so
 * this holds several at a time (a 320 kbps frame is
 * about 1.4 KB). It is topped up once less than
 * INPUT_REFILL_BYTES remain unread.
 */
#define INPUT_BUFFER_BYTES (16 * 1024)
#define INPUT_REFILL_BYTES (8 * 1024)

typedef struct {
    FILE *file;

    long data_start;            // first byte after any ID3v2 tag
    uint32_t data_size;         // audio bytes, excluding ID3 tags
    uint32_t data_read;         // audio bytes read from the file so far

    uint32_t sample_rate;
    uint32_t duration_ms;       // 0 if unknown
    uint64_t frames_played;     // PCM frames (one sample per channel)

    size_t in_pos;              // next unread byte in `input`
    size_t in_len;              // valid bytes in `input`

    mp3dec_t decoder;
} mp3_t;

static mp3_t mp3 = {0};

static uint8_t input[INPUT_BUFFER_BYTES];
static int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
static int16_t out_buffer[MINIMP3_MAX_SAMPLES_PER_FRAME];


/*
 * --------------------------------------------------
 * QUEUE HELPERS (call with lock held)
 * --------------------------------------------------
 */

static void changed(void)
{
    version++;
}


static void select_queue_position(int position)
{
    queue_position = position;
    current_track = queue[order[queue_position]];
    position_ms = 0;
    duration_ms = 0;
    load_requested = true;
    seek_request_ms = -1;
    changed();
}


/*
 * Builds the play order. With shuffle on, the entry at
 * queue index `first` is moved to the front so the
 * current song keeps playing.
 */
static void build_order(int first)
{
    for (int i = 0; i < queue_length; i++) {
        order[i] = i;
    }

    if (!shuffle) {
        queue_position = first;
        return;
    }

    for (int i = queue_length - 1; i > 0; i--) {
        int j = esp_random() % (i + 1);
        int tmp = order[i];
        order[i] = order[j];
        order[j] = tmp;
    }

    for (int i = 0; i < queue_length; i++) {
        if (order[i] == first) {
            order[i] = order[0];
            order[0] = first;
            break;
        }
    }

    queue_position = 0;
}


/*
 * Moves to the next song in the order. Returns false
 * if we were already at the end and not repeating.
 */
static bool advance(void)
{
    if (queue_position + 1 < queue_length) {
        select_queue_position(queue_position + 1);
        return true;
    }

    if (repeat == REPEAT_ALL && queue_length > 0) {
        select_queue_position(0);
        return true;
    }

    return false;
}


/*
 * End of the queue: stop and rewind to the start of
 * the last track so pressing play starts it again.
 */
static void stop_at_end(void)
{
    playing = false;
    select_queue_position(queue_position);
}


static void wake_player(void)
{
    if (player_task_handle != NULL) {
        xTaskNotifyGive(player_task_handle);
    }
}


/*
 * --------------------------------------------------
 * MP3 FILES (player task only)
 * --------------------------------------------------
 */

static void close_mp3(void)
{
    if (mp3.file != NULL) {
        fclose(mp3.file);
    }

    memset(&mp3, 0, sizeof(mp3));
}


static uint32_t read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}


/*
 * Moves unread bytes to the front of the buffer and
 * reads more audio data after them. Returns false if
 * nothing more could be read.
 */
static bool fill_input(void)
{
    size_t unread = mp3.in_len - mp3.in_pos;

    memmove(input, input + mp3.in_pos, unread);
    mp3.in_pos = 0;
    mp3.in_len = unread;

    size_t space = INPUT_BUFFER_BYTES - mp3.in_len;
    size_t remaining = mp3.data_size - mp3.data_read;
    size_t want = (space < remaining) ? space : remaining;

    if (want == 0) {
        return false;
    }

    size_t got = fread(input + mp3.in_len, 1, want, mp3.file);

    mp3.in_len += got;
    mp3.data_read += got;

    return got > 0;
}


/*
 * VBR files carry a "Xing" (or "Info") header in their
 * first frame with the total frame count, which gives an
 * exact duration. Returns 0 if there isn't one.
 */
static uint32_t xing_frame_count(const uint8_t *frame, size_t length)
{
    if (length < 4) {
        return 0;
    }

    bool mpeg1 = (frame[1] & 0x08) != 0;
    bool mono = (frame[3] & 0xC0) == 0xC0;
    bool has_crc = (frame[1] & 0x01) == 0;

    size_t pos = 4 + (has_crc ? 2 : 0) +
                 (mpeg1 ? (mono ? 17 : 32) : (mono ? 9 : 17));

    if (pos + 12 > length) {
        return 0;
    }

    if (memcmp(frame + pos, "Xing", 4) != 0 &&
        memcmp(frame + pos, "Info", 4) != 0) {
        return 0;
    }

    uint32_t flags = read_be32(frame + pos + 4);

    return (flags & 0x01) ? read_be32(frame + pos + 8) : 0;
}


/*
 * Opens an MP3 file, skips its ID3 tags, and works out
 * the sample rate and duration from the first frame.
 */
static bool open_mp3(const char *path)
{
    close_mp3();

    mp3.file = fopen(path, "rb");

    if (mp3.file == NULL) {
        ESP_LOGE(TAG, "Can't open %s", path);
        return false;
    }

    fseek(mp3.file, 0, SEEK_END);
    long file_size = ftell(mp3.file);
    fseek(mp3.file, 0, SEEK_SET);

    /*
     * ID3v2 tag at the start: "ID3", version, flags,
     * then a 28-bit "syncsafe" size (7 bits per byte).
     */
    uint8_t header[10];
    long audio_start = 0;

    if (fread(header, 1, 10, mp3.file) == 10 && memcmp(header, "ID3", 3) == 0) {

        audio_start = 10 +
            (((long)(header[6] & 0x7F) << 21) |
             ((long)(header[7] & 0x7F) << 14) |
             ((long)(header[8] & 0x7F) << 7) |
             (long)(header[9] & 0x7F));

        if (header[5] & 0x10) {
            audio_start += 10;  // tag footer
        }
    }

    /*
     * ID3v1 tag: the last 128 bytes, starting "TAG".
     */
    long audio_end = file_size;

    if (file_size >= 128) {

        uint8_t tag[3];

        fseek(mp3.file, file_size - 128, SEEK_SET);

        if (fread(tag, 1, 3, mp3.file) == 3 && memcmp(tag, "TAG", 3) == 0) {
            audio_end -= 128;
        }
    }

    if (audio_end <= audio_start) {
        ESP_LOGE(TAG, "%s has no audio data", path);
        close_mp3();
        return false;
    }

    mp3.data_start = audio_start;
    mp3.data_size = (uint32_t)(audio_end - audio_start);

    fseek(mp3.file, mp3.data_start, SEEK_SET);
    fill_input();

    /*
     * Find the first frame without decoding it (pcm =
     * NULL) to learn the format.
     */
    mp3dec_init(&mp3.decoder);

    mp3dec_frame_info_t info;
    int samples_per_frame = mp3dec_decode_frame(
        &mp3.decoder, input, mp3.in_len, NULL, &info);

    if (samples_per_frame == 0 || info.layer != 3) {
        ESP_LOGE(TAG, "%s is not an MP3 file", path);
        close_mp3();
        return false;
    }

    mp3.sample_rate = info.hz;

    uint32_t frame_count = xing_frame_count(
        input + info.frame_offset, mp3.in_len - info.frame_offset);

    if (frame_count > 0) {
        mp3.duration_ms = (uint32_t)(
            (uint64_t)frame_count * samples_per_frame * 1000 / info.hz);
    } else if (info.bitrate_kbps > 0) {
        /*
         * Constant bitrate: bits / kbps = milliseconds.
         */
        mp3.duration_ms = (uint32_t)(
            (uint64_t)mp3.data_size * 8 / info.bitrate_kbps);
    }

    /*
     * Start decoding from the beginning of the buffer.
     */
    mp3dec_init(&mp3.decoder);

    ESP_LOGI(
        TAG,
        "Playing %s (%lu Hz, %d kbps, %lu s)",
        path,
        (unsigned long)info.hz,
        info.bitrate_kbps,
        (unsigned long)(mp3.duration_ms / 1000)
    );

    return true;
}


static uint32_t mp3_position_ms(void)
{
    if (mp3.sample_rate == 0) {
        return 0;
    }

    return (uint32_t)(mp3.frames_played * 1000 / mp3.sample_rate);
}


/*
 * Jumps to roughly `ms` by byte position, which is exact
 * for constant-bitrate files and close for VBR. The
 * decoder re-syncs on the next frame header.
 */
static void seek_mp3(uint32_t ms)
{
    uint32_t offset = 0;

    if (mp3.duration_ms > 0) {
        offset = (uint32_t)((uint64_t)mp3.data_size * ms / mp3.duration_ms);
    }

    if (offset > mp3.data_size) {
        offset = mp3.data_size;
    }

    fseek(mp3.file, mp3.data_start + offset, SEEK_SET);

    mp3.data_read = offset;
    mp3.in_pos = 0;
    mp3.in_len = 0;
    mp3.frames_played = (uint64_t)ms * mp3.sample_rate / 1000;

    mp3dec_init(&mp3.decoder);
}


/*
 * Decodes one MP3 frame, converts it to stereo with
 * volume applied, and sends it to the output. Returns
 * false at the end of the track.
 */
static bool play_chunk(int current_volume)
{
    for (;;) {

        if (mp3.in_len - mp3.in_pos < INPUT_REFILL_BYTES &&
            mp3.data_read < mp3.data_size) {
            fill_input();
        }

        size_t available = mp3.in_len - mp3.in_pos;

        if (available == 0) {
            return false;
        }

        mp3dec_frame_info_t info;

        int samples = mp3dec_decode_frame(
            &mp3.decoder, input + mp3.in_pos, available, pcm, &info);

        if (info.frame_bytes == 0) {
            /*
             * Only part of a frame left: read more, or
             * stop if the file is finished.
             */
            if (!fill_input()) {
                return false;
            }
            continue;
        }

        mp3.in_pos += info.frame_bytes;

        if (samples == 0) {
            /*
             * Skipped junk, or a frame that needs data
             * from before a seek point. Keep going.
             */
            continue;
        }

        if ((uint32_t)info.hz != mp3.sample_rate) {
            mp3.sample_rate = info.hz;
            audio_out_start(mp3.sample_rate);
        }

        /*
         * Volume curve: squared, which sounds more even
         * than a straight line.
         */
        int32_t gain = current_volume * current_volume;
        const int32_t full = PLAYER_MAX_VOLUME * PLAYER_MAX_VOLUME;

        for (int i = 0; i < samples; i++) {

            int32_t left;
            int32_t right;

            if (info.channels == 2) {
                left = pcm[i * 2];
                right = pcm[i * 2 + 1];
            } else {
                left = right = pcm[i];
            }

            out_buffer[i * 2] = (int16_t)(left * gain / full);
            out_buffer[i * 2 + 1] = (int16_t)(right * gain / full);
        }

        audio_out_write(out_buffer, samples);

        mp3.frames_played += samples;

        return true;
    }
}


/*
 * --------------------------------------------------
 * PLAYER TASK
 * --------------------------------------------------
 */

static void player_task(void *arg)
{
    bool was_playing = false;
    int failures_in_a_row = 0;

    for (;;) {

        /*
         * Pick up requests from the UI.
         */
        xSemaphoreTake(lock, portMAX_DELAY);

        bool do_load = load_requested;
        int track = current_track;
        int32_t seek_ms = seek_request_ms;
        bool is_playing = playing;
        int current_volume = volume;

        load_requested = false;
        seek_request_ms = -1;

        xSemaphoreGive(lock);

        if (do_load) {

            /*
             * Mute across the track change; playback
             * below unmutes once new audio is flowing.
             */
            audio_out_set_muted(true);
            was_playing = false;

            const library_track_t *info = library_track(track);

            if (info != NULL && open_mp3(info->path)) {

                failures_in_a_row = 0;
                audio_out_start(mp3.sample_rate);

                xSemaphoreTake(lock, portMAX_DELAY);
                duration_ms = mp3.duration_ms;
                position_ms = 0;
                changed();
                xSemaphoreGive(lock);

            } else {

                close_mp3();

                /*
                 * Skip unplayable files, but give up if
                 * nothing in the queue works.
                 */
                xSemaphoreTake(lock, portMAX_DELAY);

                failures_in_a_row++;

                if (!playing ||
                    failures_in_a_row >= queue_length ||
                    !advance()) {
                    playing = false;
                    changed();
                }

                xSemaphoreGive(lock);
                continue;
            }
        }

        if (seek_ms >= 0 && mp3.file != NULL) {

            audio_out_set_muted(true);
            was_playing = false;

            seek_mp3((uint32_t)seek_ms);
            audio_out_start(mp3.sample_rate);

            xSemaphoreTake(lock, portMAX_DELAY);
            position_ms = mp3_position_ms();
            xSemaphoreGive(lock);
        }

        if (!is_playing || mp3.file == NULL) {

            audio_out_set_muted(true);
            was_playing = false;

            /*
             * Sleep until the UI asks for something.
             */
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        if (!was_playing) {
            audio_out_start(mp3.sample_rate);
            audio_out_set_muted(false);
            was_playing = true;
        }

        if (play_chunk(current_volume)) {

            xSemaphoreTake(lock, portMAX_DELAY);
            position_ms = mp3_position_ms();
            xSemaphoreGive(lock);

            continue;
        }

        /*
         * End of track.
         */
        xSemaphoreTake(lock, portMAX_DELAY);

        if (repeat == REPEAT_ONE) {
            select_queue_position(queue_position);
        } else if (!advance()) {
            stop_at_end();
        }

        xSemaphoreGive(lock);
    }
}


/*
 * --------------------------------------------------
 * PUBLIC FUNCTIONS (called from the UI task)
 * --------------------------------------------------
 */

void player_init(void)
{
    lock = xSemaphoreCreateMutex();

    audio_out_init();

    xTaskCreatePinnedToCore(
        player_task,
        "player",
        PLAYER_TASK_STACK,
        NULL,
        5,
        &player_task_handle,
        1
    );

    ESP_LOGI(TAG, "Player initialized");
}


void player_play_queue(const int *tracks, int count, int start)
{
    if (count <= 0 || start < 0 || start >= count) {
        return;
    }

    if (count > LIBRARY_MAX_TRACKS) {
        count = LIBRARY_MAX_TRACKS;
    }

    xSemaphoreTake(lock, portMAX_DELAY);

    memcpy(queue, tracks, count * sizeof(int));
    queue_length = count;

    build_order(start);

    playing = true;
    select_queue_position(queue_position);

    xSemaphoreGive(lock);

    wake_player();
}


void player_toggle_pause(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);

    if (current_track >= 0) {
        playing = !playing;
        changed();
    }

    xSemaphoreGive(lock);

    wake_player();
}


void player_next(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);

    if (queue_length > 0 && !advance()) {
        stop_at_end();
    }

    xSemaphoreGive(lock);

    wake_player();
}


void player_previous(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);

    if (queue_length > 0) {

        if (position_ms > RESTART_THRESHOLD_MS) {
            seek_request_ms = 0;
            position_ms = 0;
        } else if (queue_position > 0) {
            select_queue_position(queue_position - 1);
        } else if (repeat == REPEAT_ALL) {
            select_queue_position(queue_length - 1);
        } else {
            seek_request_ms = 0;
            position_ms = 0;
        }
    }

    xSemaphoreGive(lock);

    wake_player();
}


void player_seek(int seconds)
{
    xSemaphoreTake(lock, portMAX_DELAY);

    if (current_track >= 0 && duration_ms > 0) {

        int64_t target = (int64_t)position_ms + (int64_t)seconds * 1000;

        if (target < 0) {
            target = 0;
        }

        /*
         * Don't jump past the end; land just before it.
         */
        if (target > (int64_t)duration_ms - 1000) {
            target = (duration_ms > 1000) ? duration_ms - 1000 : 0;
        }

        seek_request_ms = (int32_t)target;
        position_ms = (uint32_t)target;
    }

    xSemaphoreGive(lock);

    wake_player();
}


void player_change_volume(int delta)
{
    xSemaphoreTake(lock, portMAX_DELAY);

    volume += delta;

    if (volume < 0) {
        volume = 0;
    }

    if (volume > PLAYER_MAX_VOLUME) {
        volume = PLAYER_MAX_VOLUME;
    }

    changed();

    xSemaphoreGive(lock);
}


void player_toggle_shuffle(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);

    int current_index = (queue_length > 0) ? order[queue_position] : 0;

    shuffle = !shuffle;

    build_order(current_index);
    changed();

    ESP_LOGI(TAG, "Shuffle %s", shuffle ? "on" : "off");

    xSemaphoreGive(lock);
}


void player_cycle_repeat(void)
{
    static const char *names[] = {"off", "all", "one"};

    xSemaphoreTake(lock, portMAX_DELAY);

    repeat = (player_repeat_t)((repeat + 1) % 3);
    changed();

    ESP_LOGI(TAG, "Repeat %s", names[repeat]);

    xSemaphoreGive(lock);
}


void player_get_status(player_status_t *status)
{
    xSemaphoreTake(lock, portMAX_DELAY);

    status->track = current_track;
    status->playing = playing;
    status->queue_position = queue_position;
    status->queue_length = queue_length;
    status->position_ms = position_ms;
    status->duration_ms = duration_ms;
    status->volume = volume;
    status->shuffle = shuffle;
    status->repeat = repeat;
    status->version = version;

    xSemaphoreGive(lock);
}
