#include "display.h"
#include "pins.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ili9341.h"
#include "esp_log.h"

static const char *TAG = "DISPLAY";

static esp_lcd_panel_io_handle_t io_handle = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;

void display_init(void)
{
    esp_err_t ret;

    /*
     * --------------------------------------------------
     * Backlight
     * --------------------------------------------------
     *
     * Wokwi doesn't simulate the LCD LED pin, so don't
     * depend on it here.
     */

#ifndef WOKWI
    gpio_config_t bl_config = {
        .pin_bit_mask = 1ULL << LCD_BL,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    gpio_config(&bl_config);
    gpio_set_level(LCD_BL, 1);
#endif


    /*
     * --------------------------------------------------
     * SPI bus
     * --------------------------------------------------
     */

    spi_bus_config_t bus_config = {
        .sclk_io_num = LCD_CLK,
        .mosi_io_num = LCD_DIN,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 240 * 320 * sizeof(uint16_t)
    };

    ret = spi_bus_initialize(
        SPI2_HOST,
        &bus_config,
        SPI_DMA_CH_AUTO
    );

    ESP_ERROR_CHECK(ret);


    /*
     * --------------------------------------------------
     * LCD SPI interface
     * --------------------------------------------------
     */

    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_DC,
        .cs_gpio_num = LCD_CS,
        .pclk_hz = 20 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };

    ret = esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)SPI2_HOST,
        &io_config,
        &io_handle
    );

    ESP_ERROR_CHECK(ret);


    /*
     * --------------------------------------------------
     * ILI9341 panel
     * --------------------------------------------------
     */

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16
    };

    ret = esp_lcd_new_panel_ili9341(
        io_handle,
        &panel_config,
        &panel_handle
    );

    ESP_ERROR_CHECK(ret);


    /*
     * Wokwi has no RST pin, so reset is handled
     * internally by the panel configuration.
     */

    ret = esp_lcd_panel_reset(panel_handle);
    ESP_ERROR_CHECK(ret);

    ret = esp_lcd_panel_init(panel_handle);
    ESP_ERROR_CHECK(ret);


    /*
     * Rotate/orient later once we see the actual
     * simulated display.
     */

    ret = esp_lcd_panel_disp_on_off(panel_handle, true);
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "Display initialized");
}


void display_fill(uint16_t color)
{
    static uint16_t line[240];

    for (int x = 0; x < 240; x++) {
        line[x] = color;
    }

    for (int y = 0; y < 320; y++) {

        esp_err_t ret = esp_lcd_panel_draw_bitmap(
            panel_handle,
            0,
            y,
            240,
            y + 1,
            line
        );

        ESP_ERROR_CHECK(ret);
    }
}


void display_test_pattern(void)
{
    /*
     * RGB565 colors.
     */

    display_fill(0xF800);    // Red
    display_fill(0x07E0);    // Green
    display_fill(0x001F);    // Blue
    display_fill(0xFFFF);    // White
    display_fill(0x0000);    // Black

    ESP_LOGI(TAG, "Display test pattern complete");
}