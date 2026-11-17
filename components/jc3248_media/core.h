#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Bring up the display, touch, SD card and USB mass-storage device, run the
// panel self-test and scan the card. Safe to call more than once.
void jc3248_core_init(void);

// Start the browser/player task. The task owns the display, the card and the
// decoder from this point on.
void jc3248_core_start_browser(void);

// True when the card and the USB MSC storage were brought up successfully.
bool jc3248_core_storage_ready(void);

#ifdef __cplusplus
}
#endif
