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

static const char *TAG = "PLAYER";

/*
 * Frames read from the SD card per loop iteration.
 */
#define CHUNK_FRAMES 512

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

typedef struct {
    FILE *file;

    uint32_t sample_rate;
    uint16_t channels;
    uint16_t block_align;

    long data_start;
    uint32_t data_size;
    uint32_t data_read;
} wav_t;

static wav_t wav = {0};

static int16_t read_buffer[CHUNK_FRAMES * 2];
static int16_t out_buffer[CHUNK_FRAMES * 2];


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
 * WAV FILES (player task only)
 * --------------------------------------------------
 */

static void close_wav(void)
{
    if (wav.file != NULL) {
        fclose(wav.file);
    }

    memset(&wav, 0, sizeof(wav));
}


static uint16_t read_u16(const uint8_t *bytes)
{
    return (uint16_t)(bytes[0] | (bytes[1] << 8));
}


static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}


/*
 * Opens a 16-bit PCM WAV file (mono or stereo, any
 * sample rate) and positions it at the audio data.
 */
static bool open_wav(const char *path)
{
    close_wav();

    wav.file = fopen(path, "rb");

    if (wav.file == NULL) {
        ESP_LOGE(TAG, "Can't open %s", path);
        return false;
    }

    uint8_t header[12];

    if (fread(header, 1, 12, wav.file) != 12 ||
        memcmp(header, "RIFF", 4) != 0 ||
        memcmp(header + 8, "WAVE", 4) != 0) {
        ESP_LOGE(TAG, "%s is not a WAV file", path);
        close_wav();
        return false;
    }

    bool have_format = false;
    uint16_t bits_per_sample = 0;

    /*
     * Walk the chunks until we reach the audio data.
     */
    for (;;) {

        uint8_t chunk[8];

        if (fread(chunk, 1, 8, wav.file) != 8) {
            ESP_LOGE(TAG, "%s has no audio data", path);
            close_wav();
            return false;
        }

        uint32_t chunk_size = read_u32(chunk + 4);

        if (memcmp(chunk, "fmt ", 4) == 0) {

            uint8_t format[16];

            if (chunk_size < 16 || fread(format, 1, 16, wav.file) != 16) {
                break;
            }

            uint16_t audio_format = read_u16(format);

            wav.channels = read_u16(format + 2);
            wav.sample_rate = read_u32(format + 4);
            wav.block_align = read_u16(format + 12);
            bits_per_sample = read_u16(format + 14);

            /*
             * 1 = PCM, 0xFFFE = WAVE_FORMAT_EXTENSIBLE
             * (used by some editors for plain PCM too).
             */
            have_format =
                (audio_format == 1 || audio_format == 0xFFFE) &&
                bits_per_sample == 16 &&
                (wav.channels == 1 || wav.channels == 2) &&
                wav.sample_rate > 0;

            /*
             * Skip any extra format bytes (and the pad byte).
             */
            fseek(wav.file, (chunk_size - 16) + (chunk_size & 1), SEEK_CUR);

        } else if (memcmp(chunk, "data", 4) == 0) {

            wav.data_start = ftell(wav.file);
            wav.data_size = chunk_size;
            break;

        } else {

            fseek(wav.file, chunk_size + (chunk_size & 1), SEEK_CUR);
        }
    }

    if (!have_format || wav.data_size == 0) {
        ESP_LOGE(
            TAG,
            "%s: unsupported format (need 16-bit PCM, got %u-bit, %u ch)",
            path,
            bits_per_sample,
            wav.channels
        );
        close_wav();
        return false;
    }

    ESP_LOGI(
        TAG,
        "Playing %s (%lu Hz, %u ch)",
        path,
        (unsigned long)wav.sample_rate,
        wav.channels
    );

    return true;
}


static uint32_t bytes_to_ms(uint32_t bytes)
{
    uint64_t frames = bytes / wav.block_align;

    return (uint32_t)(frames * 1000 / wav.sample_rate);
}


static void seek_wav(uint32_t ms)
{
    uint64_t frame = (uint64_t)ms * wav.sample_rate / 1000;
    uint32_t offset = (uint32_t)(frame * wav.block_align);

    if (offset > wav.data_size) {
        offset = wav.data_size;
    }

    fseek(wav.file, wav.data_start + offset, SEEK_SET);
    wav.data_read = offset;
}


/*
 * Reads one chunk, converts it to stereo with volume
 * applied, and sends it to the output. Returns false
 * at the end of the track.
 */
static bool play_chunk(int current_volume)
{
    uint32_t remaining = wav.data_size - wav.data_read;
    uint32_t want = CHUNK_FRAMES * wav.block_align;

    if (want > remaining) {
        want = remaining;
    }

    size_t got = (want > 0) ? fread(read_buffer, 1, want, wav.file) : 0;
    size_t frames = got / wav.block_align;

    if (frames == 0) {
        return false;
    }

    wav.data_read += got;

    /*
     * Volume curve: squared, which sounds more even
     * than a straight line.
     */
    int32_t gain = current_volume * current_volume;
    const int32_t full = PLAYER_MAX_VOLUME * PLAYER_MAX_VOLUME;

    for (size_t i = 0; i < frames; i++) {

        int32_t left;
        int32_t right;

        if (wav.channels == 2) {
            left = read_buffer[i * 2];
            right = read_buffer[i * 2 + 1];
        } else {
            left = right = read_buffer[i];
        }

        out_buffer[i * 2] = (int16_t)(left * gain / full);
        out_buffer[i * 2 + 1] = (int16_t)(right * gain / full);
    }

    audio_out_write(out_buffer, frames);

    return true;
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

            if (info != NULL && open_wav(info->path)) {

                failures_in_a_row = 0;
                audio_out_start(wav.sample_rate);

                xSemaphoreTake(lock, portMAX_DELAY);
                duration_ms = bytes_to_ms(wav.data_size);
                position_ms = 0;
                changed();
                xSemaphoreGive(lock);

            } else {

                close_wav();

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

        if (seek_ms >= 0 && wav.file != NULL) {

            audio_out_set_muted(true);
            was_playing = false;

            seek_wav((uint32_t)seek_ms);
            audio_out_start(wav.sample_rate);

            xSemaphoreTake(lock, portMAX_DELAY);
            position_ms = bytes_to_ms(wav.data_read);
            xSemaphoreGive(lock);
        }

        if (!is_playing || wav.file == NULL) {

            audio_out_set_muted(true);
            was_playing = false;

            /*
             * Sleep until the UI asks for something.
             */
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            continue;
        }

        if (!was_playing) {
            audio_out_start(wav.sample_rate);
            audio_out_set_muted(false);
            was_playing = true;
        }

        if (play_chunk(current_volume)) {

            xSemaphoreTake(lock, portMAX_DELAY);
            position_ms = bytes_to_ms(wav.data_read);
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
        4096,
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
