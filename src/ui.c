#include "ui.h"

#include "esp_log.h"

static const char *TAG = "UI";

#define MENU_ITEM_COUNT 5

static ui_screen_t current_screen = UI_MENU;
static int menu_selection = 0;

static const char *menu_items[MENU_ITEM_COUNT] = {
    "Music",
    "Artists",
    "Albums",
    "Playlists",
    "Settings"
};


static void show_menu(void)
{
    ESP_LOGI(TAG, "----- MENU VIEW -----");

    for (int i = 0; i < MENU_ITEM_COUNT; i++) {

        if (i == menu_selection) {
            ESP_LOGI(TAG, "> %s", menu_items[i]);
        } else {
            ESP_LOGI(TAG, "  %s", menu_items[i]);
        }
    }

    ESP_LOGI(TAG, "Selection: %d", menu_selection);
}


static void show_now_playing(void)
{
    ESP_LOGI(TAG, "----- NOW PLAYING -----");
    ESP_LOGI(TAG, "Track: Example Song");
    ESP_LOGI(TAG, "Artist: Example Artist");
    ESP_LOGI(TAG, "Album: Example Album");
}


void ui_init(void)
{
    current_screen = UI_MENU;
    menu_selection = 0;

    show_menu();

    ESP_LOGI(TAG, "UI initialized");
}


void ui_handle_event(button_event_t event)
{
    switch (current_screen) {

        /*
         * ==========================================
         * MENU VIEW
         * ==========================================
         */

        case UI_MENU:

            switch (event) {

                case BUTTON_NONE:
                    break;


                case BUTTON_UP:

                    if (menu_selection > 0) {
                        menu_selection--;
                    }

                    show_menu();
                    break;


                case BUTTON_DOWN:

                    if (menu_selection < MENU_ITEM_COUNT - 1) {
                        menu_selection++;
                    }

                    show_menu();
                    break;


                case BUTTON_CENTER:

                    ESP_LOGI(
                        TAG,
                        "Selected: %s",
                        menu_items[menu_selection]
                    );

                    /*
                     * Temporary behavior:
                     * selecting an item enters Now Playing.
                     */
                    current_screen = UI_NOW_PLAYING;

                    show_now_playing();
                    break;


                case BUTTON_RIGHT:

                    /*
                     * Menu specification:
                     * RIGHT enters Now Playing.
                     */
                    current_screen = UI_NOW_PLAYING;

                    show_now_playing();
                    break;


                case BUTTON_LEFT:

                    /*
                     * For now, LEFT just reports back.
                     * Directory navigation will be added later.
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
                     * Center double-click isn't used
                     * in Menu View.
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

                    /*
                     * Double center-click returns
                     * to Menu View.
                     */
                    current_screen = UI_MENU;

                    show_menu();
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