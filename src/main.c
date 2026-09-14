#include "buttons.h"
#include "ui.h"
#include "display.h"

#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting YELYAH");

    buttons_init();

    display_init();

    display_test_pattern();

    ui_init();

    while (1) {

        button_event_t event = buttons_get_event();

        if (event != BUTTON_NONE) {
            ui_handle_event(event);
        }

        esp_rom_delay_us(10000);
    }
}