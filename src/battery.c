#include "battery.h"
#include "pins.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "BATTERY";

#define SAMPLES 8

static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_cali_handle_t cali_handle = NULL;
static adc_channel_t channel;


void battery_init(void)
{
    adc_unit_t unit;

    if (adc_oneshot_io_to_channel(BATTERY_SENSE, &unit, &channel) != ESP_OK) {
        ESP_LOGE(TAG, "GPIO%d is not an ADC pin", BATTERY_SENSE);
        return;
    }

    adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = unit,
    };

    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_config, &adc_handle));

    /*
     * 12 dB attenuation reads up to ~3.1 V, which covers
     * the divider's maximum of 4.2 V / 2 = 2.1 V.
     */
    adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };

    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, channel, &channel_config));

#ifndef WOKWI
    /*
     * Real chips store per-unit calibration in eFuse;
     * use it for accurate millivolts.
     */
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = unit,
        .chan = channel,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };

    if (adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle) != ESP_OK) {
        ESP_LOGW(TAG, "No ADC calibration, readings will be approximate");
        cali_handle = NULL;
    }
#endif

    ESP_LOGI(TAG, "Battery monitor on GPIO%d: %d mV", BATTERY_SENSE, battery_read_mv());
}


int battery_read_mv(void)
{
    if (adc_handle == NULL) {
        return 0;
    }

    int total = 0;

    for (int i = 0; i < SAMPLES; i++) {

        int raw = 0;

        if (adc_oneshot_read(adc_handle, channel, &raw) != ESP_OK) {
            return 0;
        }

        total += raw;
    }

    int raw = total / SAMPLES;
    int pin_mv;

    if (cali_handle == NULL ||
        adc_cali_raw_to_voltage(cali_handle, raw, &pin_mv) != ESP_OK) {
        /*
         * Uncalibrated (and Wokwi, whose ADC maps
         * 0-3.3 V linearly onto 0-4095).
         */
        pin_mv = raw * 3300 / 4095;
    }

    return pin_mv * BATTERY_DIVIDER_RATIO;
}


int battery_percent(int millivolts)
{
    /*
     * Typical LiPo voltage vs. remaining charge while
     * lightly loaded.
     */
    static const struct {
        int mv;
        int percent;
    } curve[] = {
        {4200, 100},
        {4100, 90},
        {4000, 78},
        {3900, 63},
        {3800, 48},
        {3700, 30},
        {3600, 15},
        {3500, 7},
        {3400, 3},
        {3200, 0},
    };

    const int points = sizeof(curve) / sizeof(curve[0]);

    if (millivolts >= curve[0].mv) {
        return 100;
    }

    for (int i = 1; i < points; i++) {

        if (millivolts >= curve[i].mv) {

            int mv_span = curve[i - 1].mv - curve[i].mv;
            int percent_span = curve[i - 1].percent - curve[i].percent;

            return curve[i].percent +
                   (millivolts - curve[i].mv) * percent_span / mv_span;
        }
    }

    return 0;
}
