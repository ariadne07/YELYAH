#include "sdcard.h"

#include "esp_log.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting YELYAH");

    sdcard_init_detect();

    if (!sdcard_is_inserted()) {
        ESP_LOGW(TAG, "No SD card detected");
        return;
    }

    ESP_LOGI(TAG, "SD card detected");

    if (sdcard_init()) {
        ESP_LOGI(TAG, "SD card OK");
        sdcard_test();
    } else {
        ESP_LOGE(TAG, "SD card initialization failed");
    }

    ESP_LOGI(TAG, "SD test complete");
}