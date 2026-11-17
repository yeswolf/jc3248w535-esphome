#include "log_buffer.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "logbuf";

#define LOG_LINES 160
#define LOG_LINE_MAX 220

typedef struct {
    uint32_t seq;
    uint16_t len;
    char text[LOG_LINE_MAX];
} log_entry_t;

static log_entry_t *s_entries;
static SemaphoreHandle_t s_lock;
static uint32_t s_seq;

void log_buffer_init(void)
{
    if (s_entries != NULL) {
        return;
    }
    s_entries = heap_caps_calloc(LOG_LINES, sizeof(log_entry_t),
                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_entries == NULL) {
        // PSRAM may not be up yet when this runs on the native boot path.
        s_entries = heap_caps_calloc(LOG_LINES, sizeof(log_entry_t), MALLOC_CAP_8BIT);
    }
    if (s_entries == NULL) {
        ESP_LOGE(TAG, "no memory for the log ring");
        return;
    }
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        ESP_LOGE(TAG, "no mutex for the log ring");
        heap_caps_free(s_entries);
        s_entries = NULL;
    }
}

void log_buffer_addf(const char *format, ...)
{
    char line[LOG_LINE_MAX];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    log_buffer_add(line);
}

void log_buffer_add(const char *line)
{
    if (line == NULL || s_entries == NULL || s_lock == NULL) {
        return;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(20)) != pdTRUE) {
        return;
    }
    uint32_t seq = ++s_seq;
    log_entry_t *entry = &s_entries[(seq - 1) % LOG_LINES];
    size_t len = strlen(line);
    if (len >= LOG_LINE_MAX) {
        len = LOG_LINE_MAX - 1;
    }
    memcpy(entry->text, line, len);
    entry->text[len] = '\0';
    entry->len = (uint16_t)len;
    entry->seq = seq;
    xSemaphoreGive(s_lock);
}

size_t log_buffer_read(uint32_t since, char *out, size_t out_size, uint32_t *next_seq)
{
    if (out == NULL || out_size == 0) {
        if (next_seq != NULL) {
            *next_seq = since;
        }
        return 0;
    }
    size_t used = 0;
    uint32_t last = since;
    if (s_entries == NULL || s_lock == NULL) {
        if (next_seq != NULL) {
            *next_seq = since;
        }
        return 0;
    }
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        if (next_seq != NULL) {
            *next_seq = since;
        }
        return 0;
    }

    uint32_t first = s_seq > LOG_LINES ? s_seq - LOG_LINES + 1 : 1;
    if (since + 1 < first) {
        since = first - 1;
    }
    for (uint32_t seq = since + 1; seq <= s_seq; seq++) {
        log_entry_t *entry = &s_entries[(seq - 1) % LOG_LINES];
        if (entry->seq != seq) {
            continue;
        }
        if (used + entry->len + 2 > out_size) {
            break;
        }
        memcpy(out + used, entry->text, entry->len);
        used += entry->len;
        out[used++] = '\n';
        last = seq;
    }
    xSemaphoreGive(s_lock);

    if (next_seq != NULL) {
        *next_seq = last;
    }
    return used;
}
