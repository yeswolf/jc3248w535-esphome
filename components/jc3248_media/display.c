#include "display.h"

#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_axs15231b.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"

#include "board.h"
#include "font_8x16.h"

static const char *TAG = "display";

/*
 * The panel is used in its native portrait orientation (320x480), which is
 * the mode the vendor demo and the board bring-up repo are known to drive
 * correctly: full-width stripes, RAMWR for the first one and RAMWRC for the
 * rest. The UI and the video player work in landscape 480x320; display_flush()
 * rotates the logical frame into a portrait physical frame with 32x32 tiles
 * (cache-friendly: every source row and every destination row is one 64-byte
 * cache line).
 *
 * Mapping: logical (lx, ly) -> physical (px, py) = (319 - ly, lx).
 */

#define TILE 32
#define STRIPE_ROWS 48  // 320*48*2 = 30720 bytes, just under the 32 KB SPI DMA limit
#define STRIPE_BYTES (PANEL_NATIVE_WIDTH * STRIPE_ROWS * 2)

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static uint16_t *s_fb;    // logical landscape frame, PSRAM
static uint16_t *s_phys;  // portrait frame sent to the panel, PSRAM
static uint16_t *s_video; // portrait video frame written by the JPEG decoder
static uint16_t *s_bounce[2];  // ping-pong internal DMA stripes
static uint16_t s_tile[TILE * TILE];  // internal RAM scratch
static SemaphoreHandle_t s_flush_done;
static uint8_t s_backlight_percent = 100;  // requested level
static uint8_t s_duty_percent = 100;       // level currently driven
static esp_timer_handle_t s_fade_timer;
static int64_t s_fade_start_us;
static uint32_t s_fade_duration_us;
static uint8_t s_fade_from;
static uint8_t s_fade_to;

#define BACKLIGHT_LEDC_MODE LEDC_LOW_SPEED_MODE
#define BACKLIGHT_LEDC_TIMER LEDC_TIMER_0
#define BACKLIGHT_LEDC_CHANNEL LEDC_CHANNEL_0
#define BACKLIGHT_LEDC_BITS LEDC_TIMER_10_BIT
#define BACKLIGHT_LEDC_MAX ((1u << 10) - 1u)
#define BACKLIGHT_LEDC_HZ 5000

static inline uint16_t be16(uint16_t c)
{
    return __builtin_bswap16(c);
}

static bool flush_done_cb(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata,
                          void *user_ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_flush_done, &woken);
    return woken == pdTRUE;
}

esp_err_t display_init(void)
{
    const spi_bus_config_t buscfg = AXS15231B_PANEL_BUS_QSPI_CONFIG(
        LCD_PIN_SCLK, LCD_PIN_D0, LCD_PIN_D1, LCD_PIN_D2, LCD_PIN_D3,
        PANEL_NATIVE_WIDTH * PANEL_NATIVE_HEIGHT * 2);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO), TAG,
                        "QSPI bus init failed");

    esp_lcd_panel_io_spi_config_t io_config =
        AXS15231B_PANEL_IO_QSPI_CONFIG(LCD_PIN_CS, flush_done_cb, NULL);
    io_config.pclk_hz = LCD_PCLK_HZ;
    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &s_io), TAG,
        "panel io init failed");

    axs15231b_vendor_config_t vendor = {
        .flags.use_qspi_interface = 1,
    };
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_axs15231b(s_io, &panel_config, &s_panel), TAG,
                        "AXS15231B init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "panel init failed");
    // The vendor init table does not issue DISPON; without this the panel stays
    // lit but black.
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "display on failed");

    s_flush_done = xSemaphoreCreateCounting(2, 0);
    if (!s_flush_done) {
        return ESP_ERR_NO_MEM;
    }

    s_fb = heap_caps_malloc(LCD_WIDTH * LCD_HEIGHT * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_phys = heap_caps_malloc(PANEL_NATIVE_WIDTH * PANEL_NATIVE_HEIGHT * 2,
                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    // The JPEG decoder requires a 16-byte aligned output buffer.
    s_video = heap_caps_aligned_alloc(16, PANEL_NATIVE_WIDTH * PANEL_NATIVE_HEIGHT * 2,
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_fb || !s_phys || !s_video) {
        ESP_LOGE(TAG, "no PSRAM for the framebuffers");
        return ESP_ERR_NO_MEM;
    }
    memset(s_fb, 0, LCD_WIDTH * LCD_HEIGHT * 2);
    memset(s_phys, 0, PANEL_NATIVE_WIDTH * PANEL_NATIVE_HEIGHT * 2);
    memset(s_video, 0, PANEL_NATIVE_WIDTH * PANEL_NATIVE_HEIGHT * 2);

    // DMAing straight from PSRAM while SDMMC also uses the bus can underflow
    // the LCD SPI FIFO. Stage every stripe in internal DMA RAM instead.
    for (int i = 0; i < 2; i++) {
        s_bounce[i] = heap_caps_malloc(STRIPE_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!s_bounce[i]) {
            ESP_LOGE(TAG, "no internal DMA RAM for display stripe %d (%d bytes)", i,
                     STRIPE_BYTES);
            return ESP_ERR_NO_MEM;
        }
    }

    ledc_timer_config_t bl_timer = {
        .speed_mode = BACKLIGHT_LEDC_MODE,
        .duty_resolution = BACKLIGHT_LEDC_BITS,
        .timer_num = BACKLIGHT_LEDC_TIMER,
        .freq_hz = BACKLIGHT_LEDC_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&bl_timer), TAG, "backlight timer failed");
    ledc_channel_config_t bl_channel = {
        .gpio_num = LCD_PIN_BL,
        .speed_mode = BACKLIGHT_LEDC_MODE,
        .channel = BACKLIGHT_LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = BACKLIGHT_LEDC_TIMER,
        .duty = BACKLIGHT_LEDC_MAX,
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&bl_channel), TAG, "backlight channel failed");
    display_set_backlight_percent(100);

    ESP_LOGI(TAG, "AXS15231B ready (%dx%d landscape, panel portrait)", LCD_WIDTH, LCD_HEIGHT);
    return ESP_OK;
}

static void set_backlight_duty_(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    s_duty_percent = percent;
    uint32_t duty = (uint32_t)percent * BACKLIGHT_LEDC_MAX / 100u;
    ledc_set_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CHANNEL, duty);
    ledc_update_duty(BACKLIGHT_LEDC_MODE, BACKLIGHT_LEDC_CHANNEL);
}

static void fade_timer_cb(void *arg)
{
    (void)arg;
    int64_t elapsed = esp_timer_get_time() - s_fade_start_us;
    if (elapsed >= (int64_t)s_fade_duration_us) {
        set_backlight_duty_(s_fade_to);
        esp_timer_stop(s_fade_timer);
        return;
    }
    int from = s_fade_from;
    int to = s_fade_to;
    int value = from + (int)((int64_t)(to - from) * elapsed / (int64_t)s_fade_duration_us);
    set_backlight_duty_((uint8_t)value);
}

void display_fade_backlight(uint8_t target_percent, uint32_t duration_ms)
{
    if (target_percent > 100) {
        target_percent = 100;
    }
    if (!s_fade_timer) {
        esp_timer_create_args_t args = {
            .callback = fade_timer_cb,
            .name = "backlight_fade",
        };
        if (esp_timer_create(&args, &s_fade_timer) != ESP_OK) {
            set_backlight_duty_(target_percent);
            return;
        }
    }
    esp_timer_stop(s_fade_timer);
    if (duration_ms == 0) {
        set_backlight_duty_(target_percent);
        return;
    }
    s_fade_from = s_duty_percent;
    s_fade_to = target_percent;
    s_fade_start_us = esp_timer_get_time();
    s_fade_duration_us = duration_ms * 1000u;
    esp_timer_start_periodic(s_fade_timer, 30000);
}

void display_set_backlight_percent(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    if (s_fade_timer) {
        esp_timer_stop(s_fade_timer);
    }
    s_backlight_percent = percent;
    set_backlight_duty_(percent);
}

void display_backlight(bool on)
{
    set_backlight_duty_(on ? s_backlight_percent : 0);
}

uint16_t *display_framebuffer(void)
{
    return s_fb;
}

void display_pixel(int x, int y, uint16_t color)
{
    if ((unsigned)x >= LCD_WIDTH || (unsigned)y >= LCD_HEIGHT) {
        return;
    }
    s_fb[y * LCD_WIDTH + x] = be16(color);
}

void display_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > LCD_WIDTH) {
        w = LCD_WIDTH - x;
    }
    if (y + h > LCD_HEIGHT) {
        h = LCD_HEIGHT - y;
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    uint16_t c = be16(color);
    for (int row = 0; row < h; row++) {
        uint16_t *dst = s_fb + (y + row) * LCD_WIDTH + x;
        for (int col = 0; col < w; col++) {
            dst[col] = c;
        }
    }
}

void display_hline(int x, int y, int w, uint16_t color)
{
    display_fill_rect(x, y, w, 1, color);
}

void display_clear(uint16_t color)
{
    display_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, color);
}

static int glyph_index(uint32_t cp)
{
    int lo = 0;
    int hi = FONT_GLYPH_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (font_codepoints[mid] == cp) {
            return mid;
        }
        if (font_codepoints[mid] < cp) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    for (int i = 0; i < FONT_GLYPH_COUNT; i++) {
        if (font_codepoints[i] == '?') {
            return i;
        }
    }
    return -1;
}

static const uint8_t *next_codepoint(const char **s, uint32_t *cp)
{
    const uint8_t *p = (const uint8_t *)*s;
    uint32_t c = *p++;
    if (c == 0) {
        *s = (const char *)p - 1;
        *cp = 0;
        return NULL;
    }
    if (c < 0x80) {
        *cp = c;
    } else if ((c & 0xE0) == 0xC0) {
        *cp = ((c & 0x1F) << 6) | (*p++ & 0x3F);
    } else if ((c & 0xF0) == 0xE0) {
        *cp = ((c & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F);
        p += 2;
    } else if ((c & 0xF8) == 0xF0) {
        *cp = ((c & 0x07) << 18) | ((p[0] & 0x3F) << 12) | ((p[1] & 0x3F) << 6) |
              (p[2] & 0x3F);
        p += 3;
    } else {
        *cp = '?';
    }
    *s = (const char *)p;
    return p;
}

static void draw_glyph(int x, int y, int idx, uint16_t fg, uint16_t bg, bool opaque, int scale)
{
    if (idx < 0) {
        return;
    }
    const uint8_t *rows = font_glyphs[idx];
    for (int row = 0; row < FONT_CELL_H; row++) {
        uint8_t bits = rows[row];
        for (int col = 0; col < FONT_CELL_W; col++) {
            bool on = bits & (1u << col);
            if (!on && !opaque) {
                continue;
            }
            uint16_t c = on ? fg : bg;
            for (int sy = 0; sy < scale; sy++) {
                for (int sx = 0; sx < scale; sx++) {
                    display_pixel(x + col * scale + sx, y + row * scale + sy, c);
                }
            }
        }
    }
}

static void draw_string(int x, int y, const char *utf8, uint16_t fg, uint16_t bg, bool opaque,
                        int scale)
{
    const char *s = utf8;
    uint32_t cp;
    while (*s) {
        if (next_codepoint(&s, &cp) == NULL || cp == 0) {
            break;
        }
        draw_glyph(x, y, glyph_index(cp), fg, bg, opaque, scale);
        x += FONT_CELL_W * scale;
    }
}

void display_text(int x, int y, const char *utf8, uint16_t fg, uint16_t bg, bool opaque)
{
    draw_string(x, y, utf8, fg, bg, opaque, 1);
}

void display_text_scaled(int x, int y, const char *utf8, uint16_t fg, uint16_t bg, int scale)
{
    draw_string(x, y, utf8, fg, bg, false, scale);
}

int display_text_width(const char *utf8)
{
    int n = 0;
    const char *s = utf8;
    uint32_t cp;
    while (*s) {
        if (next_codepoint(&s, &cp) == NULL || cp == 0) {
            break;
        }
        n++;
    }
    return n * FONT_CELL_W;
}

// ---------------------------------------------------------------------------
// Video fast path
//
// esp_new_jpeg rotates the frame by 90 degrees while decoding and writes it
// straight into a portrait 320x480 buffer. These helpers draw overlays in the
// same logical landscape coordinates, using the same rotation mapping.
// ---------------------------------------------------------------------------

uint16_t *display_video_buffer(void)
{
    return s_video;
}

static inline void vput_pixel(int lx, int ly, uint16_t color)
{
    if ((unsigned)lx >= LCD_WIDTH || (unsigned)ly >= LCD_HEIGHT) {
        return;
    }
    s_video[(size_t)lx * PANEL_NATIVE_WIDTH + (PANEL_NATIVE_WIDTH - 1 - ly)] = be16(color);
}

void display_video_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (y < 0) {
        h += y;
        y = 0;
    }
    if (x + w > LCD_WIDTH) {
        w = LCD_WIDTH - x;
    }
    if (y + h > LCD_HEIGHT) {
        h = LCD_HEIGHT - y;
    }
    if (w <= 0 || h <= 0) {
        return;
    }
    uint16_t c = be16(color);
    // For a fixed logical x (physical row) the pixels are contiguous.
    for (int lx = x; lx < x + w; lx++) {
        uint16_t *row = s_video + (size_t)lx * PANEL_NATIVE_WIDTH;
        for (int ly = y; ly < y + h; ly++) {
            row[(PANEL_NATIVE_WIDTH - 1) - ly] = c;
        }
    }
}

void display_video_text(int x, int y, const char *utf8, uint16_t fg, uint16_t bg, bool opaque)
{
    const char *s = utf8;
    uint32_t cp;
    while (*s) {
        if (next_codepoint(&s, &cp) == NULL || cp == 0) {
            break;
        }
        int idx = glyph_index(cp);
        if (idx >= 0) {
            const uint8_t *rows = font_glyphs[idx];
            for (int row = 0; row < FONT_CELL_H; row++) {
                uint8_t bits = rows[row];
                for (int col = 0; col < FONT_CELL_W; col++) {
                    bool on = bits & (1u << col);
                    if (on || opaque) {
                        vput_pixel(x + col, y + row, on ? fg : bg);
                    }
                }
            }
        }
        x += FONT_CELL_W;
    }
}

// Rotate the logical 480x320 frame into the portrait 320x480 buffer.
static void rotate_frame(void)
{
    for (int sy0 = 0; sy0 < LCD_HEIGHT; sy0 += TILE) {
        for (int sx0 = 0; sx0 < LCD_WIDTH; sx0 += TILE) {
            // Load a 32x32 source tile row by row (each row is 64 bytes).
            for (int r = 0; r < TILE; r++) {
                memcpy(&s_tile[r * TILE], s_fb + (size_t)(sy0 + r) * LCD_WIDTH + sx0,
                       TILE * sizeof(uint16_t));
            }
            // Write it rotated: logical column c becomes physical row sx0+c,
            // logical row r becomes physical column 319-(sy0+r).
            for (int c = 0; c < TILE; c++) {
                uint16_t *dst = s_phys + (size_t)(sx0 + c) * PANEL_NATIVE_WIDTH;
                for (int r = 0; r < TILE; r++) {
                    dst[(PANEL_NATIVE_WIDTH - 1) - (sy0 + r)] = s_tile[r * TILE + c];
                }
            }
        }
    }
}

static void flush_physical(const uint16_t *phys)
{
    int inflight = 0;
    int stripe = 0;

    for (int y = 0; y < PANEL_NATIVE_HEIGHT; y += STRIPE_ROWS) {
        int rows = PANEL_NATIVE_HEIGHT - y;
        if (rows > STRIPE_ROWS) {
            rows = STRIPE_ROWS;
        }
        size_t bytes = (size_t)PANEL_NATIVE_WIDTH * rows * 2;

        // Two stripes may be in flight. Before reusing a bounce buffer, wait
        // for the oldest transfer to complete (transfers finish in FIFO order).
        if (inflight == 2) {
            if (xSemaphoreTake(s_flush_done, pdMS_TO_TICKS(100)) == pdTRUE) {
                inflight--;
            } else {
                ESP_LOGW(TAG, "flush timeout at y=%d", y);
            }
        }

        uint16_t *bounce = s_bounce[stripe & 1];
        memcpy(bounce, phys + (size_t)y * PANEL_NATIVE_WIDTH, bytes);
        esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, 0, y, PANEL_NATIVE_WIDTH, y + rows,
                                                  bounce);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "draw_bitmap failed at y=%d: %s", y, esp_err_to_name(err));
            continue;
        }
        inflight++;
        stripe++;
    }

    while (inflight > 0) {
        if (xSemaphoreTake(s_flush_done, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGW(TAG, "flush drain timeout");
            break;
        }
        inflight--;
    }
}

void display_flush(void)
{
    rotate_frame();
    flush_physical(s_phys);
}

void display_flush_video(void)
{
    // The decoder already produced the portrait 320x480 frame.
    flush_physical(s_video);
}
