#include "display.h"
#include "pins.h"

#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ili9341.h"
#include "esp_log.h"

#include <stdbool.h>
#include <stdint.h>

static const char *TAG = "DISPLAY";

static esp_lcd_panel_io_handle_t lcd_io = NULL;
static esp_lcd_panel_handle_t lcd_panel = NULL;


/*
 * ============================================================
 * 5x7 FONT
 * ============================================================
 *
 * Each character is 5 pixels wide x 7 pixels tall.
 *
 * Each byte represents one vertical column.
 * Bit 0 = top pixel
 * Bit 6 = bottom pixel
 *
 * Supported:
 *   A-Z
 *   0-9
 *   SPACE
 *   -
 *   .
 *   :
 *   /
 *   ?
 *   !
 */
 
static const uint8_t font_uppercase[26][5] = {

    /* A */
    {0x7E, 0x11, 0x11, 0x11, 0x7E},

    /* B */
    {0x7F, 0x49, 0x49, 0x49, 0x36},

    /* C */
    {0x3E, 0x41, 0x41, 0x41, 0x22},

    /* D */
    {0x7F, 0x41, 0x41, 0x22, 0x1C},

    /* E */
    {0x7F, 0x49, 0x49, 0x49, 0x41},

    /* F */
    {0x7F, 0x09, 0x09, 0x09, 0x01},

    /* G */
    {0x3E, 0x41, 0x49, 0x49, 0x7A},

    /* H */
    {0x7F, 0x08, 0x08, 0x08, 0x7F},

    /* I */
    {0x41, 0x41, 0x7F, 0x41, 0x41},

    /* J */
    {0x20, 0x40, 0x41, 0x3F, 0x01},

    /* K */
    {0x7F, 0x08, 0x14, 0x22, 0x41},

    /* L */
    {0x7F, 0x40, 0x40, 0x40, 0x40},

    /* M */
    {0x7F, 0x02, 0x04, 0x02, 0x7F},

    /* N */
    {0x7F, 0x04, 0x08, 0x10, 0x7F},

    /* O */
    {0x3E, 0x41, 0x41, 0x41, 0x3E},

    /* P */
    {0x7F, 0x09, 0x09, 0x09, 0x06},

    /* Q */
    {0x3E, 0x41, 0x51, 0x21, 0x5E},

    /* R */
    {0x7F, 0x09, 0x19, 0x29, 0x46},

    /* S */
    {0x46, 0x49, 0x49, 0x49, 0x31},

    /* T */
    {0x01, 0x01, 0x7F, 0x01, 0x01},

    /* U */
    {0x3F, 0x40, 0x40, 0x40, 0x3F},

    /* V */
    {0x1F, 0x20, 0x40, 0x20, 0x1F},

    /* W */
    {0x7F, 0x20, 0x18, 0x20, 0x7F},

    /* X */
    {0x63, 0x14, 0x08, 0x14, 0x63},

    /* Y */
    {0x07, 0x08, 0x70, 0x08, 0x07},

    /* Z */
    {0x61, 0x51, 0x49, 0x45, 0x43}
};


/*
 * Numbers 0-9
 */
static const uint8_t font_numbers[10][5] = {

    /* 0 */
    {0x3E, 0x51, 0x49, 0x45, 0x3E},

    /* 1 */
    {0x00, 0x42, 0x7F, 0x40, 0x00},

    /* 2 */
    {0x42, 0x61, 0x51, 0x49, 0x46},

    /* 3 */
    {0x21, 0x41, 0x45, 0x4B, 0x31},

    /* 4 */
    {0x18, 0x14, 0x12, 0x7F, 0x10},

    /* 5 */
    {0x27, 0x45, 0x45, 0x45, 0x39},

    /* 6 */
    {0x3C, 0x4A, 0x49, 0x49, 0x30},

    /* 7 */
    {0x01, 0x71, 0x09, 0x05, 0x03},

    /* 8 */
    {0x36, 0x49, 0x49, 0x49, 0x36},

    /* 9 */
    {0x06, 0x49, 0x49, 0x29, 0x1E}
};


/*
 * Additional characters
 */
static const uint8_t font_space[5] = {
    0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t font_dash[5] = {
    0x08, 0x08, 0x08, 0x08, 0x08
};

static const uint8_t font_period[5] = {
    0x00, 0x60, 0x60, 0x00, 0x00
};

static const uint8_t font_colon[5] = {
    0x00, 0x36, 0x36, 0x00, 0x00
};

static const uint8_t font_slash[5] = {
    0x40, 0x20, 0x10, 0x08, 0x04
};

static const uint8_t font_question[5] = {
    0x02, 0x01, 0x51, 0x09, 0x06
};

static const uint8_t font_exclamation[5] = {
    0x00, 0x00, 0x5F, 0x00, 0x00
};


/*
 * Return the bitmap for a character.
 */
static const uint8_t *get_glyph(char character)
{
    /*
     * Convert lowercase to uppercase.
     */
    if (character >= 'a' && character <= 'z') {
        character = (char)(character - 'a' + 'A');
    }

    /*
     * A-Z
     */
    if (character >= 'A' && character <= 'Z') {
        return font_uppercase[character - 'A'];
    }

    /*
     * 0-9
     */
    if (character >= '0' && character <= '9') {
        return font_numbers[character - '0'];
    }

    switch (character) {

        case ' ':
            return font_space;

        case '-':
            return font_dash;

        case '.':
            return font_period;

        case ':':
            return font_colon;

        case '/':
            return font_slash;

        case '?':
            return font_question;

        case '!':
            return font_exclamation;

        default:
            return font_question;
    }
}


/*
 * ============================================================
 * LCD INITIALIZATION
 * ============================================================
 */

void display_init(void)
{
    ESP_LOGI(TAG, "Initializing ILI9341");

    /*
     * SPI bus configuration.
     */
    spi_bus_config_t bus_config = {
        .sclk_io_num = LCD_CLK,
        .mosi_io_num = LCD_DIN,
        .miso_io_num = SD_SO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = DISPLAY_WIDTH * 80 * sizeof(uint16_t)
    };

    ESP_ERROR_CHECK(
        spi_bus_initialize(
            SPI2_HOST,
            &bus_config,
            SPI_DMA_CH_AUTO
        )
    );


    /*
     * SPI connection between ESP32 and LCD.
     */
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_DC,
        .cs_gpio_num = LCD_CS,
        .pclk_hz = 20 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10
    };

    ESP_ERROR_CHECK(
        esp_lcd_new_panel_io_spi(
            (esp_lcd_spi_bus_handle_t)SPI2_HOST,
            &io_config,
            &lcd_io
        )
    );


    /*
     * ILI9341 panel configuration.
     *
     * Wokwi does not provide a reset pin, so we use -1.
     */
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16
    };

    ESP_ERROR_CHECK(
        esp_lcd_new_panel_ili9341(
            lcd_io,
            &panel_config,
            &lcd_panel
        )
    );


    /*
     * Reset and initialize the LCD.
     */
    ESP_ERROR_CHECK(
        esp_lcd_panel_reset(lcd_panel)
    );

    ESP_ERROR_CHECK(
        esp_lcd_panel_init(lcd_panel)
    );


    /*
     * Initial orientation.
     */
    ESP_ERROR_CHECK(
        esp_lcd_panel_swap_xy(
            lcd_panel,
            false
        )
    );

    ESP_ERROR_CHECK(
        esp_lcd_panel_mirror(
            lcd_panel,
            true,
            false
        )
    );


    /*
     * Turn display on.
     */
    ESP_ERROR_CHECK(
        esp_lcd_panel_disp_on_off(
            lcd_panel,
            true
        )
    );

    ESP_LOGI(TAG, "ILI9341 initialized");
}


/*
 * ============================================================
 * BASIC DRAWING
 * ============================================================
 */

/*
 * Fill the entire display with one RGB565 color.
 */
void display_clear(uint16_t color)
{
    display_fill_rect(
        0,
        0,
        DISPLAY_WIDTH,
        DISPLAY_HEIGHT,
        color
    );
}


/*
 * Draw a filled rectangle.
 */
void display_fill_rect(
    int x,
    int y,
    int width,
    int height,
    uint16_t color
)
{
    if (lcd_panel == NULL) {
        return;
    }

    /*
     * Clip rectangle to display boundaries.
     */
    if (x < 0) {
        width += x;
        x = 0;
    }

    if (y < 0) {
        height += y;
        y = 0;
    }

    if (x + width > DISPLAY_WIDTH) {
        width = DISPLAY_WIDTH - x;
    }

    if (y + height > DISPLAY_HEIGHT) {
        height = DISPLAY_HEIGHT - y;
    }

    if (width <= 0 || height <= 0) {
        return;
    }


    /*
     * RGB565 is sent MSB first.
     */
    static uint16_t line_buffer[DISPLAY_WIDTH];

    uint16_t swapped_color =
        (uint16_t)((color << 8) | (color >> 8));

    for (int i = 0; i < width; i++) {
        line_buffer[i] = swapped_color;
    }


    /*
     * Draw one row at a time.
     */
    for (int row = 0; row < height; row++) {

        ESP_ERROR_CHECK(
            esp_lcd_panel_draw_bitmap(
                lcd_panel,
                x,
                y + row,
                x + width,
                y + row + 1,
                line_buffer
            )
        );
    }
}


/*
 * Draw a single pixel.
 */
void display_draw_pixel(
    int x,
    int y,
    uint16_t color
)
{
    if (x < 0 || x >= DISPLAY_WIDTH ||
        y < 0 || y >= DISPLAY_HEIGHT) {
        return;
    }

    display_fill_rect(
        x,
        y,
        1,
        1,
        color
    );
}


/*
 * ============================================================
 * TEXT RENDERING
 * ============================================================
 */

/*
 * Draw one 5x7 character.
 *
 * scale = 1:
 *     character is 5x7 pixels
 *
 * scale = 2:
 *     character is 10x14 pixels
 *
 * scale = 3:
 *     character is 15x21 pixels
 */
void display_draw_char(
    int x,
    int y,
    char character,
    uint16_t color,
    uint16_t background,
    int scale
)
{
    if (scale < 1) {
        scale = 1;
    }

    const uint8_t *glyph = get_glyph(character);

    for (int column = 0; column < 5; column++) {

        uint8_t column_data = glyph[column];

        for (int row = 0; row < 7; row++) {

            bool pixel_on =
                (column_data & (1U << row)) != 0;

            uint16_t pixel_color;

            if (pixel_on) {
                pixel_color = color;
            } else {
                pixel_color = background;
            }

            display_fill_rect(
                x + (column * scale),
                y + (row * scale),
                scale,
                scale,
                pixel_color
            );
        }
    }
}


/*
 * Draw a string.
 */
void display_draw_text(
    int x,
    int y,
    const char *text,
    uint16_t color,
    uint16_t background,
    int scale
)
{
    if (text == NULL) {
        return;
    }

    if (scale < 1) {
        scale = 1;
    }

    int cursor_x = x;

    while (*text != '\0') {

        display_draw_char(
            cursor_x,
            y,
            *text,
            color,
            background,
            scale
        );

        /*
         * 5 pixels for character
         * + 1 pixel spacing
         */
        cursor_x += 6 * scale;

        text++;
    }
}


/*
 * ============================================================
 * DISPLAY TEST
 * ============================================================
 */

void display_show_test(void)
{
    const uint16_t BLACK = 0x0000;
    const uint16_t WHITE = 0xFFFF;
    const uint16_t RED   = 0xF800;
    const uint16_t GREEN = 0x07E0;

    display_clear(BLACK);

    display_draw_text(
        10,
        10,
        "YELYAH",
        WHITE,
        BLACK,
        3
    );

    display_draw_text(
        10,
        40,
        "MP3 PLAYER",
        RED,
        BLACK,
        2
    );

    display_draw_text(
        10,
        80,
        "MENU",
        GREEN,
        BLACK,
        2
    );

    display_draw_text(
        10,
        110,
        "> MUSIC",
        WHITE,
        BLACK,
        2
    );

    display_draw_text(
        10,
        130,
        "ARTISTS",
        WHITE,
        BLACK,
        2
    );

    display_draw_text(
        10,
        150,
        "ALBUMS",
        WHITE,
        BLACK,
        2
    );

    display_draw_text(
        10,
        170,
        "PLAYLISTS",
        WHITE,
        BLACK,
        2
    );

    display_draw_text(
        10,
        190,
        "SETTINGS",
        WHITE,
        BLACK,
        2
    );

    ESP_LOGI(TAG, "Display test rendered");
}