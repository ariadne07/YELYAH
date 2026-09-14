#ifndef UI_H
#define UI_H

#include "buttons.h"

typedef enum {
    UI_MENU_VIEW,
    UI_NOW_PLAYING
} ui_screen_t;

void ui_init(void);

void ui_handle_event(button_event_t event);

void ui_render(void);

ui_screen_t ui_get_screen(void);

#endif