#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t touch_init(void);

// Poll the controller and return the first touch point. Coordinates are in
// display space (480x272).
bool touch_read(int *x, int *y);

// True once per new touch-down (a fresh tap after the finger was lifted).
bool touch_tapped(int *x, int *y);

#ifdef __cplusplus
}
#endif
