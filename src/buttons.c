#include "buttons.h"
#include "pins.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "BUTTONS";

/* Timing configuration */
#define DEBOUNCE_TIME_US      50000     // 50 ms
#define LONG_PRESS_TIME_US    700000    // 700 ms
#define DOUBLE_CLICK_TIME_US  400000    // 400 ms

typedef struct {
    bool raw_state;
    bool stable_state;

    bool pressed;
    bool long_sent;

    int64_t state_change_time;
    int64_t press_time;
} button_state_t;

static button_state_t up_state = {0};
static button_state_t down_state = {0};
static button_state_t left_state = {0};
static button_state_t right_state = {0};
static button_state_t center_state = {0};

static bool center_waiting_for_second_click = false;
static int64_t center_first_click_time = 0;


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

    /*
     * Initialize button states based on their
     * current physical state.
     */
    int64_t now = esp_timer_get_time();

    up_state.raw_state =
        up_state.stable_state =
        (gpio_get_level(PIN_BUTTON_UP) == 0);
    up_state.state_change_time = now;

    down_state.raw_state =
        down_state.stable_state =
        (gpio_get_level(PIN_BUTTON_DOWN) == 0);
    down_state.state_change_time = now;

    left_state.raw_state =
        left_state.stable_state =
        (gpio_get_level(PIN_BUTTON_LEFT) == 0);
    left_state.state_change_time = now;

    right_state.raw_state =
        right_state.stable_state =
        (gpio_get_level(PIN_BUTTON_RIGHT) == 0);
    right_state.state_change_time = now;

    center_state.raw_state =
        center_state.stable_state =
        (gpio_get_level(PIN_BUTTON_CENTER) == 0);
    center_state.state_change_time = now;

    ESP_LOGI(TAG, "Buttons initialized");
}


/*
 * Debounce a single button.
 *
 * Returns:
 *   true  = button is stably pressed
 *   false = button is stably released
 */
static bool debounce_button(
    int gpio_level,
    button_state_t *state
)
{
    int64_t now = esp_timer_get_time();

    bool raw_pressed = (gpio_level == 0);

    /*
     * Raw signal changed.
     * Start/restart debounce timer.
     */
    if (raw_pressed != state->raw_state) {
        state->raw_state = raw_pressed;
        state->state_change_time = now;
    }

    /*
     * Raw signal has stayed unchanged long enough
     * to accept the new stable state.
     */
    if (state->stable_state != state->raw_state) {

        if ((now - state->state_change_time) >= DEBOUNCE_TIME_US) {
            state->stable_state = state->raw_state;
        }
    }

    return state->stable_state;
}


/*
 * Process a debounced button.
 *
 * Generates either:
 *   - a normal button event on release
 *   - a long-press event after LONG_PRESS_TIME_US
 */
static button_event_t process_button(
    bool pressed,
    button_state_t *state,
    button_event_t click_event,
    button_event_t long_event
)
{
    int64_t now = esp_timer_get_time();

    /*
     * Button is pressed.
     */
    if (pressed) {

        /*
         * This is the beginning of a new press.
         */
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
     * Button is released.
     */
    else {

        if (state->pressed) {

            state->pressed = false;

            /*
             * If a long press has already been sent,
             * don't also send a normal click.
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
     * ------------------------------------------------
     * CENTER BUTTON
     * ------------------------------------------------
     *
     * Center has:
     *   - normal click
     *   - double click
     *   - long press
     */

    bool center_pressed = debounce_button(
        gpio_get_level(PIN_BUTTON_CENTER),
        &center_state
    );

    /*
     * Center is pressed.
     */
    if (center_pressed) {

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

    /*
     * Center is released.
     */
    else {

        if (center_state.pressed) {

            center_state.pressed = false;

            /*
             * Don't treat a long press as a click.
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
                     * Second click came too late.
                     * Start a new single-click window.
                     */
                    center_first_click_time = now;
                }
            }
        }
    }


    /*
     * If the center button has been waiting for
     * a second click and the timeout expires,
     * report the first click as a normal CENTER event.
     */
    if (center_waiting_for_second_click &&
        (now - center_first_click_time) > DOUBLE_CLICK_TIME_US) {

        center_waiting_for_second_click = false;

        return BUTTON_CENTER;
    }


    /*
     * ------------------------------------------------
     * OTHER BUTTONS
     * ------------------------------------------------
     */

    button_event_t event;


    event = process_button(
        debounce_button(
            gpio_get_level(PIN_BUTTON_UP),
            &up_state
        ),
        &up_state,
        BUTTON_UP,
        BUTTON_UP_LONG
    );

    if (event != BUTTON_NONE) {
        return event;
    }


    event = process_button(
        debounce_button(
            gpio_get_level(PIN_BUTTON_DOWN),
            &down_state
        ),
        &down_state,
        BUTTON_DOWN,
        BUTTON_DOWN_LONG
    );

    if (event != BUTTON_NONE) {
        return event;
    }


    event = process_button(
        debounce_button(
            gpio_get_level(PIN_BUTTON_LEFT),
            &left_state
        ),
        &left_state,
        BUTTON_LEFT,
        BUTTON_LEFT_LONG
    );

    if (event != BUTTON_NONE) {
        return event;
    }


    event = process_button(
        debounce_button(
            gpio_get_level(PIN_BUTTON_RIGHT),
            &right_state
        ),
        &right_state,
        BUTTON_RIGHT,
        BUTTON_RIGHT_LONG
    );

    if (event != BUTTON_NONE) {
        return event;
    }


    return BUTTON_NONE;
}