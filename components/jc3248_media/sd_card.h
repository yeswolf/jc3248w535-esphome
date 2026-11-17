#pragma once

#include "esp_err.h"
#include "sdmmc_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

// Bring up the TF slot on SPI3 (the panel owns SPI2) and initialize the card.
// The returned card stays valid for the lifetime of the firmware; FatFS on top
// of it is managed by the TinyUSB MSC storage object.
esp_err_t sd_card_init(sdmmc_card_t **out_card);

#ifdef __cplusplus
}
#endif
