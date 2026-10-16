// 3.7 V LiPo battery + 100k/100k voltage divider, for Wokwi.
//
// Wokwi can't power the ESP32 from a battery or simulate
// a resistor divider, so this chip models both: the
// "Battery voltage" slider sets the cell voltage and the
// SENSE pin outputs half of it, which is what GPIO10
// sees on the real board.
//
// The bar on the chip shows the charge level
// (green > 40%, yellow > 15%, red below).

#include "wokwi-api.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define DIVIDER_RATIO 2.0f

#define EMPTY_VOLTS 3.2f
#define FULL_VOLTS  4.2f

typedef struct {
  pin_t pin_sense;
  uint32_t voltage_attr;
  float last_volts;
  buffer_t framebuffer;
  uint32_t width;
  uint32_t height;
} chip_state_t;

static void draw_level(chip_state_t *chip, float volts) {
  float level = (volts - EMPTY_VOLTS) / (FULL_VOLTS - EMPTY_VOLTS);

  if (level < 0) level = 0;
  if (level > 1) level = 1;

  uint8_t fill[4] = {0x30, 0xC0, 0x40, 0xFF};

  if (level <= 0.15f) {
    fill[0] = 0xD0; fill[1] = 0x20; fill[2] = 0x20;
  } else if (level <= 0.40f) {
    fill[0] = 0xE0; fill[1] = 0xC0; fill[2] = 0x20;
  }

  uint8_t empty[4] = {0x30, 0x30, 0x30, 0xFF};
  uint32_t filled_columns = (uint32_t)(level * chip->width + 0.5f);

  for (uint32_t y = 0; y < chip->height; y++) {
    for (uint32_t x = 0; x < chip->width; x++) {
      uint8_t *pixel = (x < filled_columns) ? fill : empty;
      buffer_write(chip->framebuffer, (y * chip->width + x) * 4, pixel, 4);
    }
  }
}

static void update(void *user_data) {
  chip_state_t *chip = (chip_state_t *)user_data;

  float volts = attr_read_float(chip->voltage_attr);

  if (volts == chip->last_volts) {
    return;
  }

  chip->last_volts = volts;

  pin_dac_write(chip->pin_sense, volts / DIVIDER_RATIO);
  draw_level(chip, volts);

  printf("Battery: %.2f V (SENSE %.2f V)\n", volts, volts / DIVIDER_RATIO);
}

void chip_init(void) {
  chip_state_t *chip = malloc(sizeof(chip_state_t));

  chip->pin_sense = pin_init("SENSE", ANALOG);
  chip->voltage_attr = attr_init_float("voltage", 3.9f);
  chip->last_volts = -1.0f;
  chip->framebuffer = framebuffer_init(&chip->width, &chip->height);

  update(chip);

  // Pick up slider changes.
  const timer_config_t timer_config = {
    .user_data = chip,
    .callback = update,
  };

  timer_t timer = timer_init(&timer_config);
  timer_start(timer, 100000, true);
}
