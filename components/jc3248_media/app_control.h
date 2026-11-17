#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

// Commands arrive from Home Assistant (through the ESPHome component) and are
// consumed by the browser task, which owns the SD card, the decoder and the
// display.
typedef enum {
    APP_CMD_PLAY_FILE,
    APP_CMD_PLAY_RANDOM,
    APP_CMD_PLAY_ALL,
    APP_CMD_NEXT,
    APP_CMD_PREV,
    APP_CMD_STOP,
    APP_CMD_RESCAN,
    APP_CMD_SHOW_BROWSER,
    APP_CMD_SET_COLOR,
    APP_CMD_SLEEP,
    APP_CMD_DELETE_FILE,
} app_cmd_type_t;

typedef struct {
    app_cmd_type_t type;
    char path[512];  // mount-relative path ("/clip.avi") or empty
    uint16_t color;  // RGB565, host order
    bool on;
} app_cmd_t;

esp_err_t app_control_init(void);

// Post a command from any task. Returns false when the queue is full.
bool app_control_post(const app_cmd_t *cmd, TickType_t wait);

// Take one command without blocking when wait is 0.
bool app_control_take(app_cmd_t *out, TickType_t wait);

// True when at least one command is queued. Playback polls this through
// should_stop() so a network command can interrupt a running clip.
bool app_control_pending(void);

// The browser publishes what it is playing so the ESPHome entities can mirror
// it. A NULL or empty path means playback is stopped.
void app_control_set_playing(const char *path);
bool app_control_get_playing(char *path, size_t cap);

// True while the screen is filled with a solid colour instead of showing the
// browser or a video.
void app_control_set_color_mode(bool on);
bool app_control_get_color_mode(void);

// True while the browser task is in the sleep state (screen off, no touch).
void app_control_set_sleeping(bool sleeping);
bool app_control_get_sleeping(void);

// The requested backlight level. The browser applies it when it wakes from
// sleep, so the panel and the backlight are always re-enabled from the task
// that owns the display.
void app_control_set_backlight(uint8_t percent);
uint8_t app_control_get_backlight(void);

#ifdef __cplusplus
}
#endif
