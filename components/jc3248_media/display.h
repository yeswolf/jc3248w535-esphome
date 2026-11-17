#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// RGB565 colours in host order; the framebuffer stores them big-endian for the
// panel.
#define COL_BLACK 0x0000
#define COL_WHITE 0xFFFF
#define COL_BG 0x0861
#define COL_HEADER 0x1A5B
#define COL_ROW_A 0x1082
#define COL_ROW_B 0x18C3
#define COL_DIM 0x9CF3
#define COL_DIR 0xFD20
#define COL_VIDEO 0x07E0
#define COL_ACCENT 0x05FF
#define COL_WARN 0xF800
#define COL_SELECT 0x03EF

esp_err_t display_init(void);
void display_backlight(bool on);
// 0..100. Uses LEDC PWM on the backlight pin; 0 is fully dark.
void display_set_backlight_percent(uint8_t percent);
// Fade the physical backlight to target over duration_ms without changing the
// requested brightness. Non-blocking (timer driven).
void display_fade_backlight(uint8_t target_percent, uint32_t duration_ms);
void display_clear(uint16_t color);
void display_fill_rect(int x, int y, int w, int h, uint16_t color);
void display_hline(int x, int y, int w, uint16_t color);
void display_text(int x, int y, const char *utf8, uint16_t fg, uint16_t bg, bool opaque);
void display_text_scaled(int x, int y, const char *utf8, uint16_t fg, uint16_t bg, int scale);
int display_text_width(const char *utf8);
void display_flush(void);
uint16_t *display_framebuffer(void);
void display_pixel(int x, int y, uint16_t color);

// Video fast path: the hardware-accelerated JPEG decoder writes a rotated
// 320x480 RGB565 frame straight into this buffer, so no software rotation is
// needed. Overlays use the same logical (480x320) coordinates as the UI.
uint16_t *display_video_buffer(void);
void display_video_fill_rect(int x, int y, int w, int h, uint16_t color);
void display_video_text(int x, int y, const char *utf8, uint16_t fg, uint16_t bg, bool opaque);
void display_flush_video(void);

#ifdef __cplusplus
}
#endif
