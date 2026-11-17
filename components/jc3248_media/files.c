#include "files.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "board.h"
#include "fs_gate.h"
#include "log_buffer.h"
#include "usb_msc.h"

static const char *TAG = "files";

#define SCAN_DEPTH 4

static char (*s_paths)[FILE_PATH_MAX];
static int s_count;
static uint32_t s_generation;
static uint32_t s_hash;
static bool s_has_scanned;
static SemaphoreHandle_t s_mutex;

static bool ensure_ready(void)
{
    if (!s_mutex) {
        s_mutex = xSemaphoreCreateMutex();
    }
    if (!s_paths) {
        s_paths = heap_caps_malloc((size_t)FILES_MAX * FILE_PATH_MAX,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_paths) {
            ESP_LOGE(TAG, "no PSRAM for the playlist (%d entries)", FILES_MAX);
        }
    }
    return s_mutex && s_paths;
}

bool files_is_playable(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) {
        return false;
    }
    return !strcasecmp(dot, ".mjpeg") || !strcasecmp(dot, ".mjpg") ||
           !strcasecmp(dot, ".mjpe") || !strcasecmp(dot, ".avi") ||
           !strcasecmp(dot, ".jpg") || !strcasecmp(dot, ".jpeg");
}

static int entry_cmp(const void *a, const void *b)
{
    return strcasecmp((const char *)a, (const char *)b);
}

static void scan_dir(const char *rel, int depth)
{
    if (depth > SCAN_DEPTH || s_count >= FILES_MAX) {
        return;
    }
    char abs[FILE_PATH_MAX];
    if (snprintf(abs, sizeof(abs), "%s%s", SD_MOUNT_POINT, rel) >= (int)sizeof(abs)) {
        return;
    }
    DIR *dir = opendir(abs);
    if (!dir) {
        return;
    }

    struct dirent *de;
    while ((de = readdir(dir)) != NULL && s_count < FILES_MAX) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
            continue;
        }
        size_t a = strlen(rel);
        size_t b = strlen(de->d_name);
        if (a + 1 + b + 1 > FILE_PATH_MAX) {
            continue;
        }
        char child_rel[FILE_PATH_MAX];
        memcpy(child_rel, rel, a);
        child_rel[a] = '/';
        memcpy(child_rel + a + 1, de->d_name, b + 1);

        char child_abs[FILE_PATH_MAX];
        if (snprintf(child_abs, sizeof(child_abs), "%s%s", SD_MOUNT_POINT, child_rel) >=
            (int)sizeof(child_abs)) {
            continue;
        }
        struct stat st;
        if (stat(child_abs, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            scan_dir(child_rel, depth + 1);
        } else if (S_ISREG(st.st_mode) && files_is_playable(de->d_name)) {
            snprintf(s_paths[s_count], FILE_PATH_MAX, "%s", child_rel);
            s_count++;
        }
    }
    closedir(dir);
}

static uint32_t list_hash(void)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < s_count; i++) {
        for (const unsigned char *p = (const unsigned char *)s_paths[i]; *p; p++) {
            h = (h ^ *p) * 16777619u;
        }
        h = (h ^ '\n') * 16777619u;
    }
    return h;
}

int files_scan(void)
{
    if (!ensure_ready()) {
        return 0;
    }
    if (usb_msc_host_active() || fs_gate_suspended()) {
        return files_count();
    }
    if (!fs_gate_lock(5000)) {
        ESP_LOGW(TAG, "filesystem busy, scan skipped");
        return files_count();
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_count = 0;
    scan_dir("", 0);
    qsort(s_paths, s_count, FILE_PATH_MAX, entry_cmp);

    uint32_t hash = list_hash();
    if (!s_has_scanned || hash != s_hash) {
        s_hash = hash;
        s_generation++;
        s_has_scanned = true;
    }
    int count = s_count;
    xSemaphoreGive(s_mutex);
    fs_gate_unlock();

    ESP_LOGI(TAG, "%d playable file(s), generation %u", count, (unsigned)s_generation);
    log_buffer_addf("[files] %d playable file(s), generation %u", count,
                    (unsigned)s_generation);
    return count;
}

int files_count(void)
{
    return s_count;
}

const char *files_path(int index)
{
    if (index < 0 || index >= s_count) {
        return NULL;
    }
    return s_paths[index];
}

bool files_delete(const char *path)
{
    if (!path || path[0] != '/' || strstr(path, "..") != NULL) {
        return false;
    }
    if (usb_msc_host_active() || fs_gate_suspended()) {
        return false;
    }

    const char *rel = path;
    size_t mount_len = strlen(SD_MOUNT_POINT);
    if (!strncmp(path, SD_MOUNT_POINT, mount_len)) {
        rel = path + mount_len;
        if (rel[0] == '\0') {
            return false;
        }
    }
    char abs[FILE_PATH_MAX];
    if (snprintf(abs, sizeof(abs), "%s%s", SD_MOUNT_POINT, rel) >= (int)sizeof(abs)) {
        return false;
    }
    if (!fs_gate_lock(5000)) {
        ESP_LOGW(TAG, "filesystem busy, delete of %s skipped", rel);
        log_buffer_addf("[files] delete %s failed: filesystem busy", rel);
        return false;
    }

    struct stat st;
    bool ok = stat(abs, &st) == 0 && S_ISREG(st.st_mode) && remove(abs) == 0;
    int err = errno;
    fs_gate_unlock();

    if (ok) {
        ESP_LOGI(TAG, "deleted %s", rel);
        log_buffer_addf("[files] deleted %s", rel);
        files_scan();
    } else {
        ESP_LOGW(TAG, "delete %s failed: %s", rel, strerror(err));
        log_buffer_addf("[files] delete %s failed: %s", rel, strerror(err));
    }
    return ok;
}

int files_index_of(const char *path)
{
    if (!path || !path[0]) {
        return -1;
    }
    for (int i = 0; i < s_count; i++) {
        if (!strcmp(s_paths[i], path)) {
            return i;
        }
    }
    return -1;
}

uint32_t files_generation(void)
{
    return s_generation;
}

bool files_lock(uint32_t timeout_ms)
{
    if (!ensure_ready()) {
        return false;
    }
    return xSemaphoreTake(s_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void files_unlock(void)
{
    if (s_mutex) {
        xSemaphoreGive(s_mutex);
    }
}

int files_snapshot(char *buf, size_t cap)
{
    if (!buf || cap == 0 || !ensure_ready()) {
        return -1;
    }
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return -1;
    }
    size_t used = 0;
    int written = 0;
    for (int i = 0; i < s_count; i++) {
        size_t len = strlen(s_paths[i]);
        size_t need = len + (i ? 1 : 0);
        if (used + need + 1 > cap) {
            written = -1;
            break;
        }
        if (i) {
            buf[used++] = '\n';
        }
        memcpy(buf + used, s_paths[i], len);
        used += len;
        written++;
    }
    buf[used < cap ? used : cap - 1] = '\0';
    xSemaphoreGive(s_mutex);
    return written;
}
