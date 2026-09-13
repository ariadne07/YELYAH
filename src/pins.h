#ifndef PINS_H
#define PINS_H

#include "driver/gpio.h"

#ifdef WOKWI
#error WOKWI IS NOT DEFINED
#endif

//PCM5102A
#define DAC_BCK GPIO_NUM_2
#define DAC_WSEL GPIO_NUM_3
#define DAC_DIN GPIO_NUM_4

//LCD Display 
#define LCD_CS GPIO_NUM_1
#define LCD_DC GPIO_NUM_5
#define LCD_RST GPIO_NUM_6
#define LCD_BL GPIO_NUM_43
#define LCD_CLK GPIO_NUM_7
#define LCD_DIN GPIO_NUM_9  

//MicroSD Card
#define SD_CS GPIO_NUM_17
#define SD_CLK GPIO_NUM_7
#define SD_SO GPIO_NUM_8
#define SD_SI GPIO_NUM_9
#define SD_DETECT GPIO_NUM_38

//Buttons
#define PIN_BUTTON_LEFT GPIO_NUM_39
#define PIN_BUTTON_UP GPIO_NUM_40
#define PIN_BUTTON_DOWN GPIO_NUM_42
#define PIN_BUTTON_CENTER GPIO_NUM_41
#define PIN_BUTTON_RIGHT GPIO_NUM_10

//#endif

#ifdef SEEED_XIAO_ESP32S3

//PCM5102A
#define DAC_BCK GPIO_NUM_2
#define DAC_WSEL GPIO_NUM_3
#define DAC_DIN GPIO_NUM_4

//LCD Display 
#define LCD_CS GPIO_NUM_1
#define LCD_DC GPIO_NUM_5
#define LCD_RST GPIO_NUM_6
#define LCD_BL GPIO_NUM_43
#define LCD_CLK GPIO_NUM_7
#define LCD_DIN GPIO_NUM_9

//MicroSD Card
#define SD_CS GPIO_NUM_44
#define SD_CLK GPIO_NUM_7
#define SD_SO GPIO_NUM_8
#define SD_SI GPIO_NUM_9
#define SD_DETECT GPIO_NUM_38

//Buttons
#define PIN_BUTTON_LEFT GPIO_NUM_39
#define PIN_BUTTON_UP GPIO_NUM_40
#define PIN_BUTTON_DOWN GPIO_NUM_42
#define PIN_BUTTON_CENTER GPIO_NUM_41
#define PIN_BUTTON_RIGHT GPIO_NUM_10

#endif

#endif

