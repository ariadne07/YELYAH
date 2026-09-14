#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

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

void display_show_test(void);

#endif
