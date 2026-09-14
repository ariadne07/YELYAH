#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

#define DISPLAY_WIDTH  240
#define DISPLAY_HEIGHT 320

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

void display_draw_char(
    int x,
    int y,
    char character,
    uint16_t color,
    uint16_t background,
    int scale
);

void display_draw_text(
    int x,
    int y,
    const char *text,
    uint16_t color,
    uint16_t background,
    int scale
);

void display_show_test(void);

#endif