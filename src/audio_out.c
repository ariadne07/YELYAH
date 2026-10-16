#include "audio_out.h"
#include "pins.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "AUDIO_OUT";

static uint32_t current_rate = 0;


/*
 * --------------------------------------------------
 * DAC MUTE (both builds)
 * --------------------------------------------------
 */

static void mute_init(void)
{
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << DAC_MUTE,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&config));

    audio_out_set_muted(true);
}


void audio_out_set_muted(bool muted)
{
    gpio_set_level(DAC_MUTE, muted ? 0 : 1);
}


#ifdef WOKWI

/*
 * --------------------------------------------------
 * WOKWI: simulated real-time output
 * --------------------------------------------------
 */

#include "esp_timer.h"

/*
 * Time (esp_timer microseconds) at which everything
 * written so far would have finished playing.
 */
static int64_t play_deadline_us = 0;


void audio_out_init(void)
{
    mute_init();

    ESP_LOGI(TAG, "Wokwi build: audio is timed but not output");
}


void audio_out_start(uint32_t sample_rate)
{
    current_rate = sample_rate;
    play_deadline_us = esp_timer_get_time();
}


void audio_out_write(const int16_t *frames, size_t frame_count)
{
    (void)frames;

    if (current_rate == 0) {
        return;
    }

    play_deadline_us += (int64_t)frame_count * 1000000 / current_rate;

    int64_t now = esp_timer_get_time();

    /*
     * If we fell far behind (e.g. the simulation was
     * paused), restart the clock instead of rushing.
     */
    if (now - play_deadline_us > 250000) {
        play_deadline_us = now;
        return;
    }

    int64_t wait_us = play_deadline_us - now;

    if (wait_us > 0) {
        TickType_t ticks = pdMS_TO_TICKS(wait_us / 1000);
        vTaskDelay(ticks > 0 ? ticks : 1);
    }
}


#else

/*
 * --------------------------------------------------
 * HARDWARE: I2S to PCM5102A
 * --------------------------------------------------
 */

#include "driver/i2s_std.h"

static i2s_chan_handle_t tx_channel = NULL;


void audio_out_init(void)
{
    mute_init();

    i2s_chan_config_t chan_config =
        I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);

    /*
     * Send silence instead of repeating old data if the
     * player can't keep up.
     */
    chan_config.auto_clear = true;

    ESP_ERROR_CHECK(i2s_new_channel(&chan_config, &tx_channel, NULL));

    current_rate = 44100;

    /*
     * The PCM5102A generates its own system clock from
     * BCK, so MCLK is not connected (tie the board's
     * SCK pin to GND).
     */
    i2s_std_config_t std_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(current_rate),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_STEREO
        ),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = DAC_BCK,
            .ws = DAC_WSEL,
            .dout = DAC_DIN,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_channel, &std_config));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_channel));

    ESP_LOGI(TAG, "I2S output initialized");
}


void audio_out_start(uint32_t sample_rate)
{
    if (sample_rate == current_rate) {
        return;
    }

    i2s_std_clk_config_t clk_config = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);

    ESP_ERROR_CHECK(i2s_channel_disable(tx_channel));
    ESP_ERROR_CHECK(i2s_channel_reconfig_std_clock(tx_channel, &clk_config));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_channel));

    current_rate = sample_rate;

    ESP_LOGI(TAG, "Sample rate set to %lu Hz", (unsigned long)sample_rate);
}


void audio_out_write(const int16_t *frames, size_t frame_count)
{
    size_t bytes_written = 0;

    i2s_channel_write(
        tx_channel,
        frames,
        frame_count * 2 * sizeof(int16_t),
        &bytes_written,
        portMAX_DELAY
    );
}

#endif
