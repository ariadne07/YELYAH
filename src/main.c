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
                ESP_LOGI(TAG, "UP CLICK");
                break;

            case BUTTON_DOWN:
                ESP_LOGI(TAG, "DOWN CLICK");
                break;

            case BUTTON_LEFT:
                ESP_LOGI(TAG, "LEFT CLICK");
                break;

            case BUTTON_RIGHT:
                ESP_LOGI(TAG, "RIGHT CLICK");
                break;

            case BUTTON_CENTER:
                ESP_LOGI(TAG, "CENTER CLICK");
                break;

            case BUTTON_UP_LONG:
                ESP_LOGI(TAG, "UP LONG");
                break;

            case BUTTON_DOWN_LONG:
                ESP_LOGI(TAG, "DOWN LONG");
                break;

            case BUTTON_LEFT_LONG:
                ESP_LOGI(TAG, "LEFT LONG");
                break;

            case BUTTON_RIGHT_LONG:
                ESP_LOGI(TAG, "RIGHT LONG");
                break;

            case BUTTON_CENTER_LONG:
                ESP_LOGI(TAG, "CENTER LONG");
                break;

            case BUTTON_CENTER_DOUBLE:
                ESP_LOGI(TAG, "CENTER DOUBLE CLICK");
                break;

            case BUTTON_NONE:
                break;
        }

        esp_rom_delay_us(10000);
    }
}