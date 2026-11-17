#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *ctx;
    // Read up to len bytes. Returns the number of bytes read, 0 on EOF/error.
    size_t (*read)(void *ctx, void *buf, size_t len);
    // fseek()-style seek. Returns 0 on success.
    int (*seek)(void *ctx, long offset, int whence);
    // Polled between frames; return true to stop playback.
    bool (*should_stop)(void *ctx);
    // Called once, after the first frame has been flushed to the panel.
    // May be NULL. Used by the slideshow to fade the backlight in.
    void (*on_first_frame)(void *ctx);
    // true: restart at EOF (single-file playback). false: return 1 once the
    // last frame has been shown (slideshow).
    bool loop;
} mjpeg_io_t;

// Play a raw concatenated-JPEG stream (.mjpeg/.mjpg) or an AVI. The file is
// accessed through the callbacks so that the caller can keep the SD card
// access serialized with the USB MSC worker. Returns 0 when stopped by the
// caller, 1 when a non-looping clip reached its natural end, a negative value
// on a fatal error.
int mjpeg_play(const mjpeg_io_t *io, const char *title);

// Show/hide the in-video fps banner. The banner and the filename strip are
// currently disabled in mjpeg.cpp; this entry point is kept for when the
// overlay is re-enabled.
void mjpeg_toggle_overlay(void);

// Short description of the last playback failure ("parse -5", "decode -3",
// "size 307200", ...). Empty when there is nothing to report.
const char *mjpeg_last_error(void);

#ifdef __cplusplus
}
#endif
