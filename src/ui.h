#ifndef UI_H
#define UI_H

#include "buttons.h"

void ui_init(void);

void ui_handle_event(
button_event_t event
);

/*
 * Call regularly from the main loop. Keeps the Now
 * Playing screen in sync with the player and clears
 * pop-up messages.
 */
void ui_tick(void);

/*
 * Fully redraws the current screen.
 */
void ui_render(void);

#endif
