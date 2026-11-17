#include "app_control.h"

#include <stdio.h>
#include <string.h>

#include "freertos/queue.h"
#include "freertos/semphr.h"

#define CMD_QUEUE_LEN 8
#define MEDIA_PATH_MAX 512

static QueueHandle_t s_queue;
static SemaphoreHandle_t s_media_mutex;
static char s_playing[MEDIA_PATH_MAX];
static volatile bool s_color_mode;
static volatile bool s_sleeping;
static volatile uint8_t s_backlight = 100;

esp_err_t app_control_init(void)
{
    if (!s_queue) {
        s_queue = xQueueCreate(CMD_QUEUE_LEN, sizeof(app_cmd_t));
    }
    if (!s_media_mutex) {
        s_media_mutex = xSemaphoreCreateMutex();
    }
    return (s_queue && s_media_mutex) ? ESP_OK : ESP_ERR_NO_MEM;
}

bool app_control_post(const app_cmd_t *cmd, TickType_t wait)
{
    if (!s_queue || !cmd) {
        return false;
    }
    return xQueueSend(s_queue, cmd, wait) == pdTRUE;
}

bool app_control_take(app_cmd_t *out, TickType_t wait)
{
    if (!s_queue || !out) {
        return false;
    }
    return xQueueReceive(s_queue, out, wait) == pdTRUE;
}

bool app_control_pending(void)
{
    return s_queue && uxQueueMessagesWaiting(s_queue) > 0;
}

void app_control_set_playing(const char *path)
{
    if (!s_media_mutex) {
        return;
    }
    xSemaphoreTake(s_media_mutex, portMAX_DELAY);
    if (path) {
        snprintf(s_playing, sizeof(s_playing), "%s", path);
    } else {
        s_playing[0] = '\0';
    }
    xSemaphoreGive(s_media_mutex);
}

bool app_control_get_playing(char *path, size_t cap)
{
    bool playing = false;
    if (!s_media_mutex) {
        if (path && cap) {
            path[0] = '\0';
        }
        return false;
    }
    xSemaphoreTake(s_media_mutex, portMAX_DELAY);
    playing = s_playing[0] != '\0';
    if (path && cap) {
        snprintf(path, cap, "%s", s_playing);
    }
    xSemaphoreGive(s_media_mutex);
    return playing;
}

void app_control_set_color_mode(bool on)
{
    s_color_mode = on;
}

bool app_control_get_color_mode(void)
{
    return s_color_mode;
}

void app_control_set_sleeping(bool sleeping)
{
    s_sleeping = sleeping;
}

bool app_control_get_sleeping(void)
{
    return s_sleeping;
}

void app_control_set_backlight(uint8_t percent)
{
    s_backlight = percent > 100 ? 100 : percent;
}

uint8_t app_control_get_backlight(void)
{
    return s_backlight;
}
