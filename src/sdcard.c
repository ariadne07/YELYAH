#include "sdcard.h"
#include "pins.h"

#include "driver/sdspi_host.h"
#include "esp_vfs_fat.h"
#include "esp_log.h"
#include "sdmmc_cmd.h"

#include <dirent.h>
#include <stdio.h>

static const char *TAG = "SDCARD";

#define SD_MOUNT_POINT "/sdcard"

static sdmmc_card_t *sd_card = NULL;
static bool mounted = false;


bool sdcard_init(void)
{
    ESP_LOGI(TAG, "Initializing SD card");


    /*
     * Configure the SD card as an SPI device.
     *
     * SPI2_HOST must already have been initialized,
     * which is currently done by display_init().
     */
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    sdspi_device_config_t slot_config =
        SDSPI_DEVICE_CONFIG_DEFAULT();

    slot_config.host_id = SPI2_HOST;
    slot_config.gpio_cs = SD_CS;
    slot_config.gpio_cd = SDSPI_SLOT_NO_CD;
    slot_config.gpio_wp = SDSPI_SLOT_NO_WP;


    /*
     * Mount the FAT filesystem.
     *
     * This convenience function attaches the SD card
     * to the already-initialized SPI bus, initializes
     * the card, and mounts the FAT filesystem.
     */
    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 0
    };

    esp_err_t ret = esp_vfs_fat_sdspi_mount(
        SD_MOUNT_POINT,
        &host,
        &slot_config,
        &mount_config,
        &sd_card
    );

    if (ret != ESP_OK) {

        ESP_LOGE(
            TAG,
            "Failed to mount SD card: %s",
            esp_err_to_name(ret)
        );

        return false;
    }


    mounted = true;

    ESP_LOGI(
        TAG,
        "SD card mounted at %s",
        SD_MOUNT_POINT
    );

    sdmmc_card_print_info(stdout, sd_card);

    return true;
}


bool sdcard_is_mounted(void)
{
    return mounted;
}


void sdcard_test(void)
{
    if (!mounted) {
        ESP_LOGE(TAG, "SD card is not mounted");
        return;
    }

    ESP_LOGI(TAG, "Listing %s", SD_MOUNT_POINT);

    DIR *dir = opendir(SD_MOUNT_POINT);

    if (dir == NULL) {
        ESP_LOGE(
            TAG,
            "Could not open %s",
            SD_MOUNT_POINT
        );

        return;
    }

    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {

        if (entry->d_type == DT_DIR) {

            ESP_LOGI(
                TAG,
                "[DIR ] %s",
                entry->d_name
            );

        } else {

            ESP_LOGI(
                TAG,
                "[FILE] %s",
                entry->d_name
            );
        }
    }

    closedir(dir);

    ESP_LOGI(TAG, "SD card test complete");
}