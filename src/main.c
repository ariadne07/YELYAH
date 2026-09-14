#include "display.h"
#include "sdcard.h"

#include "esp_log.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting YELYAH");

    /*
     * This initializes SPI2_HOST.
     */
    display_init();

    /*
     * SD uses the same SPI bus.
     */
    if (sdcard_init()) {
        ESP_LOGI(TAG, "SD card OK");

        sdcard_test();
    } else {
        ESP_LOGE(TAG, "SD card initialization failed");
    }

    while (1) {
    }
}