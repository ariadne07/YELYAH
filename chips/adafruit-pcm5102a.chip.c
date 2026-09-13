//gemini written 

#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>

// 1. Define a struct to hold references to your chip's pins and internal values
typedef struct {
  pin_t pin_bck;
  pin_t pin_din;
  pin_t pin_lrck;
} chip_state_t;

// 2. This callback runs automatically whenever a monitored input pin flips HIGH or LOW
static void pin_change_callback(void *user_data, pin_t pin, uint32_t value) {
  chip_state_t *chip = (chip_state_t*)user_data;

  // Now you don't even need to call pin_read()! 
  // 'pin' tells you which pin flipped, and 'value' tells you if it is 1 (HIGH) or 0 (LOW).
}


// 3. This function is called by Wokwi once when the simulation canvas boots up
void chip_init() {
  // Allocate memory space for this chip instance
  chip_state_t *chip = malloc(sizeof(chip_state_t));
  
  // Bind your C pins to the exact labels specified inside your pcm5102a.chip.json array
  chip->pin_bck  = pin_init("BCK", INPUT);
  chip->pin_din  = pin_init("DIN", INPUT);
  chip->pin_lrck = pin_init("LRCK", INPUT);

  // Set up a listener configuration block to track signal edges
  const pin_watch_config_t watch_config = {
    .edge = BOTH,                         // Trigger callback on both rising and falling edges
    .pin_change = pin_change_callback,    // Direct it to your callback logic above
    .user_data = chip,                    // Pass our pin state struct pointer
  };
  
  // Begin watching the pins for incoming signals
  pin_watch(chip->pin_bck, &watch_config);
  pin_watch(chip->pin_din, &watch_config);
  pin_watch(chip->pin_lrck, &watch_config);
}
