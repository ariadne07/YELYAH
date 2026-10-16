#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

#define DISPLAY_WIDTH  240
#define DISPLAY_HEIGHT 320

/*
 * Font metrics. Each character is 5x7 pixels plus
 * one blank column, multiplied by the text scale.
 */
#define FONT_CHAR_WIDTH  6
#define FONT_CHAR_HEIGHT 7

/* RGB565 colors */
#define COLOR_BLACK 0x0000
#define COLOR_WHITE 0xFFFF
#define COLOR_RED   0xF800
#define COLOR_GREEN 0x07E0
#define COLOR_BLUE  0x001F
#define COLOR_GREY  0x8410
#define COLOR_DARK  0x2104

/*
 * Initializes the shared SPI bus (LCD + SD card)
 * and the LCD panel.
 */
void display_init(void);

void display_clear(uint16_t color);

void display_fill_rect(
int x,
int y,
int width,
int height,
uint16_t color
);

void display_draw_pixel(
int x,
int y,
uint16_t color
);

void display_draw_text(
int x,
int y,
const char *text,
uint16_t color,
uint16_t bg_color,
int scale
);

#endif
