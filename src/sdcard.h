#ifndef SDCARD_H
#define SDCARD_H

#include <stdbool.h>

void sdcard_init_detect(void);

bool sdcard_is_inserted(void);
bool sdcard_is_mounted(void);

bool sdcard_init(void);
void sdcard_test(void);

#endif
