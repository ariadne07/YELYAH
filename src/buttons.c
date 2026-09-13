#include "buttons.h"
#include "pins.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "BUTTONS";

void buttons_init(void)
{
    const uint64_t button_mask =
        (1ULL << PIN_BUTTON_UP) |
        (1ULL << PIN_BUTTON_CENTER) |
        (1ULL << PIN_BUTTON_DOWN) |
        (1ULL << PIN_BUTTON_LEFT) |
        (1ULL << PIN_BUTTON_RIGHT);

    gpio_config_t config = {
        .pin_bit_mask = button_mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    gpio_config(&config);

    ESP_LOGI(TAG, "Buttons initialized");
}

button_event_t buttons_get_event(void)
{
    if (gpio_get_level(PIN_BUTTON_UP) == 0) {
        return BUTTON_UP;
    }

    if (gpio_get_level(PIN_BUTTON_CENTER) == 0) {
        return BUTTON_CENTER;
    }

    if (gpio_get_level(PIN_BUTTON_DOWN) == 0) {
        return BUTTON_DOWN;
    }

    if (gpio_get_level(PIN_BUTTON_LEFT) == 0) {
        return BUTTON_LEFT;
    }

    if (gpio_get_level(PIN_BUTTON_RIGHT) == 0) {
        return BUTTON_RIGHT;
    }

    return BUTTON_NONE;
}