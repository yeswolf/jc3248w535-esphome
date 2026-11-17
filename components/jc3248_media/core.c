#include "core.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

#include "app_control.h"
#include "board.h"
#include "browser.h"
#include "display.h"
#include "files.h"
#include "fs_gate.h"
#include "log_buffer.h"
#include "sd_card.h"
#include "touch.h"
#include "usb_msc.h"

static const char *TAG = "app";

static sdmmc_card_t *s_card;
static bool s_started;
static bool s_storage_ok;

// The on-board NS4168 amplifier is always powered and has no enable pin. Park
// its I2S inputs low: this firmware never plays audio, and floating lines next
// to the QSPI display pick up enough noise to make the speaker hiss.
static void silence_onboard_amp(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << GPIO_NUM_2) | (1ULL << GPIO_NUM_41) | (1ULL << GPIO_NUM_42),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&cfg);
    gpio_set_level(GPIO_NUM_2, 0);
    gpio_set_level(GPIO_NUM_41, 0);
    gpio_set_level(GPIO_NUM_42, 0);
}

static esp_err_t storage_init(void)
{
    if (!s_card) {
        esp_err_t err = sd_card_init(&s_card);
        if (err != ESP_OK) {
            return err;
        }
    }
    return usb_msc_start(s_card, browser_prepare_for_usb);
}

// Eight solid bars are the quickest way to tell a wiring/transfer problem
// from a UI problem: the bars must be clean, vertical and reach the bottom
// edge of the glass.
static void display_test_pattern(void)
{
    static const uint16_t bars[8] = {
        0xF800, 0x07E0, 0x001F, 0xFFFF, 0xFFE0, 0x07FF, 0xF81F, 0x0000,
    };
    for (int i = 0; i < 8; i++) {
        display_fill_rect(i * (LCD_WIDTH / 8), 0, LCD_WIDTH / 8, LCD_HEIGHT, bars[i]);
    }
    display_text(8, 8, "DISPLAY TEST", COL_WHITE, COL_BLACK, true);
    display_flush();
    vTaskDelay(pdMS_TO_TICKS(1200));
}

void jc3248_core_init(void)
{
    if (s_started) {
        return;
    }
    s_started = true;

    ESP_LOGI(TAG, "booting JC3248W535 media player");
    app_control_init();
    fs_gate_init();
    log_buffer_init();

    silence_onboard_amp();
    ESP_ERROR_CHECK(display_init());
    display_test_pattern();
    display_clear(COL_BG);
    display_text_scaled(24, 70, "ESP32-S3  JC3248W535", COL_WHITE, 0, 1);
    display_text(24, 120, "SD card | file browser | MJPEG", COL_DIM, 0, false);
    display_text(24, 150, "Starting...", COL_ACCENT, 0, false);
    display_text(24, 180, "decoder: esp_new_jpeg / O2", COL_DIM, 0, false);
    display_flush();

    esp_err_t terr = touch_init();
    if (terr != ESP_OK) {
        ESP_LOGW(TAG, "touch unavailable: %s", esp_err_to_name(terr));
    }

    esp_err_t serr = storage_init();
    s_storage_ok = (serr == ESP_OK);
    if (serr != ESP_OK) {
        ESP_LOGE(TAG, "storage unavailable: %s", esp_err_to_name(serr));
    } else if (usb_msc_fs_ready()) {
        // tinyusb_msc_set_storage_mount_point() returns before the mount
        // completes in the TinyUSB task. If the card is already mounted, scan
        // now; otherwise the mount callback queues APP_CMD_RESCAN and the
        // browser task builds the playlist as soon as the card is ready.
        files_scan();
    }
}

void jc3248_core_start_browser(void)
{
    browser_start(storage_init, s_storage_ok);
}

bool jc3248_core_storage_ready(void)
{
    return s_storage_ok;
}
