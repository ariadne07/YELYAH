#include "ui.h"
#include "audio_out.h"
#include "battery.h"
#include "display.h"
#include "library.h"
#include "pins.h"
#include "player.h"
#include "storage.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "UI";

/*
 * --------------------------------------------------
 * LAYOUT
 * --------------------------------------------------
 *
 * The Now Playing screen fits in the top 240x240 so
 * it carries over to the square ST7789 display. Lists
 * use however many rows fit on the screen.
 */

#define MARGIN          8
#define TEXT_SCALE      2
#define CHAR_W          (FONT_CHAR_WIDTH * TEXT_SCALE)

#define HEADER_TEXT_Y   10
#define HEADER_HEIGHT   34

#define LIST_TOP        (HEADER_HEIGHT + 4)
#define ROW_HEIGHT      26
#define VISIBLE_ROWS    ((DISPLAY_HEIGHT - LIST_TOP) / ROW_HEIGHT)

#define SCROLLBAR_W     4
#define ROW_RIGHT       (DISPLAY_WIDTH - SCROLLBAR_W - 2)
#define ROW_CHARS       ((ROW_RIGHT - 2 * MARGIN) / CHAR_W)
#define LINE_CHARS      ((DISPLAY_WIDTH - 2 * MARGIN) / CHAR_W)

#define NP_TITLE_Y      48
#define NP_ARTIST_Y     74
#define NP_ALBUM_Y      100
#define NP_BAR_Y        150
#define NP_BAR_H        6
#define NP_TIME_Y       164
#define NP_STATUS_Y     206

#define TOAST_HEIGHT    26

#define STATUS_POLL_US  200000
#define BATTERY_POLL_US 2000000
#define TOAST_US        1200000

/*
 * Battery percentage, small text in the top-right corner
 * above the header line.
 */
#define BATTERY_CELLS   4
#define BATTERY_X       (DISPLAY_WIDTH - 2 - BATTERY_CELLS * FONT_CHAR_WIDTH)
#define BATTERY_Y       1

/*
 * Consecutive critical readings before shutting down,
 * so one noisy sample can't turn the player off.
 */
#define CRITICAL_READINGS 3

#define MAX_DEPTH       8


/*
 * --------------------------------------------------
 * VIEWS
 * --------------------------------------------------
 */

typedef enum {
    VIEW_MENU,
    VIEW_SONGS,
    VIEW_ARTISTS,
    VIEW_ALBUMS,
    VIEW_ARTIST_ALBUMS,     // param = artist
    VIEW_ALBUM_TRACKS,      // param = album
    VIEW_NOW_PLAYING,
    VIEW_PLAYLISTS,
    VIEW_SETTINGS
} view_kind_t;

typedef struct {
    view_kind_t kind;
    int param;
    int selection;
    int scroll;
} view_t;

/*
 * Navigation stack. LEFT (or double-click on Now
 * Playing) goes back one level.
 */
static view_t stack[MAX_DEPTH];
static int depth = 0;

/*
 * Entries of the list currently on screen.
 */
static int items[LIBRARY_MAX_TRACKS];
static int item_count = 0;

#define MENU_ITEM_COUNT 5

static const char *menu_items[MENU_ITEM_COUNT] = {
    "Music",
    "Artists",
    "Albums",
    "Playlists",
    "Settings"
};

static bool toast_active = false;
static int64_t toast_until = 0;
static int64_t last_poll = 0;

static int64_t last_battery_poll = 0;
static int battery_mv = 0;
static bool battery_low_warned = false;
static int critical_count = 0;

/*
 * What the Now Playing screen currently shows.
 */
static uint32_t drawn_version = 0;
static uint32_t drawn_second = 0;


static view_t *top(void)
{
    return &stack[depth - 1];
}


static bool is_list(view_kind_t kind)
{
    return kind <= VIEW_ALBUM_TRACKS;
}


/*
 * --------------------------------------------------
 * DRAWING HELPERS
 * --------------------------------------------------
 */

/*
 * Draws text in a fixed number of character cells,
 * truncating with ".." or padding with spaces. Padding
 * overwrites old text without clearing first, which
 * avoids flicker.
 */
static void draw_padded(
    int x,
    int y,
    const char *text,
    int cells,
    uint16_t color,
    uint16_t bg_color
)
{
    char buf[48];

    if (cells >= (int)sizeof(buf)) {
        cells = sizeof(buf) - 1;
    }

    int len = strlen(text);

    if (len > cells) {
        memcpy(buf, text, cells - 2);
        buf[cells - 2] = '.';
        buf[cells - 1] = '.';
    } else {
        memcpy(buf, text, len);
        memset(buf + len, ' ', cells - len);
    }

    buf[cells] = '\0';

    display_draw_text(x, y, buf, color, bg_color, TEXT_SCALE);
}


/*
 * Draws text so it ends `cells` characters from x.
 */
static void draw_right_aligned(
    int right_x,
    int y,
    const char *text,
    int cells,
    uint16_t color
)
{
    char buf[24];

    snprintf(buf, sizeof(buf), "%*s", cells, text);

    display_draw_text(
        right_x - cells * CHAR_W,
        y,
        buf,
        color,
        COLOR_BLACK,
        TEXT_SCALE
    );
}


static void format_time(uint32_t ms, char *buf, size_t size)
{
    uint32_t seconds = ms / 1000;

    snprintf(buf, size, "%lu:%02lu",
             (unsigned long)(seconds / 60),
             (unsigned long)(seconds % 60));
}


static void draw_header(const char *title)
{
    draw_padded(MARGIN, HEADER_TEXT_Y, title, LINE_CHARS, COLOR_WHITE, COLOR_BLACK);

    display_fill_rect(0, HEADER_HEIGHT - 2, DISPLAY_WIDTH, 1, COLOR_GREY);
}


static void show_toast(const char *text)
{
    int y = DISPLAY_HEIGHT - TOAST_HEIGHT;

    display_fill_rect(0, y, DISPLAY_WIDTH, TOAST_HEIGHT, COLOR_BLUE);
    draw_padded(MARGIN, y + 6, text, LINE_CHARS, COLOR_WHITE, COLOR_BLUE);

    toast_active = true;
    toast_until = esp_timer_get_time() + TOAST_US;
}


/*
 * --------------------------------------------------
 * BATTERY
 * --------------------------------------------------
 */

static void draw_battery(void)
{
    char text[8] = "";

    if (battery_mv > 0) {
        snprintf(text, sizeof(text), "%d%%", battery_percent(battery_mv));
    }

    char padded[8];

    snprintf(padded, sizeof(padded), "%*s", BATTERY_CELLS, text);

    uint16_t color = (battery_mv > 0 && battery_mv < BATTERY_LOW_MV)
                         ? COLOR_RED
                         : COLOR_GREY;

    display_draw_text(BATTERY_X, BATTERY_Y, padded, color, COLOR_BLACK, 1);
}


/*
 * Stops everything and puts the chip into deep sleep
 * before the battery's own protection cuts power
 * abruptly. Wakes again on reset or power cycle.
 */
static void shut_down_for_battery(void)
{
    ESP_LOGW(TAG, "Battery critical (%d mV), shutting down", battery_mv);

    audio_out_set_muted(true);

    display_clear(COLOR_BLACK);
    display_draw_text(MARGIN, 90, "Battery empty", COLOR_RED, COLOR_BLACK, TEXT_SCALE);
    display_draw_text(MARGIN, 120, "Please charge", COLOR_WHITE, COLOR_BLACK, TEXT_SCALE);

    vTaskDelay(pdMS_TO_TICKS(3000));

    /*
     * Backlight off and DAC muted, held through sleep
     * (both are RTC-capable pins on the ESP32-S3).
     */
    gpio_set_level(LCD_BL, 0);
    gpio_hold_en(LCD_BL);
    gpio_hold_en(DAC_MUTE);

    esp_deep_sleep_start();
}


static void check_battery(void)
{
    int mv = battery_read_mv();

    if (mv <= 0) {
        return;
    }

    int old_percent = battery_percent(battery_mv);
    bool was_low = battery_mv > 0 && battery_mv < BATTERY_LOW_MV;

    battery_mv = mv;

    if (mv < BATTERY_CRITICAL_MV) {
        if (++critical_count >= CRITICAL_READINGS) {
            shut_down_for_battery();
        }
    } else {
        critical_count = 0;
    }

    /*
     * Warn once when it first drops low; re-arm after
     * charging back up a bit.
     */
    if (mv < BATTERY_LOW_MV && !battery_low_warned) {
        battery_low_warned = true;
        show_toast("Battery low");
    } else if (mv > BATTERY_LOW_MV + 100) {
        battery_low_warned = false;
    }

    bool is_low = mv < BATTERY_LOW_MV;

    if (battery_percent(mv) != old_percent || is_low != was_low) {
        draw_battery();
    }
}


/*
 * --------------------------------------------------
 * LISTS
 * --------------------------------------------------
 */

static void load_items(view_t *view)
{
    item_count = 0;

    switch (view->kind) {

        case VIEW_MENU:
            item_count = MENU_ITEM_COUNT;
            break;

        case VIEW_SONGS:
            item_count = library_track_count();
            for (int i = 0; i < item_count; i++) {
                items[i] = i;
            }
            break;

        case VIEW_ARTISTS:
            item_count = library_artist_count();
            for (int i = 0; i < item_count; i++) {
                items[i] = i;
            }
            break;

        case VIEW_ALBUMS:
            item_count = library_album_count();
            for (int i = 0; i < item_count; i++) {
                items[i] = i;
            }
            break;

        case VIEW_ARTIST_ALBUMS:
            item_count = library_albums_for_artist(
                view->param, items, LIBRARY_MAX_TRACKS);
            break;

        case VIEW_ALBUM_TRACKS:
            item_count = library_tracks_for_album(
                view->param, items, LIBRARY_MAX_TRACKS);
            break;

        default:
            break;
    }
}


static const char *view_title(const view_t *view)
{
    switch (view->kind) {
        case VIEW_MENU:          return "YELYAH";
        case VIEW_SONGS:         return "Songs";
        case VIEW_ARTISTS:       return "Artists";
        case VIEW_ALBUMS:        return "Albums";
        case VIEW_ARTIST_ALBUMS: return library_artist_name(view->param);
        case VIEW_ALBUM_TRACKS:  return library_album_name(view->param);
        case VIEW_PLAYLISTS:     return "Playlists";
        case VIEW_SETTINGS:      return "Settings";
        default:                 return "";
    }
}


static const char *item_label(const view_t *view, int index)
{
    switch (view->kind) {

        case VIEW_MENU:
            return menu_items[index];

        case VIEW_SONGS:
        case VIEW_ALBUM_TRACKS:
            return library_track(items[index])->title;

        case VIEW_ARTISTS:
            return library_artist_name(items[index]);

        case VIEW_ALBUMS:
        case VIEW_ARTIST_ALBUMS:
            return library_album_name(items[index]);

        default:
            return "";
    }
}


static void draw_row(int index)
{
    view_t *view = top();

    int row = index - view->scroll;

    if (row < 0 || row >= VISIBLE_ROWS) {
        return;
    }

    int y = LIST_TOP + row * ROW_HEIGHT;

    if (index >= item_count) {
        display_fill_rect(0, y, ROW_RIGHT, ROW_HEIGHT, COLOR_BLACK);
        return;
    }

    uint16_t bg = (index == view->selection) ? COLOR_BLUE : COLOR_BLACK;

    display_fill_rect(0, y, ROW_RIGHT, ROW_HEIGHT - 2, bg);

    draw_padded(MARGIN, y + 6, item_label(view, index), ROW_CHARS, COLOR_WHITE, bg);
}


static void draw_scrollbar(void)
{
    int x = DISPLAY_WIDTH - SCROLLBAR_W;
    int track_height = VISIBLE_ROWS * ROW_HEIGHT;

    if (item_count <= VISIBLE_ROWS) {
        display_fill_rect(x, LIST_TOP, SCROLLBAR_W, track_height, COLOR_BLACK);
        return;
    }

    int thumb_height = track_height * VISIBLE_ROWS / item_count;
    int thumb_y = LIST_TOP + track_height * top()->scroll / item_count;

    display_fill_rect(x, LIST_TOP, SCROLLBAR_W, track_height, COLOR_DARK);
    display_fill_rect(x, thumb_y, SCROLLBAR_W, thumb_height, COLOR_GREY);
}


static void draw_all_rows(void)
{
    for (int row = 0; row < VISIBLE_ROWS; row++) {
        draw_row(top()->scroll + row);
    }

    draw_scrollbar();
}


static void draw_empty_message(void)
{
    int y = LIST_TOP + 10;

    if (!storage_is_mounted()) {
        display_draw_text(MARGIN, y, "No SD card", COLOR_WHITE, COLOR_BLACK, TEXT_SCALE);
        display_draw_text(MARGIN, y + 30, "Insert a card and restart",
                          COLOR_GREY, COLOR_BLACK, 1);
        return;
    }

    display_draw_text(MARGIN, y, "No music found", COLOR_WHITE, COLOR_BLACK, TEXT_SCALE);
    display_draw_text(MARGIN, y + 30, "Add 16-bit WAV files to",
                      COLOR_GREY, COLOR_BLACK, 1);
    display_draw_text(MARGIN, y + 42, "/Music/Artist/Album/",
                      COLOR_GREY, COLOR_BLACK, 1);
}


static void render_list(void)
{
    display_clear(COLOR_BLACK);

    draw_header(view_title(top()));

    if (item_count == 0) {
        draw_empty_message();
        return;
    }

    draw_all_rows();
}


static void move_selection(int delta)
{
    view_t *view = top();

    int old_selection = view->selection;
    int old_scroll = view->scroll;

    int selection = view->selection + delta;

    if (selection < 0) {
        selection = 0;
    }

    if (selection > item_count - 1) {
        selection = item_count - 1;
    }

    if (selection == old_selection || item_count == 0) {
        return;
    }

    view->selection = selection;

    if (selection < view->scroll) {
        view->scroll = selection;
    } else if (selection >= view->scroll + VISIBLE_ROWS) {
        view->scroll = selection - VISIBLE_ROWS + 1;
    }

    if (view->scroll != old_scroll) {
        draw_all_rows();
    } else {
        draw_row(old_selection);
        draw_row(selection);
    }
}


/*
 * --------------------------------------------------
 * NOW PLAYING
 * --------------------------------------------------
 */

static void draw_progress(const player_status_t *status)
{
    int bar_width = DISPLAY_WIDTH - 2 * MARGIN;
    int filled = 0;

    if (status->duration_ms > 0) {
        filled = (int)((uint64_t)bar_width * status->position_ms / status->duration_ms);
    }

    if (filled > bar_width) {
        filled = bar_width;
    }

    display_fill_rect(MARGIN, NP_BAR_Y, filled, NP_BAR_H, COLOR_GREEN);
    display_fill_rect(MARGIN + filled, NP_BAR_Y, bar_width - filled, NP_BAR_H, COLOR_DARK);

    char elapsed[12];
    char total[12];

    format_time(status->position_ms, elapsed, sizeof(elapsed));
    format_time(status->duration_ms, total, sizeof(total));

    draw_padded(MARGIN, NP_TIME_Y, elapsed, 5, COLOR_WHITE, COLOR_BLACK);
    draw_right_aligned(DISPLAY_WIDTH - MARGIN, NP_TIME_Y, total, 5, COLOR_WHITE);

    drawn_second = status->position_ms / 1000;
}


static void draw_now_playing_fields(const player_status_t *status)
{
    const library_track_t *track = library_track(status->track);

    /*
     * Top line: state and position in the queue.
     */
    const char *state = "STOPPED";
    uint16_t state_color = COLOR_GREY;

    if (track != NULL) {
        state = status->playing ? "PLAYING" : "PAUSED";
        state_color = status->playing ? COLOR_GREEN : COLOR_WHITE;
    }

    draw_padded(MARGIN, HEADER_TEXT_Y, state, 7, state_color, COLOR_BLACK);

    char position[16] = "";

    if (track != NULL) {
        snprintf(position, sizeof(position), "%d/%d",
                 status->queue_position + 1, status->queue_length);
    }

    draw_right_aligned(DISPLAY_WIDTH - MARGIN, HEADER_TEXT_Y, position, 7, COLOR_GREY);

    /*
     * Song info.
     */
    if (track != NULL) {
        draw_padded(MARGIN, NP_TITLE_Y, track->title, LINE_CHARS, COLOR_WHITE, COLOR_BLACK);
        draw_padded(MARGIN, NP_ARTIST_Y, library_artist_name(track->artist),
                    LINE_CHARS, COLOR_GREY, COLOR_BLACK);
        draw_padded(MARGIN, NP_ALBUM_Y, library_album_name(track->album),
                    LINE_CHARS, COLOR_GREY, COLOR_BLACK);
    } else {
        draw_padded(MARGIN, NP_TITLE_Y, "Nothing playing", LINE_CHARS, COLOR_WHITE, COLOR_BLACK);
        draw_padded(MARGIN, NP_ARTIST_Y, "Pick a song from", LINE_CHARS, COLOR_GREY, COLOR_BLACK);
        draw_padded(MARGIN, NP_ALBUM_Y, "the Music menu", LINE_CHARS, COLOR_GREY, COLOR_BLACK);
    }

    draw_progress(status);

    /*
     * Bottom line: volume, shuffle, repeat.
     */
    char volume[12];

    snprintf(volume, sizeof(volume), "VOL %d", status->volume);

    draw_padded(MARGIN, NP_STATUS_Y, volume, 6, COLOR_WHITE, COLOR_BLACK);

    display_draw_text(
        MARGIN + 8 * CHAR_W,
        NP_STATUS_Y,
        "SHUF",
        status->shuffle ? COLOR_GREEN : COLOR_DARK,
        COLOR_BLACK,
        TEXT_SCALE
    );

    static const char *repeat_labels[] = {"R:OFF", "R:ALL", "R:ONE"};

    draw_right_aligned(
        DISPLAY_WIDTH - MARGIN,
        NP_STATUS_Y,
        repeat_labels[status->repeat],
        5,
        status->repeat == REPEAT_OFF ? COLOR_DARK : COLOR_GREEN
    );

    drawn_version = status->version;
}


static void render_now_playing(void)
{
    player_status_t status;

    player_get_status(&status);

    display_clear(COLOR_BLACK);

    display_fill_rect(0, HEADER_HEIGHT - 2, DISPLAY_WIDTH, 1, COLOR_GREY);

    draw_now_playing_fields(&status);
}


/*
 * Redraws only what changed since the last draw.
 */
static void refresh_now_playing(void)
{
    if (top()->kind != VIEW_NOW_PLAYING || toast_active) {
        return;
    }

    player_status_t status;

    player_get_status(&status);

    if (status.version != drawn_version) {
        draw_now_playing_fields(&status);
    } else if (status.position_ms / 1000 != drawn_second) {
        draw_progress(&status);
    }
}


/*
 * --------------------------------------------------
 * INFO SCREENS
 * --------------------------------------------------
 */

static void render_info(void)
{
    display_clear(COLOR_BLACK);

    draw_header(view_title(top()));

    int y = LIST_TOP + 10;

    if (top()->kind == VIEW_PLAYLISTS) {
        display_draw_text(MARGIN, y, "Coming soon", COLOR_WHITE, COLOR_BLACK, TEXT_SCALE);
        return;
    }

    char line[32];

    snprintf(line, sizeof(line), "SD card: %s", storage_is_mounted() ? "OK" : "none");
    display_draw_text(MARGIN, y, line, COLOR_WHITE, COLOR_BLACK, TEXT_SCALE);

    snprintf(line, sizeof(line), "Songs:   %d", library_track_count());
    display_draw_text(MARGIN, y + ROW_HEIGHT, line, COLOR_WHITE, COLOR_BLACK, TEXT_SCALE);

    snprintf(line, sizeof(line), "Artists: %d", library_artist_count());
    display_draw_text(MARGIN, y + 2 * ROW_HEIGHT, line, COLOR_WHITE, COLOR_BLACK, TEXT_SCALE);

    snprintf(line, sizeof(line), "Albums:  %d", library_album_count());
    display_draw_text(MARGIN, y + 3 * ROW_HEIGHT, line, COLOR_WHITE, COLOR_BLACK, TEXT_SCALE);

#ifdef WOKWI
    const char *build = "Build:   Wokwi";
#else
    const char *build = "Build:   Hardware";
#endif

    if (battery_mv > 0) {
        snprintf(line, sizeof(line), "Battery: %d.%02dV",
                 battery_mv / 1000, (battery_mv % 1000) / 10);
    } else {
        snprintf(line, sizeof(line), "Battery: n/a");
    }

    display_draw_text(MARGIN, y + 4 * ROW_HEIGHT, line, COLOR_WHITE, COLOR_BLACK, TEXT_SCALE);

    display_draw_text(MARGIN, y + 5 * ROW_HEIGHT, build, COLOR_GREY, COLOR_BLACK, TEXT_SCALE);
}


/*
 * --------------------------------------------------
 * NAVIGATION
 * --------------------------------------------------
 */

static void push_view(view_kind_t kind, int param)
{
    if (depth >= MAX_DEPTH) {
        return;
    }

    stack[depth++] = (view_t){
        .kind = kind,
        .param = param,
        .selection = 0,
        .scroll = 0
    };

    load_items(top());
    ui_render();
}


static void pop_view(void)
{
    if (depth <= 1) {
        return;
    }

    depth--;

    load_items(top());
    ui_render();
}


static void select_item(void)
{
    view_t *view = top();

    if (item_count == 0) {
        return;
    }

    int selected = view->selection;

    switch (view->kind) {

        case VIEW_MENU:
            ESP_LOGI(TAG, "Selected: %s", menu_items[selected]);

            switch (selected) {
                case 0: push_view(VIEW_SONGS, 0); break;
                case 1: push_view(VIEW_ARTISTS, 0); break;
                case 2: push_view(VIEW_ALBUMS, 0); break;
                case 3: push_view(VIEW_PLAYLISTS, 0); break;
                case 4: push_view(VIEW_SETTINGS, 0); break;
            }
            break;

        case VIEW_ARTISTS:
            push_view(VIEW_ARTIST_ALBUMS, items[selected]);
            break;

        case VIEW_ALBUMS:
        case VIEW_ARTIST_ALBUMS:
            push_view(VIEW_ALBUM_TRACKS, items[selected]);
            break;

        case VIEW_SONGS:
        case VIEW_ALBUM_TRACKS:
            /*
             * Play this list starting from the selected song.
             */
            player_play_queue(items, item_count, selected);
            push_view(VIEW_NOW_PLAYING, 0);
            break;

        default:
            break;
    }
}


/*
 * --------------------------------------------------
 * BUTTON HANDLING
 * --------------------------------------------------
 */

/*
 * Long presses work the same on every screen.
 * Returns true if the event was handled.
 */
static bool handle_global_event(button_event_t event)
{
    bool on_now_playing = top()->kind == VIEW_NOW_PLAYING;
    player_status_t status;

    switch (event) {

        case BUTTON_UP_LONG:
            player_toggle_shuffle();
            player_get_status(&status);
            if (!on_now_playing) {
                show_toast(status.shuffle ? "Shuffle on" : "Shuffle off");
            }
            return true;

        case BUTTON_DOWN_LONG: {
            static const char *labels[] = {"Repeat off", "Repeat all", "Repeat one"};
            player_cycle_repeat();
            player_get_status(&status);
            if (!on_now_playing) {
                show_toast(labels[status.repeat]);
            }
            return true;
        }

        case BUTTON_LEFT_LONG:
        case BUTTON_RIGHT_LONG:
            player_seek(event == BUTTON_LEFT_LONG ? -10 : 10);
            if (!on_now_playing) {
                player_get_status(&status);
                if (status.track < 0) {
                    show_toast("Nothing playing");
                } else {
                    show_toast(event == BUTTON_LEFT_LONG ? "Rewind 10s" : "Forward 10s");
                }
            }
            return true;

        case BUTTON_CENTER_LONG:
            ESP_LOGI(TAG, "Power / lock action");
            show_toast("Lock: not yet");
            return true;

        default:
            return false;
    }
}


static void handle_list_event(button_event_t event)
{
    switch (event) {

        case BUTTON_UP:
            move_selection(-1);
            break;

        case BUTTON_DOWN:
            move_selection(1);
            break;

        case BUTTON_CENTER:
            select_item();
            break;

        case BUTTON_RIGHT:
            push_view(VIEW_NOW_PLAYING, 0);
            break;

        case BUTTON_LEFT:
            pop_view();
            break;

        default:
            break;
    }
}


static void handle_now_playing_event(button_event_t event)
{
    switch (event) {

        case BUTTON_UP:
            player_change_volume(1);
            break;

        case BUTTON_DOWN:
            player_change_volume(-1);
            break;

        case BUTTON_CENTER:
            player_toggle_pause();
            break;

        case BUTTON_CENTER_DOUBLE:
            pop_view();
            return;

        case BUTTON_LEFT:
            player_previous();
            break;

        case BUTTON_RIGHT:
            player_next();
            break;

        default:
            break;
    }

    refresh_now_playing();
}


static void handle_info_event(button_event_t event)
{
    switch (event) {

        case BUTTON_LEFT:
        case BUTTON_CENTER_DOUBLE:
            pop_view();
            break;

        case BUTTON_RIGHT:
            push_view(VIEW_NOW_PLAYING, 0);
            break;

        default:
            break;
    }
}


/*
 * --------------------------------------------------
 * PUBLIC UI FUNCTIONS
 * --------------------------------------------------
 */

void ui_init(void)
{
    battery_mv = battery_read_mv();

    depth = 0;
    push_view(VIEW_MENU, 0);

    ESP_LOGI(TAG, "UI initialized");
}


void ui_render(void)
{
    toast_active = false;

    view_kind_t kind = top()->kind;

    if (is_list(kind)) {
        render_list();
    } else if (kind == VIEW_NOW_PLAYING) {
        render_now_playing();
    } else {
        render_info();
    }

    draw_battery();
}


void ui_handle_event(button_event_t event)
{
    if (event == BUTTON_NONE) {
        return;
    }

    /*
     * Any button press dismisses a pop-up message first.
     */
    if (toast_active) {
        ui_render();
    }

    if (handle_global_event(event)) {
        refresh_now_playing();
        return;
    }

    view_kind_t kind = top()->kind;

    if (is_list(kind)) {
        handle_list_event(event);
    } else if (kind == VIEW_NOW_PLAYING) {
        handle_now_playing_event(event);
    } else {
        handle_info_event(event);
    }
}


void ui_tick(void)
{
    int64_t now = esp_timer_get_time();

    if (toast_active && now >= toast_until) {
        ui_render();
    }

    if (now - last_poll >= STATUS_POLL_US) {
        last_poll = now;
        refresh_now_playing();
    }

    if (now - last_battery_poll >= BATTERY_POLL_US) {
        last_battery_poll = now;
        check_battery();
    }
}
