#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Called from the TinyUSB task before FatFS is detached. Closes any file the
// browser/player still has open.
void browser_prepare_for_usb(void);

// Last-chance storage bring-up, used by the "no card" screen. Returns ESP_OK
// when the card and the MSC storage are both up.
typedef esp_err_t (*browser_storage_init_fn)(void);

// Never returns. `storage_ok` tells the UI whether it can list the card.
void browser_run(browser_storage_init_fn storage_init, bool storage_ok);

// Create the browser/player task and return. Only the first call has an
// effect; the task owns the display, the card and the decoder afterwards.
void browser_start(browser_storage_init_fn storage_init, bool storage_ok);

#ifdef __cplusplus
}
#endif
