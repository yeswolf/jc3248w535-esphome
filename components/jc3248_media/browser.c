#include "browser.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "app_control.h"
#include "board.h"
#include "display.h"
#include "files.h"
#include "font_8x16.h"
#include "fs_gate.h"
#include "log_buffer.h"
#include "mjpeg.h"
#include "touch.h"
#include "usb_msc.h"

static const char *TAG = "browser";

#define VISIBLE_ROWS 7
#define ROW_H 38
#define HEADER_H 34
#define FOOTER_Y (HEADER_H + VISIBLE_ROWS * ROW_H)  // 300
#define MAX_ENTRIES 512
#define MAX_NAME 128
#define STILL_HOLD_MS 10000
#define SLIDESHOW_FADE_OUT_MS 700
#define SLIDESHOW_FADE_IN_MS 900

typedef struct {
    char name[MAX_NAME];
    uint32_t size;
    bool is_dir;
} entry_t;

static entry_t *s_entries;
static int s_count;
static char s_cwd[256] = SD_MOUNT_POINT;
static int s_page;
static bool s_dirty = true;

static FILE *s_play_file;

static bool s_usb_screen;
static bool s_error_screen;
static bool s_confirm_format;
static char s_error[96];

static char s_status[96];
static int64_t s_status_until;

// Long-press delete confirmation.
static bool s_confirm_delete;
static char s_delete_rel[FILE_PATH_MAX];
static char s_delete_name[MAX_NAME];

// Playback/slide-show state shared with Home Assistant.
static bool s_sleeping;
static bool s_slideshow;
static bool s_color_mode;
static uint16_t s_fill_color;
static int s_current_index = -1;
static char s_current_path[FILE_PATH_MAX];

static browser_storage_init_fn s_boot_storage_init;
static bool s_boot_storage_ok;

// Bounded dir/name join. Avoids snprintf truncation warnings (and silently
// cutting a path in half) when both parts are near their maximum length.
static bool join_path(char *out, size_t cap, const char *dir, const char *name)
{
    size_t a = strlen(dir);
    size_t b = strlen(name);
    if (a + 1 + b + 1 > cap) {
        return false;
    }
    memcpy(out, dir, a);
    out[a] = '/';
    memcpy(out + a + 1, name, b + 1);
    return true;
}

// Accept both "/sdcard/foo.avi" and the playlist form "/foo.avi".
static void normalize_path(const char *in, char *rel, size_t rel_cap, char *abs, size_t abs_cap)
{
    size_t mount_len = strlen(SD_MOUNT_POINT);
    if (!strncmp(in, SD_MOUNT_POINT, mount_len)) {
        snprintf(abs, abs_cap, "%s", in);
        snprintf(rel, rel_cap, "%s", in + mount_len);
        if (rel[0] == '\0') {
            snprintf(rel, rel_cap, "/");
        }
    } else {
        snprintf(rel, rel_cap, "%s", in);
        snprintf(abs, abs_cap, "%s%s", SD_MOUNT_POINT, in);
    }
}

void browser_prepare_for_usb(void)
{
    fs_gate_set_suspend(true);
    app_control_set_playing(NULL);
    if (!fs_gate_lock(3000)) {
        ESP_LOGW(TAG, "FS busy, proceeding with detach anyway");
        return;
    }
    if (s_play_file) {
        fclose(s_play_file);
        s_play_file = NULL;
    }
    fs_gate_unlock();
}

static void set_status(const char *msg)
{
    snprintf(s_status, sizeof(s_status), "%s", msg);
    s_status_until = esp_timer_get_time() + 2500000;
}

static void set_error(const char *msg)
{
    snprintf(s_error, sizeof(s_error), "%s", msg);
    s_error_screen = true;
    s_dirty = true;
}

static void fmt_size(uint32_t bytes, char *out, size_t outlen)
{
    if (bytes >= 1024 * 1024) {
        snprintf(out, outlen, "%.1fM", bytes / (1024.0 * 1024.0));
    } else if (bytes >= 1024) {
        snprintf(out, outlen, "%.0fK", bytes / 1024.0);
    } else {
        snprintf(out, outlen, "%uB", (unsigned)bytes);
    }
}

static int entry_cmp(const void *a, const void *b)
{
    const entry_t *ea = a;
    const entry_t *eb = b;
    if (ea->is_dir != eb->is_dir) {
        return ea->is_dir ? -1 : 1;
    }
    return strcasecmp(ea->name, eb->name);
}

static void reload_dir(void)
{
    if (!s_entries) {
        return;
    }
    s_count = 0;
    s_page = 0;
    s_error_screen = false;

    if (fs_gate_suspended() || usb_msc_host_active()) {
        return;
    }

    if (!fs_gate_lock(3000)) {
        return;
    }
    if (fs_gate_suspended()) {
        fs_gate_unlock();
        return;
    }

    DIR *dir = opendir(s_cwd);
    if (!dir) {
        char msg[80];
        snprintf(msg, sizeof(msg), "Cannot open directory (%d)", errno);
        fs_gate_unlock();
        set_error(msg);
        return;
    }

    struct dirent *de;
    while ((de = readdir(dir)) != NULL && s_count < MAX_ENTRIES) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
            continue;
        }
        char path[512];
        if (!join_path(path, sizeof(path), s_cwd, de->d_name)) {
            continue;
        }
        struct stat st;
        if (stat(path, &st) != 0) {
            continue;
        }
        entry_t *e = &s_entries[s_count];
        snprintf(e->name, sizeof(e->name), "%.*s", (int)sizeof(e->name) - 1, de->d_name);
        e->is_dir = S_ISDIR(st.st_mode);
        e->size = e->is_dir ? 0 : (uint32_t)st.st_size;
        s_count++;
    }
    closedir(dir);

    // readdir's ".." is skipped above; add an explicit parent row so the UI
    // has a way back to the root.
    if (strcmp(s_cwd, SD_MOUNT_POINT) != 0 && s_count < MAX_ENTRIES) {
        entry_t *up = &s_entries[s_count++];
        snprintf(up->name, sizeof(up->name), "..");
        up->size = 0;
        up->is_dir = true;
    }

    qsort(s_entries, s_count, sizeof(entry_t), entry_cmp);
    fs_gate_unlock();
    s_dirty = true;
}

static void go_up(void)
{
    if (strcmp(s_cwd, SD_MOUNT_POINT) == 0) {
        return;
    }
    char *slash = strrchr(s_cwd, '/');
    if (slash && slash > s_cwd + strlen(SD_MOUNT_POINT)) {
        *slash = '\0';
    } else {
        snprintf(s_cwd, sizeof(s_cwd), "%s", SD_MOUNT_POINT);
    }
    reload_dir();
}

static void go_into(const char *name)
{
    size_t need = strlen(s_cwd) + 1 + strlen(name) + 1;
    if (need > sizeof(s_cwd)) {
        set_status("Path too long");
        return;
    }
    strcat(s_cwd, "/");
    strcat(s_cwd, name);
    reload_dir();
}

static size_t play_read(void *ctx, void *buf, size_t len)
{
    (void)ctx;
    size_t got = 0;
    if (!fs_gate_lock(1000)) {
        return 0;
    }
    if (!fs_gate_suspended() && s_play_file) {
        got = fread(buf, 1, len, s_play_file);
        if (fs_gate_suspended()) {
            fclose(s_play_file);
            s_play_file = NULL;
            got = 0;
        }
    } else if (s_play_file) {
        fclose(s_play_file);
        s_play_file = NULL;
    }
    fs_gate_unlock();
    return got;
}

static int play_seek(void *ctx, long offset, int whence)
{
    (void)ctx;
    int rc = -1;
    if (!fs_gate_lock(1000)) {
        return -1;
    }
    if (!fs_gate_suspended() && s_play_file) {
        rc = fseek(s_play_file, offset, whence);
    }
    fs_gate_unlock();
    return rc;
}

static bool play_should_stop(void *ctx)
{
    (void)ctx;
    if (fs_gate_suspended() || usb_msc_host_active() || s_sleeping) {
        return true;
    }
    if (app_control_pending()) {
        return true;
    }
    int x = 0, y = 0;
    if (touch_tapped(&x, &y)) {
        // The fps banner is gone, so every tap stops playback.
        return true;
    }
    return false;
}

static void leave_color_mode(void)
{
    if (s_color_mode) {
        s_color_mode = false;
        app_control_set_color_mode(false);
        s_dirty = true;
    }
}

static void restore_backlight(void)
{
    if (!s_sleeping) {
        display_set_backlight_percent(app_control_get_backlight());
    }
}

// Fade the panel to black before moving to the next clip, but stay responsive
// to network commands and USB takeover.
static bool slideshow_fade_out(void)
{
    display_fade_backlight(0, SLIDESHOW_FADE_OUT_MS);
    for (int i = 0; i < SLIDESHOW_FADE_OUT_MS / 50; i++) {
        if (app_control_pending() || s_sleeping || fs_gate_suspended() ||
            usb_msc_host_active()) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return true;
}

static void slideshow_first_frame(void *ctx)
{
    (void)ctx;
    if (s_slideshow) {
        display_fade_backlight(app_control_get_backlight(), SLIDESHOW_FADE_IN_MS);
    }
}

// Play one clip. `loop` true keeps the current single-file behaviour; false
// makes mjpeg_play() return 1 at the natural end (used by the slideshow).
// Returns 0 when interrupted, 1 at the natural end, negative on error.
static int play_abs(const char *in_path, bool loop, bool show_opening)
{
    char rel[FILE_PATH_MAX];
    char path[FILE_PATH_MAX];
    normalize_path(in_path, rel, sizeof(rel), path, sizeof(path));
    if (!s_slideshow) {
        restore_backlight();
    }

    if (show_opening) {
        display_clear(COL_BLACK);
        display_text(8, 128, "Opening stream...", COL_WHITE, COL_BLACK, true);
        display_flush();
    }

    if (!fs_gate_lock(3000)) {
        set_status("Card busy");
        return -1;
    }
    if (fs_gate_suspended() || usb_msc_host_active()) {
        fs_gate_unlock();
        return 0;
    }
    if (s_play_file) {
        fclose(s_play_file);
        s_play_file = NULL;
    }
    s_play_file = fopen(path, "rb");
    if (!s_play_file) {
        fs_gate_unlock();
        if (show_opening) {
            set_status("Cannot open file");
        }
        return -1;
    }

    long fsize = -1;
    if (fseek(s_play_file, 0, SEEK_END) == 0) {
        fsize = ftell(s_play_file);
    }
    fseek(s_play_file, 0, SEEK_SET);

    fs_gate_unlock();

    ESP_LOGI(TAG, "playing %s (%ld bytes, %s)", path, fsize, loop ? "loop" : "once");
    snprintf(s_current_path, sizeof(s_current_path), "%s", rel);
    s_current_index = files_index_of(rel);
    app_control_set_playing(rel);

    mjpeg_io_t io = {
        .ctx = NULL,
        .read = play_read,
        .seek = play_seek,
        .should_stop = play_should_stop,
        .on_first_frame = s_slideshow ? slideshow_first_frame : NULL,
        .loop = loop,
    };
    int rc = mjpeg_play(&io, NULL);

    // An upload may hold the gate for the whole body; waiting here keeps the
    // FILE handle from leaking while the browser task has nothing else to do.
    if (!fs_gate_lock(15000)) {
        ESP_LOGW(TAG, "could not close the clip, filesystem stayed busy");
    } else {
        if (s_play_file) {
            fclose(s_play_file);
            s_play_file = NULL;
        }
        fs_gate_unlock();
    }

    app_control_set_playing(NULL);
    if (rc < 0 && !usb_msc_host_active()) {
        const char *detail = mjpeg_last_error();
        if (detail && detail[0]) {
            set_status(detail);
        } else if (rc == -2) {
            set_status("MJPEG frames do not decode");
        } else if (rc == -3) {
            set_status("MJPEG 480x320 required");
        } else {
            set_status("No MJPEG frames in file");
        }
    }
    s_dirty = true;
    return rc;
}

static bool path_is_still(const char *path)
{
    const char *dot = strrchr(path, '.');
    return dot && (!strcasecmp(dot, ".jpg") || !strcasecmp(dot, ".jpeg"));
}

// Hold a still on screen long enough to be seen, while still reacting to
// network commands and touches.
static bool wait_still_hold(void)
{
    for (int i = 0; i < STILL_HOLD_MS / 100; i++) {
        if (app_control_pending() || s_sleeping || fs_gate_suspended() ||
            usb_msc_host_active()) {
            return false;
        }
        int x = 0, y = 0;
        if (touch_tapped(&x, &y)) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return true;
}

static void play_slideshow(const char *start)
{
    int count = files_count();
    if (count <= 0) {
        set_status("No files for slideshow");
        s_dirty = true;
        return;
    }

    int idx = files_index_of(start);
    if (idx < 0) {
        idx = 0;
    }
    int errors = 0;
    s_slideshow = true;
    leave_color_mode();
    if (!slideshow_fade_out()) {
        s_slideshow = false;
        restore_backlight();
        s_dirty = true;
        return;
    }

    while (s_slideshow && !s_sleeping && !fs_gate_suspended() && !usb_msc_host_active()) {
        char path[FILE_PATH_MAX];
        if (!files_lock(1000)) {
            break;
        }
        const char *p = files_path(idx);
        int total = files_count();
        if (!p || total <= 0) {
            files_unlock();
            break;
        }
        snprintf(path, sizeof(path), "%s", p);
        files_unlock();

        int rc = play_abs(path, false, false);
        if (rc == 0) {
            break;
        }
        if (app_control_pending()) {
            break;
        }
        if (rc < 0) {
            errors++;
            if (errors >= total) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(300));
        } else {
            errors = 0;
        }
        if (path_is_still(path)) {
            log_buffer_addf("[slideshow] still %s, hold %d ms", path, STILL_HOLD_MS);
            if (!wait_still_hold()) {
                break;
            }
        }
        if (!slideshow_fade_out()) {
            break;
        }
        idx = (idx + 1) % total;
    }

    s_slideshow = false;
    app_control_set_playing(NULL);
    restore_backlight();
    s_dirty = true;
}

static void play_file(const char *name)
{
    char path[FILE_PATH_MAX];
    if (!join_path(path, sizeof(path), s_cwd, name)) {
        set_status("Path too long");
        return;
    }
    s_slideshow = false;
    leave_color_mode();
    play_abs(path, true, true);
}

static void play_next(bool forward)
{
    int count = files_count();
    if (count <= 0) {
        set_status("No files");
        s_dirty = true;
        return;
    }
    int idx = s_current_index;
    if (idx < 0) {
        idx = forward ? -1 : 0;
    }
    idx = forward ? (idx + 1) % count : (idx - 1 + count) % count;

    char path[FILE_PATH_MAX] = "";
    if (files_lock(1000)) {
        const char *p = files_path(idx);
        if (p) {
            snprintf(path, sizeof(path), "%s", p);
        }
        files_unlock();
    }
    if (!path[0]) {
        return;
    }
    s_slideshow = false;
    leave_color_mode();
    play_abs(path, true, true);
}

static const char *command_name(app_cmd_type_t type)
{
    switch (type) {
    case APP_CMD_PLAY_FILE:
        return "PLAY_FILE";
    case APP_CMD_PLAY_RANDOM:
        return "PLAY_RANDOM";
    case APP_CMD_PLAY_ALL:
        return "PLAY_ALL";
    case APP_CMD_NEXT:
        return "NEXT";
    case APP_CMD_PREV:
        return "PREV";
    case APP_CMD_STOP:
        return "STOP";
    case APP_CMD_RESCAN:
        return "RESCAN";
    case APP_CMD_SHOW_BROWSER:
        return "SHOW_BROWSER";
    case APP_CMD_SET_COLOR:
        return "SET_COLOR";
    case APP_CMD_SLEEP:
        return "SLEEP";
    case APP_CMD_DELETE_FILE:
        return "DELETE_FILE";
    default:
        return "UNKNOWN";
    }
}

static bool delete_file_now(const char *rel);

static void handle_command(const app_cmd_t *cmd)
{
    log_buffer_addf("[browser] command %s (type=%d) on=%d path=%s color=0x%04x",
                    command_name(cmd->type), (int)cmd->type, cmd->on ? 1 : 0,
                    cmd->path[0] ? cmd->path : "-", (unsigned)cmd->color);
    ESP_LOGI(TAG, "command %s (type=%d) on=%d path=%s color=0x%04x",
             command_name(cmd->type), (int)cmd->type, cmd->on ? 1 : 0,
             cmd->path[0] ? cmd->path : "-", (unsigned)cmd->color);
    switch (cmd->type) {
    case APP_CMD_PLAY_FILE:
        if (cmd->path[0]) {
            s_slideshow = false;
            leave_color_mode();
            play_abs(cmd->path, true, true);
        }
        break;

    case APP_CMD_PLAY_ALL:
        s_slideshow = false;
        play_slideshow(cmd->path);
        break;

    case APP_CMD_NEXT:
        play_next(true);
        break;

    case APP_CMD_PREV:
        play_next(false);
        break;

    case APP_CMD_STOP:
        app_control_set_playing(NULL);
        s_slideshow = false;
        restore_backlight();
        s_dirty = true;
        break;

    case APP_CMD_RESCAN:
        s_confirm_delete = false;
        files_scan();
        reload_dir();
        set_status("List refreshed");
        restore_backlight();
        s_dirty = true;
        break;

    case APP_CMD_DELETE_FILE:
        s_confirm_delete = false;
        if (cmd->path[0]) {
            delete_file_now(cmd->path);
        }
        break;

    case APP_CMD_SHOW_BROWSER:
        leave_color_mode();
        restore_backlight();
        s_dirty = true;
        break;

    case APP_CMD_SET_COLOR:
        s_slideshow = false;
        if (cmd->on) {
            app_control_set_playing(NULL);
            s_color_mode = true;
            s_fill_color = cmd->color;
        } else {
            s_color_mode = false;
        }
        app_control_set_color_mode(s_color_mode);
        restore_backlight();
        s_dirty = true;
        break;

    case APP_CMD_SLEEP:
        s_confirm_delete = false;
        s_sleeping = cmd->on;
        log_buffer_addf("[browser] sleep %s: backlight %s",
                        s_sleeping ? "on" : "off",
                        s_sleeping ? "0%" : "restored");
        app_control_set_sleeping(s_sleeping);
        if (cmd->on) {
            s_slideshow = false;
            app_control_set_playing(NULL);
            display_set_backlight_percent(0);
            s_dirty = false;
        } else {
            // The panel was never switched off, so only the backlight has to
            // come back. This avoids the unreliable DISPOFF/DISPON wake path.
            display_set_backlight_percent(app_control_get_backlight());
            s_dirty = true;
        }
        break;

    default:
        break;
    }
}

static void copy_utf8_chars(const char *src, char *dst, size_t dstlen, int max_chars)
{
    size_t out = 0;
    int chars = 0;
    const unsigned char *p = (const unsigned char *)src;
    while (*p && chars < max_chars && out + 4 < dstlen) {
        int n = 1;
        if ((*p & 0xE0) == 0xC0) n = 2;
        else if ((*p & 0xF0) == 0xE0) n = 3;
        else if ((*p & 0xF8) == 0xF0) n = 4;
        for (int i = 0; i < n && p[i]; i++) {
            dst[out++] = (char)p[i];
        }
        p += n;
        chars++;
    }
    if (*p && out + 4 < dstlen) {
        // U+2026 HORIZONTAL ELLIPSIS
        dst[out++] = (char)0xE2;
        dst[out++] = (char)0x80;
        dst[out++] = (char)0xA6;
    }
    dst[out] = '\0';
}

static void draw_row(int slot, const entry_t *e)
{
    int y = HEADER_H + slot * ROW_H;
    display_fill_rect(0, y, LCD_WIDTH, ROW_H, (slot & 1) ? COL_ROW_A : COL_ROW_B);

    char size[16] = "";
    if (e->is_dir) {
        snprintf(size, sizeof(size), "<DIR>");
    } else {
        fmt_size(e->size, size, sizeof(size));
    }

    uint16_t fg = COL_WHITE;
    if (e->is_dir) {
        fg = COL_DIR;
    } else if (files_is_playable(e->name)) {
        fg = COL_VIDEO;
    }

    int size_px = display_text_width(size);
    int name_max = (LCD_WIDTH - 16 - size_px - 16) / FONT_CELL_W;
    if (name_max < 1) name_max = 1;
    char shown[160];
    copy_utf8_chars(e->name, shown, sizeof(shown), name_max);
    display_text(8, y + 11, shown, fg, 0, false);
    display_text(LCD_WIDTH - 8 - size_px, y + 11, size, COL_DIM, 0, false);
}

static void draw_header(void)
{
    display_fill_rect(0, 0, LCD_WIDTH, HEADER_H, COL_HEADER);
    const char *p = s_cwd + strlen(SD_MOUNT_POINT);
    if (*p == '\0') {
        p = "/";
    }
    char shown[80];
    copy_utf8_chars(p, shown, sizeof(shown), 36);
    display_text(6, 9, shown, COL_WHITE, 0, false);

    const char *state = usb_msc_host_active() ? "USB" : (usb_msc_fs_ready() ? "SD" : "--");
    const char *fmt = "FORMAT";
    int fmt_w = display_text_width(fmt);
    int fmt_x = LCD_WIDTH - 8 - fmt_w;
    display_text(fmt_x - 14 - display_text_width(state), 9, state,
                 usb_msc_host_active() ? COL_VIDEO : COL_DIM, 0, false);
    display_fill_rect(fmt_x - 6, 5, fmt_w + 12, 24, COL_WARN);
    display_text(fmt_x, 9, fmt, COL_WHITE, 0, false);
    display_hline(0, HEADER_H - 1, LCD_WIDTH, COL_ACCENT);
}

static void draw_footer(void)
{
    int pages = (s_count + VISIBLE_ROWS - 1) / VISIBLE_ROWS;
    if (pages < 1) pages = 1;
    display_fill_rect(0, FOOTER_Y, LCD_WIDTH, LCD_HEIGHT - FOOTER_Y, COL_HEADER);
    display_hline(0, FOOTER_Y, LCD_WIDTH, COL_ACCENT);
    display_text(8, FOOTER_Y + 2, "< PAGE", s_page > 0 ? COL_WHITE : COL_DIM, 0, false);
    display_text(LCD_WIDTH - 8 - display_text_width("PAGE >"), FOOTER_Y + 2, "PAGE >",
                 s_page + 1 < pages ? COL_WHITE : COL_DIM, 0, false);
    char num[24];
    snprintf(num, sizeof(num), "%d/%d", s_page + 1, pages);
    display_text((LCD_WIDTH - display_text_width(num)) / 2, FOOTER_Y + 2, num, COL_DIM, 0,
                 false);
}

static void draw_format_confirm(void)
{
    display_clear(COL_BG);
    const char *title = "FULL FORMAT?";
    display_text_scaled((LCD_WIDTH - display_text_width(title)) / 2, 30, title, COL_WARN, 0, 1);
    display_text(20, 80, "ALL files on the card will be deleted,", COL_WHITE, 0, false);
    display_text(20, 104, "including every video, photo and folder", COL_WHITE, 0, false);
    display_text(20, 128, "and every other file and folder.", COL_WHITE, 0, false);
    display_text(20, 160, "Filesystem: FAT32. This cannot be undone.", COL_DIM, 0, false);
    display_fill_rect(16, 200, 200, 44, COL_WARN);
    const char *yes = "YES, FORMAT";
    display_text(16 + (200 - display_text_width(yes)) / 2, 214, yes, COL_WHITE, 0, false);
    display_fill_rect(264, 200, 200, 44, COL_HEADER);
    const char *no = "CANCEL";
    display_text(264 + (200 - display_text_width(no)) / 2, 214, no, COL_WHITE, 0, false);
    display_flush();
}

static void draw_delete_confirm(void)
{
    display_clear(COL_BG);
    const char *title = "DELETE FILE?";
    display_text_scaled((LCD_WIDTH - display_text_width(title)) / 2, 34, title, COL_WARN, 0, 1);
    display_text(20, 92, "The file will be deleted from the SD card:", COL_DIM, 0, false);
    char shown[40];
    snprintf(shown, sizeof(shown), "%.32s", s_delete_name);
    display_text(20, 118, shown, COL_WHITE, 0, false);
    display_text(20, 152, "This cannot be undone.", COL_DIM, 0, false);
    display_fill_rect(30, 220, 200, 48, COL_WARN);
    const char *yes = "DELETE";
    display_text(30 + (200 - display_text_width(yes)) / 2, 236, yes, COL_WHITE, 0, false);
    display_fill_rect(250, 220, 200, 48, COL_HEADER);
    const char *no = "CANCEL";
    display_text(250 + (200 - display_text_width(no)) / 2, 236, no, COL_WHITE, 0, false);
    display_flush();
}

static bool delete_file_now(const char *rel)
{
    bool ok = files_delete(rel);
    reload_dir();
    set_status(ok ? "File deleted" : "Cannot delete file");
    s_dirty = true;
    return ok;
}

static void handle_delete_dialog_tap(int x, int y)
{
    if (y >= 220 && y <= 268 && x >= 30 && x <= 230) {
        char rel[FILE_PATH_MAX];
        snprintf(rel, sizeof(rel), "%s", s_delete_rel);
        s_confirm_delete = false;
        log_buffer_addf("[browser] delete confirmed %s", rel);
        delete_file_now(rel);
        return;
    }
    if (y >= 220 && y <= 268 && x >= 250 && x <= 450) {
        log_buffer_addf("[browser] delete cancelled %s", s_delete_rel);
        set_status("Delete cancelled");
    }
    s_confirm_delete = false;
    s_dirty = true;
}

static void draw_browser(void)
{
    if (s_color_mode) {
        display_clear(s_fill_color);
        display_flush();
        return;
    }

    if (s_confirm_delete) {
        draw_delete_confirm();
        return;
    }

    if (s_confirm_format) {
        draw_format_confirm();
        return;
    }

    if (s_error_screen) {
        display_clear(COL_BG);
        display_text_scaled(16, 40, "SD card unavailable", COL_WARN, 0, 1);
        display_text(16, 90, s_error, COL_WHITE, 0, false);
        display_text(16, 120, "Tap the screen to try again.", COL_DIM, 0, false);
        display_text(16, 140, "If the card is not FAT32, it can be formatted:", COL_DIM, 0,
                     false);
        display_fill_rect(90, 200, 300, 40, COL_WARN);
        const char *fmt = "FORMAT AS FAT32";
        display_text((LCD_WIDTH - display_text_width(fmt)) / 2, 212, fmt, COL_WHITE, 0, false);
        display_flush();
        return;
    }

    display_clear(COL_BG);
    draw_header();
    for (int i = 0; i < VISIBLE_ROWS; i++) {
        int idx = s_page * VISIBLE_ROWS + i;
        if (idx >= s_count) {
            display_fill_rect(0, HEADER_H + i * ROW_H, LCD_WIDTH, ROW_H,
                              (i & 1) ? COL_ROW_A : COL_ROW_B);
            continue;
        }
        draw_row(i, &s_entries[idx]);
    }
    draw_footer();

    if (s_status[0] && esp_timer_get_time() < s_status_until) {
        int w = display_text_width(s_status) + 16;
        int x = (LCD_WIDTH - w) / 2;
        display_fill_rect(x, 260, w, 22, COL_WARN);
        display_text(x + 8, 263, s_status, COL_WHITE, 0, false);
    } else {
        s_status[0] = '\0';
    }
    display_flush();
}

static void draw_usb(void)
{
    display_clear(COL_BG);
    display_fill_rect(0, 0, LCD_WIDTH, 6, COL_VIDEO);
    const char *t1 = "USB DRIVE";
    display_text_scaled((LCD_WIDTH - display_text_width(t1)) / 2, 60, t1, COL_VIDEO, 0, 1);
    const char *t2 = "The SD card is mounted by a phone or PC.";
    const char *t3 = "Copy files, then use Eject on the phone.";
    const char *t4 = "Only then disconnect the cable.";
    display_text((LCD_WIDTH - display_text_width(t2)) / 2, 120, t2, COL_WHITE, 0, false);
    display_text((LCD_WIDTH - display_text_width(t3)) / 2, 148, t3, COL_WHITE, 0, false);
    display_text((LCD_WIDTH - display_text_width(t4)) / 2, 190, t4, COL_DIM, 0, false);
    display_flush();
}

static void handle_tap(int x, int y, browser_storage_init_fn storage_init, bool *storage_ok)
{
    if (s_confirm_delete) {
        handle_delete_dialog_tap(x, y);
        return;
    }

    // A solid colour fills the glass; touching it always returns to the UI.
    if (s_color_mode) {
        leave_color_mode();
        s_dirty = true;
        return;
    }

    if (s_confirm_format) {
        if (y >= 200 && y <= 244) {
            if (x < 240) {
                display_clear(COL_BG);
                display_text_scaled(24, 100, "Formatting card...", COL_ACCENT, 0, 1);
                display_flush();
                // The error screen has no mounted filesystem, so its path
                // re-registers the medium before formatting. From the browser
                // the volume is mounted; f_mkfs recreates it in place.
                esp_err_t err = s_error_screen ? usb_msc_format() : usb_msc_format_full();
                s_confirm_format = false;
                if (err == ESP_OK) {
                    s_error_screen = false;
                    fs_gate_set_suspend(false);
                    snprintf(s_cwd, sizeof(s_cwd), "%s", SD_MOUNT_POINT);
                    files_scan();
                    reload_dir();
                    set_status("Card formatted");
                } else {
                    fs_gate_set_suspend(false);
                    set_error("Could not format the card");
                }
            } else if (x >= 264) {
                s_confirm_format = false;
            }
            s_dirty = true;
        }
        return;
    }

    if (s_error_screen) {
        // "Format" button is at (90,200)-(390,240). Anywhere else retries the
        // mount, which is also what the user needs after inserting a card.
        if (x >= 90 && x <= 390 && y >= 200 && y <= 240) {
            s_confirm_format = true;
            s_dirty = true;
            return;
        }

        if (!storage_init) {
            return;
        }
        esp_err_t err = storage_init();
        *storage_ok = (err == ESP_OK);
        if (err == ESP_OK) {
            s_error_screen = false;
            fs_gate_set_suspend(false);
            files_scan();
            reload_dir();
        } else {
            set_error(esp_err_to_name(err));
        }
        s_dirty = true;
        return;
    }

    if (y < HEADER_H) {
        int fmt_w = display_text_width("FORMAT");
        int fmt_x = LCD_WIDTH - 8 - fmt_w;
        if (x >= fmt_x - 6) {
            s_confirm_format = true;
            s_dirty = true;
        }
        return;
    }
    if (y >= FOOTER_Y) {
        int pages = (s_count + VISIBLE_ROWS - 1) / VISIBLE_ROWS;
        if (pages < 1) pages = 1;
        if (x < 100 && s_page > 0) {
            s_page--;
            s_dirty = true;
        } else if (x > LCD_WIDTH - 100 && s_page + 1 < pages) {
            s_page++;
            s_dirty = true;
        }
        return;
    }

    int slot = (y - HEADER_H) / ROW_H;
    int idx = s_page * VISIBLE_ROWS + slot;
    if (idx < 0 || idx >= s_count) {
        return;
    }
    entry_t *e = &s_entries[idx];
    if (e->is_dir) {
        if (!strcmp(e->name, "..")) {
            go_up();
        } else {
            go_into(e->name);
        }
    } else if (files_is_playable(e->name)) {
        play_file(e->name);
    } else {
        set_status("This format cannot be played");
    }
    s_dirty = true;
}

static bool open_delete_for_row(int y)
{
    if (s_entries == NULL || y < HEADER_H || y >= FOOTER_Y) {
        return false;
    }
    int slot = (y - HEADER_H) / ROW_H;
    int idx = s_page * VISIBLE_ROWS + slot;
    if (idx < 0 || idx >= s_count) {
        return false;
    }
    entry_t *e = &s_entries[idx];
    if (e->is_dir) {
        set_status("Folders cannot be deleted");
        s_dirty = true;
        return true;
    }
    char rel[FILE_PATH_MAX];
    if (!join_path(rel, sizeof(rel), s_cwd, e->name)) {
        return false;
    }
    snprintf(s_delete_rel, sizeof(s_delete_rel), "%s", rel);
    snprintf(s_delete_name, sizeof(s_delete_name), "%s", e->name);
    s_confirm_delete = true;
    s_dirty = true;
    log_buffer_addf("[browser] delete dialog for %s", rel);
    return true;
}

void browser_run(browser_storage_init_fn storage_init, bool storage_ok)
{
    (void)storage_ok;
    // The initial mount to APP also emits MOUNT_START, which sets the suspend
    // flag before the UI exists. Clear it once the browser owns the card.
    fs_gate_set_suspend(usb_msc_host_active());
    s_entries = heap_caps_malloc(sizeof(entry_t) * MAX_ENTRIES, MALLOC_CAP_SPIRAM);
    if (!s_entries) {
        ESP_LOGE(TAG, "no PSRAM for the directory cache");
    }

    if (s_entries && usb_msc_fs_ready()) {
        reload_dir();
    } else if (s_entries) {
        set_error(usb_msc_format_required() ? "No FAT32 partition on the card"
                                            : "Card not mounted");
    }

    bool last_usb = false;
    bool last_fs_ready = false;

    // Raw touch state for the browser: a short release is a tap, a 700 ms
    // hold opens the delete dialog. touch_tapped() cannot report duration.
    bool touch_active = false;
    bool touch_long = false;
    bool touch_moved = false;
    int touch_x0 = 0;
    int touch_y0 = 0;
    int64_t touch_start_us = 0;

    for (;;) {
        // Commands first: a pending command may interrupt a playback that is
        // still in progress, and the player leaves the queue entry untouched.
        app_cmd_t cmd;
        while (app_control_take(&cmd, 0)) {
            handle_command(&cmd);
        }

        if (s_sleeping) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        bool usb = usb_msc_host_active();
        bool fs_ready = usb_msc_fs_ready();

        if (usb != last_usb) {
            last_usb = usb;
            if (usb) {
                s_usb_screen = true;
                app_control_set_playing(NULL);
                s_dirty = true;
            } else {
                s_usb_screen = false;
                fs_gate_set_suspend(false);
                s_page = 0;
                files_scan();
                reload_dir();
                s_dirty = true;
            }
        }
        if (fs_ready != last_fs_ready) {
            last_fs_ready = fs_ready;
            if (fs_ready && !s_error_screen) {
                reload_dir();
            }
            s_dirty = true;
        }

        if (s_usb_screen) {
            if (s_dirty) {
                draw_usb();
                s_dirty = false;
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        // A failed mount leaves the FS unusable. Show the card error rather
        // than an empty listing.
        if (!fs_ready && !s_error_screen) {
            if (usb_msc_format_required()) {
                set_error("No FAT32 partition on the card");
            }
        }

        int x = 0, y = 0;
        bool browsing = !s_confirm_delete && !s_confirm_format && !s_error_screen &&
                        !s_color_mode && !s_usb_screen;
        bool down = touch_read(&x, &y);
        int64_t now_us = esp_timer_get_time();
        if (down) {
            if (!touch_active) {
                touch_active = true;
                touch_long = false;
                touch_moved = false;
                touch_x0 = x;
                touch_y0 = y;
                touch_start_us = now_us;
            } else {
                if (abs(x - touch_x0) > 20 || abs(y - touch_y0) > 20) {
                    touch_moved = true;
                }
                if (browsing && !touch_long && !touch_moved &&
                    now_us - touch_start_us >= 700000) {
                    touch_long = true;
                    open_delete_for_row(touch_y0);
                }
            }
        } else if (touch_active) {
            touch_active = false;
            if (!touch_long && !touch_moved) {
                handle_tap(touch_x0, touch_y0, storage_init, &storage_ok);
            }
        }

        if (s_status[0] && esp_timer_get_time() >= s_status_until) {
            s_status[0] = '\0';
            s_dirty = true;
        }

        if (s_dirty) {
            draw_browser();
            s_dirty = false;
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}

static void browser_task(void *arg)
{
    (void)arg;
    browser_run(s_boot_storage_init, s_boot_storage_ok);
}

void browser_start(browser_storage_init_fn storage_init, bool storage_ok)
{
    if (s_boot_storage_init) {
        return;
    }
    s_boot_storage_init = storage_init;
    s_boot_storage_ok = storage_ok;
    if (xTaskCreatePinnedToCore(browser_task, "browser", 8192, NULL, 4, NULL, 0) != pdPASS) {
        ESP_LOGE(TAG, "could not start the browser task");
    }
}
