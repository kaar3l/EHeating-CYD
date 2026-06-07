#pragma once
#include <stdbool.h>

void touch_init(void);
void touch_apply_cal(int x0, int x319, int y0, int y239);
bool touch_read(int *x, int *y);          // calibrated screen coords
bool touch_read_raw(int *rx, int *ry);    // raw ADC values (for calibration)
