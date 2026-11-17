#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Fixed-size ring of recent device log lines. The HTTP log page reads it with
// a sequence number so a browser can poll without gaps.
void log_buffer_init(void);
void log_buffer_add(const char *line);
// Formats a line directly into the ring. Use this from plain C modules: the
// ESPHome logger callback only sees the C++ logger, not every ESP_LOG call
// from native C code.
void log_buffer_addf(const char *format, ...);
// Copies lines newer than `since` into `out` as newline-separated text.
// `next_seq` receives the sequence of the last line copied, or `since` when
// nothing was copied. Returns the number of bytes written.
size_t log_buffer_read(uint32_t since, char *out, size_t out_size, uint32_t *next_seq);

#ifdef __cplusplus
}
#endif
