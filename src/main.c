#include "battery.h"
#include "buttons.h"
#include "display.h"
#include "library.h"
#include "player.h"
#include "storage.h"
#include "ui.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>

static const char *TAG = "MAIN";

#ifndef WOKWI

/*
 * Development aids for the real board. Its serial port
 * is the chip's own USB, which only appears once the
 * chip is running, so a monitor always connects a moment
 * too late to see the first log lines.
 */
#define STARTUP_DELAY_MS 2000
#define HEARTBEAT_US     (10 * 1000 * 1000)

static void log_heartbeat(void)
{
    player_status_t status;
    char battery[16] = "n/a";

    player_get_status(&status);

    int mv = battery_read_mv();

    if (mv > 0) {
        snprintf(battery, sizeof(battery), "%d mV", mv);
    }

    const char *state = "stopped";

    if (status.track >= 0) {
        state = status.playing ? "playing" : "paused";
    }

    ESP_LOGI(
        TAG,
        "YELYAH running | SD: %s | tracks: %d | battery: %s | player: %s",
        storage_is_mounted() ? "OK" : "none",
        library_track_count(),
        battery,
        state
    );
}

#endif

void app_main(void)
{
#ifndef WOKWI
    /*
     * Give a serial monitor time to connect so the whole
     * startup log is visible.
     */
    vTaskDelay(pdMS_TO_TICKS(STARTUP_DELAY_MS));
#endif

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

#ifndef WOKWI
    int64_t last_heartbeat = esp_timer_get_time();

    log_heartbeat();
#endif

    while (1) {

        button_event_t event =
            buttons_get_event();

        if (event != BUTTON_NONE) {
            ui_handle_event(event);
        }

        ui_tick();

#ifndef WOKWI
        if (esp_timer_get_time() - last_heartbeat >= HEARTBEAT_US) {
            last_heartbeat = esp_timer_get_time();
            log_heartbeat();
        }
#endif

        /*
         * Poll buttons every 10 ms.
         */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
