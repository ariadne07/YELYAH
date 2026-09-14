#ifndef SDCARD_H
#define SDCARD_H

#include <stdbool.h>

bool sdcard_init(void);

bool sdcard_is_mounted(void);

void sdcard_test(void);

#endif