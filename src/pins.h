#ifndef PINS_H
#define PINS_H

#include "driver/gpio.h"

/*
 * Pin map for the Seeed XIAO ESP32-S3 Plus.
 *
 * The Wokwi simulation (diagram.json) is wired to these
 * same GPIOs, so one map serves both builds. The only
 * difference between the builds is audio output (see
 * audio_out.c).
 *
 * XIAO pad labels are in brackets.
 */

// Display: Adafruit 1.54" 240x240 ST7789 (Wokwi stand-in: ILI9341)
#define LCD_CS  GPIO_NUM_1      // TC   [D0]
#define LCD_DC  GPIO_NUM_2      // DC   [D1]
#define LCD_RST GPIO_NUM_5      // RT   [D4]
#define LCD_BL  GPIO_NUM_6      // BL   [D5]

// Shared SPI bus: display + its microSD slot
#define LCD_CLK GPIO_NUM_13     // CL   [D17, SCK_1]
#define LCD_DIN GPIO_NUM_11     // SI   [D19, MOSI_1]
#define SD_SO   GPIO_NUM_12     // SO   [D18, MISO_1]

#define SD_CLK  LCD_CLK
#define SD_SI   LCD_DIN

// MicroSD slot on the display board
#define SD_CS   GPIO_NUM_4      // CC   [D3]

// PCM5102 DAC
#define DAC_DIN  GPIO_NUM_38    // DIN  [D11]
#define DAC_BCK  GPIO_NUM_39    // BCK  [D12]
#define DAC_WSEL GPIO_NUM_40    // WSEL [D13]
#define DAC_MUTE GPIO_NUM_3     // MU   [D2], low = muted

// Buttons (other leg to GND)
#define PIN_BUTTON_UP     GPIO_NUM_7    // SW0 [D8]
#define PIN_BUTTON_DOWN   GPIO_NUM_8    // SW1 [D9]
#define PIN_BUTTON_LEFT   GPIO_NUM_9    // SW2 [D10]
#define PIN_BUTTON_RIGHT  GPIO_NUM_41   // SW3 [D14]
#define PIN_BUTTON_CENTER GPIO_NUM_42   // SW4 [D15]

// Battery voltage through a 100k/100k divider
#define BATTERY_SENSE GPIO_NUM_10       // [D16, ADC]
#define BATTERY_DIVIDER_RATIO 2

#endif
