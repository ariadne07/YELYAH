#ifndef BATTERY_H
#define BATTERY_H

#include <stdbool.h>

/*
 * Battery monitor: reads the LiPo voltage through the
 * 100k/100k divider on BATTERY_SENSE.
 */

#define BATTERY_LOW_MV      3400    // warn the user
#define BATTERY_CRITICAL_MV 3200    // shut down before the cell's protection cuts in
#define BATTERY_MIN_VALID_MV 2500   // anything lower is treated as a bad reading

void battery_init(void);

/*
 * Battery voltage in millivolts (averaged), or 0 if the
 * ADC isn't available or the reading is implausible.
 */
int battery_read_mv(void);

/*
 * Rough state of charge, 0-100, from a typical LiPo
 * discharge curve.
 */
int battery_percent(int millivolts);

#endif
