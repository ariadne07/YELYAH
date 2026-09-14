#include "ui.h"
#include "display.h"

#include "esp_log.h"

static const char *TAG = "UI";

#define MENU_ITEM_COUNT 5

#define COLOR_BLACK  0x0000
#define COLOR_WHITE  0xFFFF
#define COLOR_GREEN  0x07E0
#define COLOR_BLUE   0x001F
#define COLOR_RED    0xF800

static ui_screen_t current_screen = UI_MENU_VIEW;

static int menu_selection = 0;

static const char *menu_items[MENU_ITEM_COUNT] = {
    "Music",
    "Artists",
    "Albums",
    "Playlists",
    "Settings"
};


/*
 * --------------------------------------------------
 * MENU
 * --------------------------------------------------
 */

static void render_menu(void)
{
    display_clear(COLOR_BLACK);

    /*
     * Title
     */
    display_draw_text(
        10,
        10,
        "YELYAH",
        COLOR_WHITE,
        COLOR_BLACK,
        3
    );

    /*
     * Menu items
     */
    for (int i = 0; i < MENU_ITEM_COUNT; i++) {

        int y = 70 + (i * 35);

        /*
         * Highlight selected item.
         */
        if (i == menu_selection) {

            display_fill_rect(
                5,
                y - 3,
                230,
                25,
                COLOR_BLUE
            );

            display_draw_text(
                12,
                y,
                menu_items[i],
                COLOR_WHITE,
                COLOR_BLUE,
                2
            );

        } else {

            display_draw_text(
                12,
                y,
                menu_items[i],
                COLOR_WHITE,
                COLOR_BLACK,
                2
            );
        }
    }
}


/*
 * --------------------------------------------------
 * NOW PLAYING
 * --------------------------------------------------
 */

static void render_now_playing(void)
{
    display_clear(COLOR_BLACK);

    display_draw_text(
        10,
        10,
        "NOW PLAYING",
        COLOR_GREEN,
        COLOR_BLACK,
        2
    );

    display_draw_text(
        10,
        70,
        "EXAMPLE SONG",
        COLOR_WHITE,
        COLOR_BLACK,
        2
    );

    display_draw_text(
        10,
        105,
        "EXAMPLE ARTIST",
        COLOR_WHITE,
        COLOR_BLACK,
        2
    );

    display_draw_text(
        10,
        140,
        "EXAMPLE ALBUM",
        COLOR_WHITE,
        COLOR_BLACK,
        2
    );

    /*
     * Placeholder progress bar.
     */
    display_fill_rect(
        10,
        210,
        220,
        5,
        COLOR_WHITE
    );

    display_fill_rect(
        10,
        210,
        80,
        5,
        COLOR_GREEN
    );

    display_draw_text(
        10,
        230,
        "1:24",
        COLOR_WHITE,
        COLOR_BLACK,
        2
    );

    display_draw_text(
        175,
        230,
        "3:45",
        COLOR_WHITE,
        COLOR_BLACK,
        2
    );
}


/*
 * --------------------------------------------------
 * PUBLIC UI FUNCTIONS
 * --------------------------------------------------
 */

void ui_init(void)
{
    current_screen = UI_MENU_VIEW;
    menu_selection = 0;

    ui_render();

    ESP_LOGI(TAG, "UI initialized");
}


void ui_render(void)
{
    switch (current_screen) {

        case UI_MENU_VIEW:
            render_menu();
            break;

        case UI_NOW_PLAYING:
            render_now_playing();
            break;
    }
}


void ui_handle_event(button_event_t event)
{
    switch (current_screen) {

        /*
         * ==========================================
         * MENU VIEW
         * ==========================================
         */

        case UI_MENU_VIEW:

            switch (event) {

                case BUTTON_NONE:
                    break;

                case BUTTON_UP:

                    if (menu_selection > 0) {
                        menu_selection--;
                    }

                    ui_render();
                    break;


                case BUTTON_DOWN:

                    if (menu_selection < MENU_ITEM_COUNT - 1) {
                        menu_selection++;
                    }

                    ui_render();
                    break;


                case BUTTON_CENTER:

                    ESP_LOGI(
                        TAG,
                        "Selected: %s",
                        menu_items[menu_selection]
                    );

                    current_screen = UI_NOW_PLAYING;

                    ui_render();
                    break;


                case BUTTON_RIGHT:

                    current_screen = UI_NOW_PLAYING;

                    ui_render();
                    break;


                case BUTTON_LEFT:

                    /*
                     * No parent directory yet.
                     */
                    ESP_LOGI(TAG, "Back");
                    break;


                case BUTTON_UP_LONG:

                    ESP_LOGI(TAG, "Shuffle toggle");
                    break;


                case BUTTON_DOWN_LONG:

                    ESP_LOGI(TAG, "Repeat toggle");
                    break;


                case BUTTON_LEFT_LONG:

                    ESP_LOGI(TAG, "Rewind");
                    break;


                case BUTTON_RIGHT_LONG:

                    ESP_LOGI(TAG, "Fast forward");
                    break;


                case BUTTON_CENTER_LONG:

                    ESP_LOGI(TAG, "Power / lock action");
                    break;


                case BUTTON_CENTER_DOUBLE:

                    /*
                     * Not used from menu view.
                     */
                    break;
            }

            break;


        /*
         * ==========================================
         * NOW PLAYING
         * ==========================================
         */

        case UI_NOW_PLAYING:

            switch (event) {

                case BUTTON_NONE:
                    break;


                case BUTTON_UP:

                    ESP_LOGI(TAG, "Volume up");
                    break;


                case BUTTON_DOWN:

                    ESP_LOGI(TAG, "Volume down");
                    break;


                case BUTTON_CENTER:

                    ESP_LOGI(TAG, "Play / Pause");
                    break;


                case BUTTON_CENTER_DOUBLE:

                    current_screen = UI_MENU_VIEW;

                    ui_render();
                    break;


                case BUTTON_LEFT:

                    ESP_LOGI(TAG, "Previous track");
                    break;


                case BUTTON_RIGHT:

                    ESP_LOGI(TAG, "Next track");
                    break;


                case BUTTON_UP_LONG:

                    ESP_LOGI(TAG, "Shuffle toggle");
                    break;


                case BUTTON_DOWN_LONG:

                    ESP_LOGI(TAG, "Repeat toggle");
                    break;


                case BUTTON_LEFT_LONG:

                    ESP_LOGI(TAG, "Rewind");
                    break;


                case BUTTON_RIGHT_LONG:

                    ESP_LOGI(TAG, "Fast forward");
                    break;


                case BUTTON_CENTER_LONG:

                    ESP_LOGI(TAG, "Power / lock action");
                    break;
            }

            break;
    }
}


ui_screen_t ui_get_screen(void)
{
    return current_screen;
}