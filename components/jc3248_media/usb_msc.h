#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "sdmmc_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

// Called from the TinyUSB task just before FatFS is detached from the card.
// The application must close any open files before returning.
typedef void (*usb_msc_prepare_cb_t)(void);

// Create the MSC storage object backed by the already-initialized SD card,
// mount it for the application, and start TinyUSB. When a host is connected,
// the storage is automatically detached from FatFS and exposed as a drive.
esp_err_t usb_msc_start(sdmmc_card_t *card, usb_msc_prepare_cb_t prepare);

// True while the USB host owns the card (FatFS is unmounted).
bool usb_msc_host_active(void);

// True when a card filesystem could be mounted for the application.
bool usb_msc_fs_ready(void);

// True if the card has no readable filesystem.
bool usb_msc_format_required(void);

// Create a FAT32 filesystem on the card. Only valid while the application owns
// the card (the USB host must not be connected). Destroys all data.
esp_err_t usb_msc_format(void);

// Recreate the whole FAT32 volume even when a filesystem is already mounted:
// writes a fresh MBR, both FATs and the root directory, so every old file and
// directory entry is gone. Only valid while the application owns the card.
esp_err_t usb_msc_format_full(void);

#ifdef __cplusplus
}
#endif
