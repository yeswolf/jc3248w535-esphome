/*
 * MJPEG playback: a .mjpeg file is concatenated baseline JPEG frames, and an
 * AVI with MJPG video stores one JPEG per '##dc' chunk. Frames go to
 * Espressif's esp_new_jpeg decoder, which is SIMD-optimised on the ESP32-S3 and
 * rotates the image 90 degrees while decoding. It writes RGB565 big-endian
 * directly into the portrait 320x480 video buffer, so there is no software
 * frame rotation in the video path.
 */

#include "mjpeg.h"

#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_jpeg_dec.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "board.h"
#include "display.h"

static const char *TAG = "mjpeg";

#define FRAME_MAX (512 * 1024)
#define READ_CHUNK 2048
#define TARGET_FRAME_MS 33  // cap raw MJPEG at ~30 fps
#define DECODER_TAG "nj"    // esp_new_jpeg; "jd" would mean the old JPEGDEC build

static unsigned char *s_frames[2];  // reader fills one while the decoder uses the other
static jpeg_dec_handle_t s_dec;
static bool s_show_stats = true;
static char s_last_error[64];

void mjpeg_toggle_overlay(void)
{
    s_show_stats = !s_show_stats;
}

const char *mjpeg_last_error(void)
{
    return s_last_error;
}

// ---------------------------------------------------------------------------
// AVI (RIFF) container
//
// Most MJPEG AVI files store one JPEG per '##dc' chunk in the 'movi' list.
// Rather than trusting an index, walk the chunk headers live: that also works
// for files whose index is missing or rebuilt, and skips audio chunks.
// ---------------------------------------------------------------------------

typedef struct {
    const mjpeg_io_t *io;
    long movi_start;
    long movi_end;
    long pos;  // next chunk header
    long end;  // end of the current (possibly nested) list
    int video_stream;
    uint32_t fps_rate;   // dwRate from the stream header
    uint32_t fps_scale;  // dwScale from the stream header
    bool loop;
    bool looped;
    long resume_pos[8];
    long resume_end[8];
    int depth;
} avi_t;

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static bool read_exact(const mjpeg_io_t *io, void *buf, size_t len)
{
    uint8_t *p = (uint8_t *)buf;
    size_t got = 0;
    while (got < len) {
        size_t n = io->read(io->ctx, p + got, len - got);
        if (n == 0) {
            return false;
        }
        got += n;
    }
    return true;
}

static int hex_nibble(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Walk the header list and pick the first video stream. Every 'strh' is one
// stream, in file order; the ordinal is the number used in '##dc' chunk IDs.
static void walk_hdrl(const mjpeg_io_t *io, long off, long end, int *counter, int *video_stream,
                      uint32_t *fps_rate, uint32_t *fps_scale, int depth)
{
    if (depth > 8) {
        return;
    }
    while (off + 8 <= end) {
        if (io->seek(io->ctx, off, SEEK_SET) != 0) {
            return;
        }
        uint8_t h[8];
        if (!read_exact(io, h, 8)) {
            return;
        }
        uint32_t size = rd32(h + 4);
        long next = off + 8 + (long)size + (size & 1);
        if (next <= off) {
            return;
        }

        if (!memcmp(h, "LIST", 4)) {
            if (size >= 4) {
                walk_hdrl(io, off + 12, off + 8 + (long)size, counter, video_stream, fps_rate,
                          fps_scale, depth + 1);
            }
        } else if (!memcmp(h, "strh", 4)) {
            if (size >= 8) {
                // Stream header: fccType, fccHandler, ..., dwScale @20, dwRate @24.
                uint8_t d[28] = {0};
                size_t want = size < sizeof(d) ? size : sizeof(d);
                if (read_exact(io, d, want) && !memcmp(d, "vids", 4) && *video_stream < 0) {
                    *video_stream = *counter;
                    if (want >= 28) {
                        *fps_scale = rd32(d + 20);
                        *fps_rate = rd32(d + 24);
                    }
                }
            }
            (*counter)++;
        }
        off = next;
    }
}

static bool avi_init(const mjpeg_io_t *io, avi_t *a)
{
    memset(a, 0, sizeof(*a));
    a->io = io;
    a->video_stream = -1;
    a->loop = io->loop;
    if (io->seek(io->ctx, 0, SEEK_SET) != 0) {
        return false;
    }

    uint8_t h[12];
    if (!read_exact(io, h, 12)) {
        return false;
    }
    if (memcmp(h, "RIFF", 4) != 0 || memcmp(h + 8, "AVI ", 4) != 0) {
        return false;
    }

    long end = 8 + (long)rd32(h + 4);
    long off = 12;
    int counter = 0;
    long movi_start = -1;
    long movi_end = -1;

    while (off + 8 <= end) {
        if (io->seek(io->ctx, off, SEEK_SET) != 0) {
            break;
        }
        uint8_t ch[8];
        if (!read_exact(io, ch, 8)) {
            break;
        }
        uint32_t size = rd32(ch + 4);
        long next = off + 8 + (long)size + (size & 1);
        if (next <= off) {
            break;
        }

        if (!memcmp(ch, "LIST", 4) && size >= 4) {
            uint8_t type[4];
            if (!read_exact(io, type, 4)) {
                break;
            }
            if (!memcmp(type, "hdrl", 4)) {
                walk_hdrl(io, off + 12, off + 8 + (long)size, &counter, &a->video_stream,
                          &a->fps_rate, &a->fps_scale, 0);
            } else if (!memcmp(type, "movi", 4)) {
                movi_start = off + 12;
                movi_end = off + 8 + (long)size;
            }
        }
        off = next;
    }

    if (movi_start < 0 || movi_end <= movi_start) {
        return false;
    }
    if (a->video_stream < 0) {
        a->video_stream = 0;
    }

    a->movi_start = movi_start;
    a->movi_end = movi_end;
    a->pos = movi_start;
    a->end = movi_end;
    return true;
}

// Next JPEG frame from the AVI, or 0 when the clip has no usable frames.
static int avi_read_frame(avi_t *a, uint8_t *dst, bool *saw_any)
{
    for (;;) {
        if (a->pos + 8 > a->end) {
            if (a->depth > 0) {
                a->depth--;
                a->pos = a->resume_pos[a->depth];
                a->end = a->resume_end[a->depth];
                continue;
            }
            if (a->looped || !a->loop) {
                return 0;
            }
            a->looped = true;
            a->pos = a->movi_start;
            a->end = a->movi_end;
            continue;
        }

        if (a->io->seek(a->io->ctx, a->pos, SEEK_SET) != 0) {
            return 0;
        }
        uint8_t h[8];
        if (!read_exact(a->io, h, 8)) {
            return 0;
        }
        uint32_t size = rd32(h + 4);
        long data = a->pos + 8;
        long next = data + (long)size + (size & 1);
        if (next <= a->pos) {
            return 0;
        }

        if (!memcmp(h, "LIST", 4)) {
            // 'rec ' groups are the usual nested list inside movi. Descend,
            // and return to the parent's next chunk when the list ends.
            if (size >= 4 && a->depth < 8) {
                a->resume_pos[a->depth] = next;
                a->resume_end[a->depth] = a->end;
                a->depth++;
                a->end = data + (long)size;
                a->pos = data + 4;  // skip the 4-byte list type
            } else {
                a->pos = next;
            }
            continue;
        }

        int stream = -1;
        if (h[2] == 'd' && (h[3] == 'c' || h[3] == 'b')) {
            int hi = hex_nibble(h[0]);
            int lo = hex_nibble(h[1]);
            if (hi >= 0 && lo >= 0) {
                stream = hi * 16 + lo;
            }
        }

        if (stream == a->video_stream && size >= 4 && size <= FRAME_MAX) {
            uint8_t soi[2];
            if (read_exact(a->io, soi, 2) && soi[0] == 0xFF && soi[1] == 0xD8) {
                if (a->io->seek(a->io->ctx, data, SEEK_SET) == 0 &&
                    read_exact(a->io, dst, size)) {
                    a->pos = next;
                    a->looped = false;
                    *saw_any = true;
                    return (int)size;
                }
                return 0;
            }
        }
        a->pos = next;
    }
}

// Read the next complete JPEG frame into s_frame. Returns the frame length, 0
// at end of file, or -1 on a malformed frame.
static int read_frame(const mjpeg_io_t *io, uint8_t *dst, bool *saw_any)
{
    unsigned char chunk[READ_CHUNK];
    size_t len = 0;
    bool in_frame = false;
    int prev = -1;
    bool rewound = false;

    for (;;) {
        size_t n = io->read(io->ctx, chunk, sizeof(chunk));
        if (n == 0) {
            if (in_frame && len > 0) {
                ESP_LOGW(TAG, "truncated frame at EOF (%u bytes)", (unsigned)len);
            }
            if (rewound || !io->loop) {
                return 0;
            }
            // Loop the clip. A frame that spans the wrap point is simply
            // dropped; the next SOI starts a clean one.
            if (io->seek(io->ctx, 0, SEEK_SET) != 0) {
                return -1;
            }
            rewound = true;
            in_frame = false;
            len = 0;
            prev = -1;
            continue;
        }

        for (size_t i = 0; i < n; i++) {
            int c = chunk[i];
            if (!in_frame) {
                if (prev == 0xFF && c == 0xD8) {
                    in_frame = true;
                    len = 0;
                    dst[len++] = 0xFF;
                    dst[len++] = 0xD8;
                }
            } else {
                if (len < FRAME_MAX) {
                    dst[len++] = (unsigned char)c;
                } else {
                    ESP_LOGE(TAG, "frame larger than %d bytes, resyncing", FRAME_MAX);
                    in_frame = false;
                    len = 0;
                }
                if (prev == 0xFF && c == 0xD9) {
                    // Anything after EOI in this chunk belongs to the next
                    // frame; hand it back to the file.
                    size_t after = n - (i + 1);
                    if (after && io->seek(io->ctx, -(long)after, SEEK_CUR) != 0) {
                        ESP_LOGW(TAG, "could not rewind %u bytes", (unsigned)after);
                    }
                    *saw_any = true;
                    return (int)len;
                }
            }
            prev = c;
        }
    }
}

typedef struct {
    int idx;
    int len;
    int read_ms;
} frame_info_t;

typedef struct {
    const mjpeg_io_t *io;
    avi_t *avi;
    bool use_avi;
    QueueHandle_t queue;
    SemaphoreHandle_t free_slots;
    SemaphoreHandle_t done;
    volatile bool stop;
} reader_ctx_t;

// Reader task: fills one frame buffer while the main task decodes the other.
// The SD/FatFS read is the slowest single stage, so overlapping it with
// decoding is what makes high-bitrate MJPEG play at real time.
static void mjpeg_reader_task(void *arg)
{
    reader_ctx_t *r = (reader_ctx_t *)arg;
    int idx = 0;
    bool saw_any = false;

    for (;;) {
        if (xSemaphoreTake(r->free_slots, pdMS_TO_TICKS(200)) != pdTRUE) {
            if (r->stop) {
                break;
            }
            continue;
        }
        if (r->stop) {
            break;
        }

        int64_t t0 = esp_timer_get_time();
        int len = r->use_avi ? avi_read_frame(r->avi, s_frames[idx], &saw_any)
                             : read_frame(r->io, s_frames[idx], &saw_any);
        frame_info_t info = {idx, len, (int)((esp_timer_get_time() - t0) / 1000)};

        if (xQueueSend(r->queue, &info, pdMS_TO_TICKS(500)) != pdTRUE) {
            break;
        }
        if (len <= 0) {
            break;
        }
        idx ^= 1;
    }

    xSemaphoreGive(r->done);
    vTaskDelete(NULL);
}

static bool decoder_open_fresh(void)
{
    if (s_dec) {
        jpeg_dec_close(s_dec);
        s_dec = NULL;
    }
    jpeg_dec_config_t cfg = {
        .output_type = JPEG_PIXEL_FORMAT_RGB565_BE,
        .scale = {0, 0},
        .clipper = {0, 0},
        .rotate = JPEG_ROTATE_90D,
        .block_enable = false,
    };
    return jpeg_dec_open(&cfg, &s_dec) == JPEG_ERR_OK;
}

int mjpeg_play(const mjpeg_io_t *io, const char *title)
{
    if (!io || !io->read || !io->seek || !io->should_stop) {
        return -1;
    }
    // The title parameter is kept for the API; overlays are disabled below.
    (void)title;
    s_show_stats = true;

    for (int i = 0; i < 2; i++) {
        if (!s_frames[i]) {
            s_frames[i] = (unsigned char *)heap_caps_malloc(FRAME_MAX, MALLOC_CAP_SPIRAM);
        }
        if (!s_frames[i]) {
            ESP_LOGE(TAG, "no PSRAM for the frame buffers");
            return -1;
        }
    }

    s_last_error[0] = '\0';
    // The decoder keeps per-image state. A previous clip (or a stop in the
    // middle of one) must not leak into this one: start with a fresh handle.
    if (!decoder_open_fresh()) {
        ESP_LOGE(TAG, "esp_new_jpeg open failed");
        snprintf(s_last_error, sizeof(s_last_error), "decoder open failed");
        return -3;
    }
    uint8_t *video = (uint8_t *)display_video_buffer();
    const int video_bytes = PANEL_NATIVE_WIDTH * PANEL_NATIVE_HEIGHT * 2;

    // Wipe the previous clip off the panel before the first frame arrives.
    // Without this a decode failure would leave the previous video's last
    // frame on screen.
    memset(video, 0, video_bytes);
    // Title overlay disabled: the video starts as soon as the first frame is
    // decoded, with no filename strip.
    // if (title) {
    //     display_video_fill_rect(0, 0, LCD_WIDTH, 20, COL_BLACK);
    //     display_video_text(4, 2, title, COL_WHITE, COL_BLACK, true);
    // }
    display_flush_video();

    avi_t avi;
    // An AVI has the same JPEG frames inside 'movi'; pick the right reader by
    // looking at the container rather than the file extension.
    bool use_avi = avi_init(io, &avi);
    if (!use_avi) {
        io->seek(io->ctx, 0, SEEK_SET);
    }
    ESP_LOGI(TAG, "container: %s", use_avi ? "AVI" : "raw MJPEG");

    // AVI carries the frame rate in the stream header. Raw MJPEG does not, so
    // it keeps the old "as fast as possible, 30 fps ceiling" behaviour.
    uint32_t frame_interval_us = 0;
    if (use_avi && avi.fps_rate > 0 && avi.fps_scale > 0) {
        uint64_t us = (uint64_t)avi.fps_scale * 1000000ULL / avi.fps_rate;
        if (us > 1000 && us < 1000000) {
            frame_interval_us = (uint32_t)us;
        }
        ESP_LOGI(TAG, "AVI rate %u/%u => %.2f fps", (unsigned)avi.fps_rate,
                 (unsigned)avi.fps_scale, (double)avi.fps_rate / (double)avi.fps_scale);
    }

    QueueHandle_t queue = xQueueCreate(2, sizeof(frame_info_t));
    SemaphoreHandle_t free_slots = xSemaphoreCreateCounting(2, 2);
    SemaphoreHandle_t done = xSemaphoreCreateBinary();
    if (!queue || !free_slots || !done) {
        if (queue) vQueueDelete(queue);
        if (free_slots) vSemaphoreDelete(free_slots);
        if (done) vSemaphoreDelete(done);
        return -1;
    }

    reader_ctx_t rctx = {
        .io = io,
        .avi = &avi,
        .use_avi = use_avi,
        .queue = queue,
        .free_slots = free_slots,
        .done = done,
        .stop = false,
    };
    TaskHandle_t reader = NULL;
    if (xTaskCreatePinnedToCore(mjpeg_reader_task, "mjpeg_rd", 8192, &rctx, 2, &reader, 1) !=
        pdPASS) {
        vQueueDelete(queue);
        vSemaphoreDelete(free_slots);
        vSemaphoreDelete(done);
        return -1;
    }

    int frames = 0;
    int bad_frames = 0;
    int result = 0;
    // Timing counters were only used by the on-screen rate banner.
    // int last_read_ms = 0;
    // int last_decode_ms = 0;
    // int last_flush_ms = 0;
    int64_t start_us = esp_timer_get_time();

    while (!io->should_stop(io->ctx)) {
        frame_info_t fi;
        if (xQueueReceive(queue, &fi, pdMS_TO_TICKS(200)) != pdTRUE) {
            if (io->should_stop(io->ctx)) {
                break;
            }
            continue;
        }
        if (fi.len <= 0) {
            // A non-looping clip that produced at least one frame just ended
            // normally; that is not an error.
            if (frames > 0 && !io->loop) {
                result = 1;
                break;
            }
            ESP_LOGE(TAG, "no playable JPEG frame found");
            snprintf(s_last_error, sizeof(s_last_error), "no frames");
            result = -1;
            break;
        }

        int64_t frame_start = esp_timer_get_time();
        jpeg_dec_io_t jio = {0};
        jio.inbuf = s_frames[fi.idx];
        jio.inbuf_len = fi.len;
        jio.outbuf = video;
        jio.out_size = video_bytes;

        jpeg_dec_header_info_t info = {0};
        jpeg_error_t jerr = jpeg_dec_parse_header(s_dec, &jio, &info);
        if (jerr != JPEG_ERR_OK) {
            ESP_LOGW(TAG, "JPEG parse failed (err %d)", (int)jerr);
            snprintf(s_last_error, sizeof(s_last_error), "parse %d", (int)jerr);
            xSemaphoreGive(free_slots);
            (void)decoder_open_fresh();
            if (++bad_frames >= 20) {
                ESP_LOGE(TAG, "too many undecodable frames");
                result = -2;
                break;
            }
            continue;
        }

        // The decoder rotates 90 degrees while decoding. Only an exact
        // 480x320 source produces the 320x480 buffer we flush.
        int outlen = 0;
        if (jpeg_dec_get_outbuf_len(s_dec, &outlen) != JPEG_ERR_OK || outlen != video_bytes) {
            ESP_LOGE(TAG, "frame is not 480x320 (output %d bytes)", outlen);
            snprintf(s_last_error, sizeof(s_last_error), "size %d", outlen);
            xSemaphoreGive(free_slots);
            result = -3;
            break;
        }

        jerr = jpeg_dec_process(s_dec, &jio);
        if (jerr != JPEG_ERR_OK) {
            ESP_LOGW(TAG, "JPEG decode failed (err %d)", (int)jerr);
            snprintf(s_last_error, sizeof(s_last_error), "decode %d", (int)jerr);
            xSemaphoreGive(free_slots);
            (void)decoder_open_fresh();
            if (++bad_frames >= 20) {
                ESP_LOGE(TAG, "too many undecodable frames");
                result = -2;
                break;
            }
            continue;
        }
        bad_frames = 0;

        // The frame buffer is no longer needed: let the reader start the next
        // SD read while we draw and flush this frame.
        xSemaphoreGive(free_slots);

        // Overlays disabled: neither the filename strip nor the fps banner is
        // drawn. The decoded frame goes straight to the panel.
        // int64_t after_decode = esp_timer_get_time();
        // if (title && (esp_timer_get_time() - start_us) < 2000000) {
        //     display_video_fill_rect(0, 0, LCD_WIDTH, 20, COL_BLACK);
        //     display_video_text(4, 2, title, COL_WHITE, COL_BLACK, true);
        // }
        // if (s_show_stats && (frames % 5) == 0) {
        //     int64_t now = esp_timer_get_time();
        //     double fps = frames ? (frames * 1000000.0 / (double)(now - start_us)) : 0.0;
        //     char stats[48];
        //     snprintf(stats, sizeof(stats), "%.1f fps %s R%d D%d F%d", fps, DECODER_TAG,
        //              last_read_ms, last_decode_ms, last_flush_ms);
        //     int w = display_text_width(stats);
        //     display_video_fill_rect(LCD_WIDTH - w - 8, 0, w + 8, 18, COL_BLACK);
        //     display_video_text(LCD_WIDTH - w - 4, 1, stats, COL_WHITE, COL_BLACK, true);
        // }
        display_flush_video();
        if (frames == 0 && io->on_first_frame != nullptr) {
            io->on_first_frame(io->ctx);
        }
        // int64_t after_flush = esp_timer_get_time();
        // last_read_ms = fi.read_ms;
        // last_decode_ms = (int)((after_decode - frame_start) / 1000);
        // last_flush_ms = (int)((after_flush - after_decode) / 1000);
        frames++;

        if ((frames % 100) == 0) {
            int64_t now = esp_timer_get_time();
            ESP_LOGI(TAG, "%d frames, %.1f fps", frames,
                     1000000.0 * frames / (double)(now - start_us));
        }

        int64_t spent_us = esp_timer_get_time() - frame_start;
        if (frame_interval_us > 0) {
            if (spent_us < (int64_t)frame_interval_us) {
                uint32_t delay_us = (uint32_t)((int64_t)frame_interval_us - spent_us);
                vTaskDelay(pdMS_TO_TICKS((delay_us + 999) / 1000));
            }
        } else {
            int64_t spent_ms = spent_us / 1000;
            if (spent_ms < TARGET_FRAME_MS) {
                vTaskDelay(pdMS_TO_TICKS(TARGET_FRAME_MS - spent_ms));
            }
        }
    }

    rctx.stop = true;
    xSemaphoreGive(free_slots);
    xSemaphoreGive(free_slots);
    if (xSemaphoreTake(done, pdMS_TO_TICKS(3000)) != pdTRUE) {
        ESP_LOGW(TAG, "reader task did not stop in time");
    }

    vQueueDelete(queue);
    vSemaphoreDelete(free_slots);
    vSemaphoreDelete(done);
    if (s_dec) {
        jpeg_dec_close(s_dec);
        s_dec = NULL;
    }
    return result;
}
