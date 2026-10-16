#ifndef STORAGE_H
#define STORAGE_H

#include <stdbool.h>

#define STORAGE_MOUNT_POINT "/sdcard"

/*
 * Mounts the SD card (FAT) on the SPI bus that
 * display_init() already set up, so call this after it.
 *
 * Returns true if the card is mounted.
 */
bool storage_init(void);

bool storage_is_mounted(void);

#endif
