#include "sd_card.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/sdmmc_host.h"
#include "esp_check.h"
#include "esp_log.h"
#include "sdmmc_cmd.h"

#include "board.h"

static const char *TAG = "sd";

esp_err_t sd_card_init(sdmmc_card_t **out_card)
{
    ESP_RETURN_ON_FALSE(out_card, ESP_ERR_INVALID_ARG, TAG, "out_card is NULL");
    *out_card = NULL;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SD_MAX_FREQ_KHZ;

    // JC3248W535 exposes only CLK/CMD/D0, so the slot runs in 1-bit mode.
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = SD_PIN_CLK;
    slot.cmd = SD_PIN_CMD;
    slot.d0 = SD_PIN_D0;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_err_t err = sdmmc_host_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "sdmmc host init failed: %s", esp_err_to_name(err));
        return err;
    }
    err = sdmmc_host_init_slot(host.slot, &slot);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "sdmmc slot init failed: %s", esp_err_to_name(err));
        return err;
    }

    sdmmc_card_t *card = calloc(1, sizeof(sdmmc_card_t));
    if (!card) {
        return ESP_ERR_NO_MEM;
    }
    err = sdmmc_card_init(&host, card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "card init at %d kHz failed (%s), retrying at %d", SD_MAX_FREQ_KHZ,
                 esp_err_to_name(err), SD_FALLBACK_FREQ_KHZ);
        host.max_freq_khz = SD_FALLBACK_FREQ_KHZ;
        memset(card, 0, sizeof(*card));
        err = sdmmc_card_init(&host, card);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "card init failed (no card?): %s", esp_err_to_name(err));
        free(card);
        return err;
    }

    sdmmc_card_print_info(stdout, card);
    *out_card = card;
    return ESP_OK;
}
