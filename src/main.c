#include "sdcard.h"
#include "music.h"

#include "esp_log.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(
        TAG,
        "Starting YELYAH"
    );


    sdcard_init_detect();


    if (!sdcard_is_inserted()) {

        ESP_LOGW(
            TAG,
            "No SD card detected"
        );

        return;
    }


    if (!sdcard_init()) {

        ESP_LOGE(
            TAG,
            "SD card initialization failed"
        );

        return;
    }


    ESP_LOGI(
        TAG,
        "SD card OK"
    );


    if (!music_scan()) {

        ESP_LOGE(
            TAG,
            "Music scan failed"
        );

        return;
    }


    size_t count =
        music_get_track_count();


    ESP_LOGI(
        TAG,
        "Library contains %u track(s)",
        (unsigned)count
    );


    for (
        size_t i = 0;
        i < count;
        i++
    ) {

        const music_track_t *track =
            music_get_track(i);


        if (track == NULL) {
            continue;
        }


        ESP_LOGI(
            TAG,
            "TRACK %u",
            (unsigned)i
        );

        ESP_LOGI(
            TAG,
            "  Title : %s",
            track->title
        );

        ESP_LOGI(
            TAG,
            "  Artist: %s",
            track->artist
        );

        ESP_LOGI(
            TAG,
            "  Album : %s",
            track->album
        );

        ESP_LOGI(
            TAG,
            "  File  : %s",
            track->path
        );
    }


    ESP_LOGI(
        TAG,
        "Music metadata test complete"
    );
}