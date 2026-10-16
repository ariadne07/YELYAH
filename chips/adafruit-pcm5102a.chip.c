// Adafruit PCM5102 I2S DAC (PID 6250) stand-in for Wokwi.
//
// Wokwi doesn't simulate I2S on the ESP32-S3, so no audio
// reaches this chip. What it does model is the MU (mute)
// pin: like the real board it is pulled high (unmuted)
// when nothing drives it.
//
// The status bar on the chip shows the mute state:
//   red   = muted (MU low)
//   green = unmuted (MU high)
// Changes are also printed to the Chips Console.

#include "wokwi-api.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
  pin_t pin_mu;
  buffer_t framebuffer;
  uint32_t width;
  uint32_t height;
} chip_state_t;

static void fill_display(chip_state_t *chip, uint8_t r, uint8_t g, uint8_t b) {
  uint8_t pixel[4] = {r, g, b, 0xFF};

  for (uint32_t i = 0; i < chip->width * chip->height; i++) {
    buffer_write(chip->framebuffer, i * 4, pixel, sizeof(pixel));
  }
}

static void show_mute_state(chip_state_t *chip, uint32_t mu_level) {
  if (mu_level == LOW) {
    fill_display(chip, 0xD0, 0x20, 0x20);
    printf("PCM5102: muted\n");
  } else {
    fill_display(chip, 0x20, 0xC0, 0x40);
    printf("PCM5102: unmuted\n");
  }
}

static void on_mute_change(void *user_data, pin_t pin, uint32_t value) {
  show_mute_state((chip_state_t *)user_data, value);
}

void chip_init(void) {
  chip_state_t *chip = malloc(sizeof(chip_state_t));

  // I2S inputs. Nothing to do with them in the simulator,
  // but declaring them keeps the pins high-impedance.
  pin_init("BCK", INPUT);
  pin_init("DIN", INPUT);
  pin_init("WSEL", INPUT);

  // Control pins, with the Adafruit board's defaults:
  // MU pulled up (unmuted); DE, FIL, FM pulled down.
  chip->pin_mu = pin_init("MU", INPUT_PULLUP);
  pin_init("DE", INPUT_PULLDOWN);
  pin_init("FIL", INPUT_PULLDOWN);
  pin_init("FM", INPUT_PULLDOWN);
  pin_init("MCK", INPUT);

  chip->framebuffer = framebuffer_init(&chip->width, &chip->height);

  const pin_watch_config_t watch_config = {
    .user_data = chip,
    .edge = BOTH,
    .pin_change = on_mute_change,
  };

  pin_watch(chip->pin_mu, &watch_config);

  show_mute_state(chip, pin_read(chip->pin_mu));
}
