#include "buttons.h"

#include "esp_log.h"
#include "esp_rom_sys.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting YELYAH");

    buttons_init();

    while (1) {

        button_event_t event = buttons_get_event();

        switch (event) {

            case BUTTON_UP:
                ESP_LOGI(TAG, "UP");
                break;

            case BUTTON_CENTER:
                ESP_LOGI(TAG, "CENTER");
                break;

            case BUTTON_DOWN:
                ESP_LOGI(TAG, "DOWN");
                break;

            case BUTTON_LEFT:
                ESP_LOGI(TAG, "LEFT");
                break;

            case BUTTON_RIGHT:
                ESP_LOGI(TAG, "RIGHT");
                break;

            case BUTTON_NONE:
                break;
        }

        // Temporary 50 ms delay for button testing.
        esp_rom_delay_us(50000);
    }
}