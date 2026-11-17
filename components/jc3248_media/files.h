#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Upper bound for the playlist exposed to Home Assistant and used by
// next/previous/slideshow. Cards with more playable files keep the first
// FILES_MAX entries in name order.
#define FILES_MAX 256
#define FILE_PATH_MAX 256

// True for the extensions the MJPEG player understands. `name` may be a full
// path; only the suffix is inspected.
bool files_is_playable(const char *name);

// Walk the card and rebuild the playlist. Paths are stored relative to the
// mount point with a leading slash ("/clip.avi", "/clips/movie.avi"). Only
// increments the generation when the sorted list actually changed.
// Returns the number of playable files.
int files_scan(void);

int files_count(void);

// Valid until the next scan. Callers that are not the browser task should
// hold files_lock().
const char *files_path(int index);

// Index of a stored playlist entry, or -1.
int files_index_of(const char *path);

// Delete one playable regular file. Accepts both "/name.jpg" and
// "/sdcard/name.jpg". Rescans the playlist on success. Returns false for
// directories, missing files, path traversal and while a USB host owns the
// card.
bool files_delete(const char *path);

uint32_t files_generation(void);

bool files_lock(uint32_t timeout_ms);
void files_unlock(void);

// Copy the playlist as newline-separated paths into buf. Returns the number
// of complete entries written, or -1 when the buffer is too small.
int files_snapshot(char *buf, size_t cap);

#ifdef __cplusplus
}
#endif
