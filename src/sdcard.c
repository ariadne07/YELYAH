#include "sdcard.h"
#include "pins.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/sdspi_host.h"

#include "esp_vfs_fat.h"
#include "esp_log.h"
#include "sdmmc_cmd.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "SDCARD";

#define SD_MOUNT_POINT "/sdcard"

static sdmmc_card_t *sd_card = NULL;
static bool mounted = false;
static bool spi_bus_initialized = false;

/* -------------------------------------------------------------------------- */
/* CARD DETECT                                                                */
/* -------------------------------------------------------------------------- */

void sdcard_init_detect(void)
{
gpio_config_t config = {
.pin_bit_mask = 1ULL << SD_DETECT,
.mode = GPIO_MODE_INPUT,
.pull_up_en = GPIO_PULLUP_ENABLE,
.pull_down_en = GPIO_PULLDOWN_DISABLE,
.intr_type = GPIO_INTR_DISABLE
};

  
esp_err_t ret = gpio_config(&config);

if (ret != ESP_OK) {
    ESP_LOGE(
        TAG,
        "Failed to initialize card detect: %s",
        esp_err_to_name(ret)
    );

    return;
}

ESP_LOGI(
    TAG,
    "Card detect initialized"
);

ESP_LOGI(
    TAG,
    "Card %s",
    sdcard_is_inserted()
        ? "inserted"
        : "not inserted"
);
  

}

bool sdcard_is_inserted(void)
{
/*
* Adafruit DET behavior:
*
* HIGH = card inserted
* LOW  = no card
*/
return gpio_get_level(SD_DETECT) != 0;
}

/* -------------------------------------------------------------------------- */
/* SPI BUS                                                                    */
/* -------------------------------------------------------------------------- */

static bool sdcard_init_spi_bus(void)
{
if (spi_bus_initialized) {
return true;
}

  
ESP_LOGI(
    TAG,
    "Initializing SPI2 bus"
);

spi_bus_config_t bus_config = {
    .mosi_io_num = SD_SI,
    .miso_io_num = SD_SO,
    .sclk_io_num = SD_CLK,

    .quadwp_io_num = -1,
    .quadhd_io_num = -1,

    .max_transfer_sz = 4096
};

esp_err_t ret = spi_bus_initialize(
    SPI2_HOST,
    &bus_config,
    SPI_DMA_CH_AUTO
);

if (ret != ESP_OK) {
    ESP_LOGE(
        TAG,
        "Failed to initialize SPI2: %s",
        esp_err_to_name(ret)
    );

    return false;
}

spi_bus_initialized = true;

ESP_LOGI(
    TAG,
    "SPI2 bus initialized"
);

return true;
  

}

/* -------------------------------------------------------------------------- */
/* SD INITIALIZATION                                                          */
/* -------------------------------------------------------------------------- */

bool sdcard_init(void)
{
ESP_LOGI(
TAG,
"Initializing SD card"
);

  
/*
 * Do not attempt to mount a missing card.
 */
if (!sdcard_is_inserted()) {

    ESP_LOGW(
        TAG,
        "No SD card inserted"
    );

    return false;
}


if (!sdcard_init_spi_bus()) {
    return false;
}


sdmmc_host_t host =
    SDSPI_HOST_DEFAULT();


sdspi_device_config_t slot_config =
    SDSPI_DEVICE_CONFIG_DEFAULT();


slot_config.host_id =
    SPI2_HOST;

slot_config.gpio_cs =
    SD_CS;

/*
 * Enable hardware/software card-detect awareness in the
 * ESP-IDF SDSPI device configuration.
 */
slot_config.gpio_cd =
    SDSPI_SLOT_NO_CD;

slot_config.gpio_wp =
    SDSPI_SLOT_NO_WP;


esp_vfs_fat_mount_config_t mount_config = {
    .format_if_mount_failed = false,
    .max_files = 8,
    .allocation_unit_size = 0
};


esp_err_t ret =
    esp_vfs_fat_sdspi_mount(
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

    mounted = false;

    return false;
}


mounted = true;


ESP_LOGI(
    TAG,
    "SD card mounted at %s",
    SD_MOUNT_POINT
);


sdmmc_card_print_info(
    stdout,
    sd_card
);


return true;
  

}

/* -------------------------------------------------------------------------- */
/* STATUS                                                                     */
/* -------------------------------------------------------------------------- */

bool sdcard_is_mounted(void)
{
return mounted;
}

/* -------------------------------------------------------------------------- */
/* FILE SYSTEM TEST                                                           */
/* -------------------------------------------------------------------------- */

void sdcard_test(void)
{
if (!mounted) {

  
    ESP_LOGE(
        TAG,
        "SD card is not mounted"
    );

    return;
}


/* ---------------------------------------------------------------------- */
/* Root directory                                                         */
/* ---------------------------------------------------------------------- */

ESP_LOGI(
    TAG,
    "Listing %s",
    SD_MOUNT_POINT
);


DIR *dir =
    opendir(SD_MOUNT_POINT);


if (dir == NULL) {

    ESP_LOGE(
        TAG,
        "Could not open %s",
        SD_MOUNT_POINT
    );

    return;
}


struct dirent *entry;


while (
    (entry = readdir(dir)) != NULL
) {

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


/* ---------------------------------------------------------------------- */
/* Create test file                                                       */
/* ---------------------------------------------------------------------- */

const char *test_path =
    SD_MOUNT_POINT "/yelyah_test.txt";


const char *test_text =
    "YELYAH SD CARD TEST\n"
    "File I/O is working.\n";


ESP_LOGI(
    TAG,
    "Creating %s",
    test_path
);


FILE *file =
    fopen(
        test_path,
        "w"
    );


if (file == NULL) {

    ESP_LOGE(
        TAG,
        "Could not create %s",
        test_path
    );

    return;
}


size_t written =
    fwrite(
        test_text,
        1,
        strlen(test_text),
        file
    );


fclose(file);


if (
    written !=
    strlen(test_text)
) {

    ESP_LOGE(
        TAG,
        "Write failed: %u of %u bytes",
        (unsigned)written,
        (unsigned)strlen(test_text)
    );

    return;
}


ESP_LOGI(
    TAG,
    "File written successfully"
);


/* ---------------------------------------------------------------------- */
/* Read test file                                                         */
/* ---------------------------------------------------------------------- */

ESP_LOGI(
    TAG,
    "Reading %s",
    test_path
);


file =
    fopen(
        test_path,
        "r"
    );


if (file == NULL) {

    ESP_LOGE(
        TAG,
        "Could not open %s for reading",
        test_path
    );

    return;
}


char buffer[128];


memset(
    buffer,
    0,
    sizeof(buffer)
);


size_t bytes_read =
    fread(
        buffer,
        1,
        sizeof(buffer) - 1,
        file
    );


fclose(file);


if (bytes_read == 0) {

    ESP_LOGE(
        TAG,
        "Read failed"
    );

    return;
}


ESP_LOGI(
    TAG,
    "Read %u bytes:",
    (unsigned)bytes_read
);


printf(
    "%s",
    buffer
);


/* ---------------------------------------------------------------------- */
/* Verify                                                                  */
/* ---------------------------------------------------------------------- */

if (
    strcmp(
        buffer,
        test_text
    ) == 0
) {

    ESP_LOGI(
        TAG,
        "FILE TEST PASSED"
    );

} else {

    ESP_LOGE(
        TAG,
        "FILE TEST FAILED"
    );
}


/* ---------------------------------------------------------------------- */
/* List again                                                             */
/* ---------------------------------------------------------------------- */

ESP_LOGI(
    TAG,
    "Listing %s again",
    SD_MOUNT_POINT
);


dir =
    opendir(
        SD_MOUNT_POINT
    );


if (dir == NULL) {

    ESP_LOGE(
        TAG,
        "Could not reopen %s",
        SD_MOUNT_POINT
    );

    return;
}


while (
    (entry = readdir(dir)) != NULL
) {

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


ESP_LOGI(
    TAG,
    "SD card file test complete"
);
  

}
