#include "battery.h"
#include "buttons.h"
#include "display.h"
#include "library.h"
#include "player.h"
#include "storage.h"
#include "ui.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting YELYAH");

    buttons_init();

    /*
     * The display sets up the SPI bus, which the SD
     * card shares, so it must come first.
     */
    display_init();

    if (storage_init()) {
        library_scan();
    }

    battery_init();
    player_init();
    ui_init();

    while (1) {

        button_event_t event =
            buttons_get_event();

        if (event != BUTTON_NONE) {
            ui_handle_event(event);
        }

        ui_tick();

        /*
         * Poll buttons every 10 ms.
         */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
