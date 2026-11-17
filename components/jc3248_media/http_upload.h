#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t port;
    // Empty username disables authentication.
    const char *username;
    const char *password;
    uint32_t max_upload_bytes;
} http_upload_config_t;

// Start the SD card upload server. Serves a small HTML page at "/", a raw
// body upload at POST "/upload?name=<file>", and JSON listings at
// "/api/files" and "/api/status".
esp_err_t http_upload_start(const http_upload_config_t *config);

bool http_upload_running(void);

#ifdef __cplusplus
}
#endif
