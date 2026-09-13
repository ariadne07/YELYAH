#include "buttons.h"
#include "pins.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "BUTTONS";

/* Timing configuration */
#define LONG_PRESS_TIME_US       800000    // 800 ms
#define DOUBLE_CLICK_TIME_US     400000    // 400 ms

typedef struct {
    bool pressed;
    bool long_sent;
    int64_t press_time;
} button_state_t;

static button_state_t up_state = {0};
static button_state_t down_state = {0};
static button_state_t left_state = {0};
static button_state_t right_state = {0};
static button_state_t center_state = {0};

static bool center_waiting_for_second_click = false;
static int64_t center_first_click_time = 0;

static button_event_t last_event = BUTTON_NONE;


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


static button_event_t process_button(
    int level,
    button_state_t *state,
    button_event_t click_event,
    button_event_t long_event
)
{
    int64_t now = esp_timer_get_time();

    /*
     * Button is pressed.
     */
    if (level == 0) {

        if (!state->pressed) {
            state->pressed = true;
            state->long_sent = false;
            state->press_time = now;
        }

        /*
         * Check for long press.
         */
        if (!state->long_sent &&
            (now - state->press_time) >= LONG_PRESS_TIME_US) {

            state->long_sent = true;
            return long_event;
        }
    }

    /*
     * Button was released.
     */
    else {

        if (state->pressed) {
            state->pressed = false;

            /*
             * If long press already happened,
             * don't also generate a click.
             */
            if (!state->long_sent) {
                return click_event;
            }
        }
    }

    return BUTTON_NONE;
}


button_event_t buttons_get_event(void)
{
    int64_t now = esp_timer_get_time();

    /*
     * Check center first because it has
     * special double-click behavior.
     */

    int center_level = gpio_get_level(PIN_BUTTON_CENTER);

    if (center_level == 0) {

        if (!center_state.pressed) {
            center_state.pressed = true;
            center_state.long_sent = false;
            center_state.press_time = now;
        }

        /*
         * Center long press.
         */
        if (!center_state.long_sent &&
            (now - center_state.press_time) >= LONG_PRESS_TIME_US) {

            center_state.long_sent = true;

            center_waiting_for_second_click = false;

            return BUTTON_CENTER_LONG;
        }
    }
    else {

        if (center_state.pressed) {

            center_state.pressed = false;

            /*
             * If it was a long press, don't treat
             * the release as a click.
             */
            if (!center_state.long_sent) {

                /*
                 * First click.
                 */
                if (!center_waiting_for_second_click) {

                    center_waiting_for_second_click = true;
                    center_first_click_time = now;

                }

                /*
                 * Second click.
                 */
                else {

                    if ((now - center_first_click_time)
                        <= DOUBLE_CLICK_TIME_US) {

                        center_waiting_for_second_click = false;

                        return BUTTON_CENTER_DOUBLE;
                    }

                    /*
                     * Too much time passed.
                     */
                    else {
                        center_first_click_time = now;
                    }
                }
            }
        }
    }

    /*
     * If we're waiting for a possible second
     * center click and the timeout expires,
     * generate the single click.
     */
    if (center_waiting_for_second_click &&
        (now - center_first_click_time) > DOUBLE_CLICK_TIME_US) {

        center_waiting_for_second_click = false;

        return BUTTON_CENTER;
    }


    /*
     * Other buttons.
     */

    button_event_t event;

    event = process_button(
        gpio_get_level(PIN_BUTTON_UP),
        &up_state,
        BUTTON_UP,
        BUTTON_UP_LONG
    );

    if (event != BUTTON_NONE) {
        return event;
    }


    event = process_button(
        gpio_get_level(PIN_BUTTON_DOWN),
        &down_state,
        BUTTON_DOWN,
        BUTTON_DOWN_LONG
    );

    if (event != BUTTON_NONE) {
        return event;
    }


    event = process_button(
        gpio_get_level(PIN_BUTTON_LEFT),
        &left_state,
        BUTTON_LEFT,
        BUTTON_LEFT_LONG
    );

    if (event != BUTTON_NONE) {
        return event;
    }


    event = process_button(
        gpio_get_level(PIN_BUTTON_RIGHT),
        &right_state,
        BUTTON_RIGHT,
        BUTTON_RIGHT_LONG
    );

    if (event != BUTTON_NONE) {
        return event;
    }


    return BUTTON_NONE;
}