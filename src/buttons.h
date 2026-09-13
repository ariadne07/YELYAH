#ifndef BUTTONS_H
#define BUTTONS_H

typedef enum {
    BUTTON_NONE = 0,

    BUTTON_UP,
    BUTTON_DOWN,
    BUTTON_LEFT,
    BUTTON_RIGHT,
    BUTTON_CENTER
} button_event_t;

void buttons_init(void);
button_event_t buttons_get_event(void);

#endif