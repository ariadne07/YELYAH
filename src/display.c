#include "display.h"
#include "pins.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_ili9341.h"
#include "esp_log.h"

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static const char *TAG = "DISPLAY";

static esp_lcd_panel_io_handle_t io_handle = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;

/* -------------------------------------------------------------------------- */
/* Display constants                                                          */
/* -------------------------------------------------------------------------- */

#define DISPLAY_WIDTH  240
#define DISPLAY_HEIGHT 320

#define COLOR_BLACK 0x0000
#define COLOR_WHITE 0xFFFF
#define COLOR_RED   0xF800
#define COLOR_GREEN 0x07E0
#define COLOR_BLUE  0x001F

/* -------------------------------------------------------------------------- */
/* 5x7 font                                                                   */
/* -------------------------------------------------------------------------- */

/*

* Only the characters currently needed by the YELYAH UI are included.
*
* Each byte represents one column.
* Bit 0 = top pixel.
  */

static const uint8_t glyph_space[5] = {
0x00, 0x00, 0x00, 0x00, 0x00
};

static const uint8_t glyph_A[5] = {
0x7E, 0x11, 0x11, 0x11, 0x7E
};

static const uint8_t glyph_B[5] = {
0x7F, 0x49, 0x49, 0x49, 0x36
};

static const uint8_t glyph_C[5] = {
0x3E, 0x41, 0x41, 0x41, 0x22
};

static const uint8_t glyph_D[5] = {
0x7F, 0x41, 0x41, 0x22, 0x1C
};

static const uint8_t glyph_E[5] = {
0x7F, 0x49, 0x49, 0x49, 0x41
};

static const uint8_t glyph_F[5] = {
0x7F, 0x09, 0x09, 0x09, 0x01
};

static const uint8_t glyph_G[5] = {
0x3E, 0x41, 0x49, 0x49, 0x7A
};

static const uint8_t glyph_H[5] = {
0x7F, 0x08, 0x08, 0x08, 0x7F
};

static const uint8_t glyph_I[5] = {
0x00, 0x41, 0x7F, 0x41, 0x00
};

static const uint8_t glyph_J[5] = {
0x20, 0x40, 0x41, 0x3F, 0x01
};

static const uint8_t glyph_K[5] = {
0x7F, 0x08, 0x14, 0x22, 0x41
};

static const uint8_t glyph_L[5] = {
0x7F, 0x40, 0x40, 0x40, 0x40
};

static const uint8_t glyph_M[5] = {
0x7F, 0x02, 0x0C, 0x02, 0x7F
};

static const uint8_t glyph_N[5] = {
0x7F, 0x04, 0x08, 0x10, 0x7F
};

static const uint8_t glyph_O[5] = {
0x3E, 0x41, 0x41, 0x41, 0x3E
};

static const uint8_t glyph_P[5] = {
0x7F, 0x09, 0x09, 0x09, 0x06
};

static const uint8_t glyph_Q[5] = {
0x3E, 0x41, 0x51, 0x21, 0x5E
};

static const uint8_t glyph_R[5] = {
0x7F, 0x09, 0x19, 0x29, 0x46
};

static const uint8_t glyph_S[5] = {
0x46, 0x49, 0x49, 0x49, 0x31
};

static const uint8_t glyph_T[5] = {
0x01, 0x01, 0x7F, 0x01, 0x01
};

static const uint8_t glyph_U[5] = {
0x3F, 0x40, 0x40, 0x40, 0x3F
};

static const uint8_t glyph_V[5] = {
0x1F, 0x20, 0x40, 0x20, 0x1F
};

static const uint8_t glyph_W[5] = {
0x7F, 0x20, 0x18, 0x20, 0x7F
};

static const uint8_t glyph_X[5] = {
0x63, 0x14, 0x08, 0x14, 0x63
};

static const uint8_t glyph_Y[5] = {
0x07, 0x08, 0x70, 0x08, 0x07
};

static const uint8_t glyph_Z[5] = {
0x61, 0x51, 0x49, 0x45, 0x43
};

static const uint8_t glyph_0[5] = {
0x3E, 0x51, 0x49, 0x45, 0x3E
};

static const uint8_t glyph_1[5] = {
0x00, 0x42, 0x7F, 0x40, 0x00
};

static const uint8_t glyph_2[5] = {
0x42, 0x61, 0x51, 0x49, 0x46
};

static const uint8_t glyph_3[5] = {
0x21, 0x41, 0x45, 0x4B, 0x31
};

static const uint8_t glyph_4[5] = {
0x18, 0x14, 0x12, 0x7F, 0x10
};

static const uint8_t glyph_5[5] = {
0x27, 0x45, 0x45, 0x45, 0x39
};

static const uint8_t glyph_6[5] = {
0x3C, 0x4A, 0x49, 0x49, 0x30
};

static const uint8_t glyph_7[5] = {
0x01, 0x71, 0x09, 0x05, 0x03
};

static const uint8_t glyph_8[5] = {
0x36, 0x49, 0x49, 0x49, 0x36
};

static const uint8_t glyph_9[5] = {
0x06, 0x49, 0x49, 0x29, 0x1E
};

static const uint8_t glyph_dash[5] = {
0x08, 0x08, 0x08, 0x08, 0x08
};

static const uint8_t glyph_period[5] = {
0x00, 0x60, 0x60, 0x00, 0x00
};

static const uint8_t glyph_colon[5] = {
0x00, 0x36, 0x36, 0x00, 0x00
};

static const uint8_t glyph_slash[5] = {
0x20, 0x10, 0x08, 0x04, 0x02
};

static const uint8_t glyph_question[5] = {
0x02, 0x01, 0x51, 0x09, 0x06
};

static const uint8_t glyph_exclamation[5] = {
0x00, 0x00, 0x5F, 0x00, 0x00
};

/* -------------------------------------------------------------------------- */
/* Font lookup                                                                */
/* -------------------------------------------------------------------------- */

static const uint8_t *get_glyph(char character)
{
if (
character >= 'a' &&
character <= 'z'
) {
character =
(char)(character - 'a' + 'A');
}


switch (character) {

    case 'A': return glyph_A;
    case 'B': return glyph_B;
    case 'C': return glyph_C;
    case 'D': return glyph_D;
    case 'E': return glyph_E;
    case 'F': return glyph_F;
    case 'G': return glyph_G;
    case 'H': return glyph_H;
    case 'I': return glyph_I;
    case 'J': return glyph_J;
    case 'K': return glyph_K;
    case 'L': return glyph_L;
    case 'M': return glyph_M;
    case 'N': return glyph_N;
    case 'O': return glyph_O;
    case 'P': return glyph_P;
    case 'Q': return glyph_Q;
    case 'R': return glyph_R;
    case 'S': return glyph_S;
    case 'T': return glyph_T;
    case 'U': return glyph_U;
    case 'V': return glyph_V;
    case 'W': return glyph_W;
    case 'X': return glyph_X;
    case 'Y': return glyph_Y;
    case 'Z': return glyph_Z;

    case '0': return glyph_0;
    case '1': return glyph_1;
    case '2': return glyph_2;
    case '3': return glyph_3;
    case '4': return glyph_4;
    case '5': return glyph_5;
    case '6': return glyph_6;
    case '7': return glyph_7;
    case '8': return glyph_8;
    case '9': return glyph_9;

    case '-': return glyph_dash;
    case '.': return glyph_period;
    case ':': return glyph_colon;
    case '/': return glyph_slash;
    case '?': return glyph_question;
    case '!': return glyph_exclamation;

    case ' ':
    default:
        return glyph_space;
}


}

/* -------------------------------------------------------------------------- */
/* RGB565 helper                                                              */
/* -------------------------------------------------------------------------- */

static uint16_t swap_rgb565_bytes(uint16_t color)
{
return (uint16_t)(
((color & 0x00FF) << 8) |
((color & 0xFF00) >> 8)
);
}

/* -------------------------------------------------------------------------- */
/* Display initialization                                                     */
/* -------------------------------------------------------------------------- */

void display_init(void)
{
esp_err_t ret;

#ifndef WOKWI


/*
 * Real hardware backlight.
 */
gpio_config_t bl_config = {
    .pin_bit_mask = 1ULL << LCD_BL,
    .mode = GPIO_MODE_OUTPUT,
    .pull_up_en = GPIO_PULLUP_DISABLE,
    .pull_down_en = GPIO_PULLDOWN_DISABLE,
    .intr_type = GPIO_INTR_DISABLE
};

ret = gpio_config(&bl_config);
ESP_ERROR_CHECK(ret);

ret = gpio_set_level(LCD_BL, 1);
ESP_ERROR_CHECK(ret);


#endif


/*
 * SPI bus.
 *
 * LCD and SD eventually share SPI2.
 */
spi_bus_config_t bus_config = {
    .sclk_io_num = LCD_CLK,
    .mosi_io_num = LCD_DIN,
    .miso_io_num = -1,

    .quadwp_io_num = -1,
    .quadhd_io_num = -1,

    .max_transfer_sz =
        DISPLAY_WIDTH *
        DISPLAY_HEIGHT *
        sizeof(uint16_t)
};


ret = spi_bus_initialize(
    SPI2_HOST,
    &bus_config,
    SPI_DMA_CH_AUTO
);

ESP_ERROR_CHECK(ret);


/*
 * LCD SPI interface.
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


ret = esp_lcd_new_panel_io_spi(
    (esp_lcd_spi_bus_handle_t)SPI2_HOST,
    &io_config,
    &io_handle
);

ESP_ERROR_CHECK(ret);


/*
 * ILI9341 panel.
 */
esp_lcd_panel_dev_config_t panel_config = {
    .reset_gpio_num = -1,
    .rgb_ele_order =
        LCD_RGB_ELEMENT_ORDER_RGB,
    .bits_per_pixel = 16
};


ret = esp_lcd_new_panel_ili9341(
    io_handle,
    &panel_config,
    &panel_handle
);

ESP_ERROR_CHECK(ret);


ret = esp_lcd_panel_reset(
    panel_handle
);

ESP_ERROR_CHECK(ret);


ret = esp_lcd_panel_init(
    panel_handle
);

ESP_ERROR_CHECK(ret);


ret = esp_lcd_panel_disp_on_off(
    panel_handle,
    true
);

ESP_ERROR_CHECK(ret);


/*
 * Keep the same basic orientation you had working.
 */
ret = esp_lcd_panel_swap_xy(
    panel_handle,
    false
);

ESP_ERROR_CHECK(ret);


ret = esp_lcd_panel_mirror(
    panel_handle,
    true,
    false
);

ESP_ERROR_CHECK(ret);


ESP_LOGI(
    TAG,
    "Display initialized"
);


}

/* -------------------------------------------------------------------------- */
/* Draw filled rectangle                                                      */
/* -------------------------------------------------------------------------- */

void display_fill_rect(
int x,
int y,
int width,
int height,
uint16_t color
)
{
if (panel_handle == NULL) {
return;
}


/*
 * Clip rectangle to display.
 */
if (x < 0) {
    width += x;
    x = 0;
}


if (y < 0) {
    height += y;
    y = 0;
}


if (
    x + width >
    DISPLAY_WIDTH
) {
    width =
        DISPLAY_WIDTH - x;
}


if (
    y + height >
    DISPLAY_HEIGHT
) {
    height =
        DISPLAY_HEIGHT - y;
}


if (
    width <= 0 ||
    height <= 0
) {
    return;
}


/*
 * One row at a time keeps memory use small.
 */
static uint16_t line[DISPLAY_WIDTH];


uint16_t spi_color =
    swap_rgb565_bytes(color);


for (
    int i = 0;
    i < width;
    i++
) {
    line[i] = spi_color;
}


for (
    int row = 0;
    row < height;
    row++
) {

    esp_err_t ret =
        esp_lcd_panel_draw_bitmap(
            panel_handle,

            x,
            y + row,

            x + width,
            y + row + 1,

            line
        );

    ESP_ERROR_CHECK(ret);
}


}

/* -------------------------------------------------------------------------- */
/* Clear entire display                                                       */
/* -------------------------------------------------------------------------- */

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

/* -------------------------------------------------------------------------- */
/* Draw single pixel                                                          */
/* -------------------------------------------------------------------------- */

void display_draw_pixel(
int x,
int y,
uint16_t color
)
{
if (panel_handle == NULL) {
return;
}


if (
    x < 0 ||
    x >= DISPLAY_WIDTH ||
    y < 0 ||
    y >= DISPLAY_HEIGHT
) {
    return;
}


uint16_t pixel =
    swap_rgb565_bytes(color);


esp_err_t ret =
    esp_lcd_panel_draw_bitmap(
        panel_handle,

        x,
        y,

        x + 1,
        y + 1,

        &pixel
    );


ESP_ERROR_CHECK(ret);


}

/* -------------------------------------------------------------------------- */
/* Draw character                                                             */
/* -------------------------------------------------------------------------- */

static void display_draw_char(
int x,
int y,
char character,
uint16_t color,
uint16_t bg_color,
int scale
)
{
if (scale < 1) {
scale = 1;
}


const uint8_t *glyph =
    get_glyph(character);


/*
 * Five font columns.
 */
for (
    int column = 0;
    column < 5;
    column++
) {

    uint8_t bits =
        glyph[column];


    for (
        int row = 0;
        row < 7;
        row++
    ) {

        bool pixel_on =
            (bits &
             (1U << row)) != 0;


        uint16_t pixel_color =
            pixel_on
                ? color
                : bg_color;


        display_fill_rect(
            x + column * scale,
            y + row * scale,
            scale,
            scale,
            pixel_color
        );
    }
}


/*
 * One blank column between characters.
 */
display_fill_rect(
    x + 5 * scale,
    y,
    scale,
    7 * scale,
    bg_color
);


}

/* -------------------------------------------------------------------------- */
/* Draw text                                                                  */
/* -------------------------------------------------------------------------- */

void display_draw_text(
int x,
int y,
const char *text,
uint16_t color,
uint16_t bg_color,
int scale
)
{
if (
text == NULL ||
panel_handle == NULL
) {
return;
}


if (scale < 1) {
    scale = 1;
}


int cursor_x = x;


while (*text != '\0') {

    /*
     * Wrap to the next line if necessary.
     */
    if (
        cursor_x +
        (6 * scale) >
        DISPLAY_WIDTH
    ) {

        cursor_x = x;
        y += 8 * scale;
    }


    /*
     * Stop if we're below the display.
     */
    if (
        y >= DISPLAY_HEIGHT
    ) {
        break;
    }


    display_draw_char(
        cursor_x,
        y,
        *text,
        color,
        bg_color,
        scale
    );


    cursor_x +=
        6 * scale;


    text++;
}


}

/* -------------------------------------------------------------------------- */
/* Convenience fill function                                                  */
/* -------------------------------------------------------------------------- */

void display_fill(uint16_t color)
{
display_clear(color);
}

/* -------------------------------------------------------------------------- */
/* Display test pattern                                                       */
/* -------------------------------------------------------------------------- */

void display_test_pattern(void)
{
display_fill(COLOR_RED);
display_fill(COLOR_GREEN);
display_fill(COLOR_BLUE);
display_fill(COLOR_WHITE);
display_fill(COLOR_BLACK);


ESP_LOGI(
    TAG,
    "Display test pattern complete"
);


}

/*

* Keep the old public test name available if some other file still calls it.
  */
  void display_show_test(void)
  {
  display_test_pattern();
  }
