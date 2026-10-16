#include "storage.h"
#include "pins.h"

#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "STORAGE";

static sdmmc_card_t *card = NULL;


bool storage_init(void)
{
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024
    };

    /*
     * The SPI bus itself is owned by the display driver.
     * Here we only add the SD card as a second device on it.
     */
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = SD_CS;
    slot_config.host_id = SPI2_HOST;

    esp_err_t ret = esp_vfs_fat_sdspi_mount(
        STORAGE_MOUNT_POINT,
        &host,
        &slot_config,
        &mount_config,
        &card
    );

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD card mount failed: %s", esp_err_to_name(ret));
        card = NULL;
        return false;
    }

    ESP_LOGI(
        TAG,
        "SD card mounted (%llu MB)",
        ((unsigned long long)card->csd.capacity) * card->csd.sector_size / (1024 * 1024)
    );

    return true;
}


bool storage_is_mounted(void)
{
    return card != NULL;
}
