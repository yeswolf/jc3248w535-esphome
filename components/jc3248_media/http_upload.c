#include "http_upload.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "mbedtls/base64.h"

#include "app_control.h"
#include "board.h"
#include "files.h"
#include "fs_gate.h"
#include "log_buffer.h"
#include "usb_msc.h"

static const char *TAG = "upload";

#define UPLOAD_BUF_SIZE 4096
#define NAME_MAX_LEN 96
#define AUTH_MAX 192

static httpd_handle_t s_server;
static bool s_auth_required;
static char s_auth_header[AUTH_MAX];
static uint32_t s_max_bytes;
static uint8_t *s_buf;

static const char s_page[] =
    "<!doctype html><html lang=en><meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>JC3248W535</title>"
    "<style>body{font-family:system-ui,sans-serif;margin:2rem;max-width:44rem}"
    "button{padding:.5rem 1rem}progress{width:100%;height:1.2rem}"
    "li{margin:.15rem 0}code{background:#eee;padding:.1rem .3rem}</style>"
    "<h1>JC3248W535</h1>"
    "<p><a href=\"/logs\">Device logs</a></p>"
    "<p>Video: <code>.avi</code>, <code>.mjpeg</code>, <code>.mjpg</code>, "
    "<code>.mjpe</code>, <code>.jpg</code>. Files are written to the card root; "
    "an existing file with the same name is replaced.</p>"
    "<p>JPEG/PNG photos are scaled to 480x320 automatically.</p>"
    "<input id=f type=file accept=\".avi,.mjpeg,.mjpg,.mjpe,.jpg,.jpeg,.png,.webp\"> "
    "<button onclick=up()>Upload</button>"
    "<p><progress id=p max=100 value=0></progress></p>"
    "<p id=s></p><h2>Files on the card</h2><ul id=l></ul>"
    "<script>"
    "const f=document.getElementById('f'),p=document.getElementById('p'),"
    "s=document.getElementById('s'),l=document.getElementById('l');"
    "async function refresh(){try{const r=await fetch('api/files');"
    "const j=await r.json();l.innerHTML='';for(const n of j.files){"
    "const li=document.createElement('li');li.textContent=n;l.appendChild(li);}}"
    "catch(e){s.textContent='List unavailable: '+e;}}"
    "async function prepare(file){"
    "if(!/^image\\/(jpeg|png|webp|bmp|gif)$/i.test(file.type))return{blob:file,name:file.name};"
    "try{const img=await createImageBitmap(file);"
    "const c=document.createElement('canvas');c.width=480;c.height=320;"
    "const g=c.getContext('2d');g.fillStyle='#000';g.fillRect(0,0,480,320);"
    "const k=Math.min(480/img.width,320/img.height);const w=img.width*k,h=img.height*k;"
    "g.drawImage(img,(480-w)/2,(320-h)/2,w,h);"
    "const blob=await new Promise(res=>c.toBlob(res,'image/jpeg',0.9));"
    "const name=file.name.replace(/\\.[^.]+$/,'')+'.jpg';"
    "return{blob,name};}catch(e){s.textContent='Could not read image: '+e;"
    "return null;}}"
    "async function up(){const file=f.files[0];if(!file)return;"
    "const prepared=await prepare(file);if(!prepared)return;"
    "const x=new XMLHttpRequest();"
    "x.open('POST','upload?name='+encodeURIComponent(prepared.name));"
    "x.upload.onprogress=e=>{if(e.lengthComputable)p.value=100*e.loaded/e.total;};"
    "x.onload=()=>{s.textContent=x.status+' '+x.responseText;"
    "if(x.status==200){p.value=100;refresh();}};"
    "x.onerror=()=>{s.textContent='Network error';};x.send(prepared.blob);}"
    "refresh();</script></html>";

static const char s_logs_page[] =
    "<!doctype html><html lang=en><meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>JC3248W535 logs</title>"
    "<style>html,body{margin:0;height:100%;background:#111;color:#ddd;"
    "font:12px/1.35 ui-monospace,monospace}#bar{padding:6px 8px;background:#1e1e1e;"
    "position:sticky;top:0;display:flex;gap:8px;align-items:center}"
    "button{font:inherit;padding:3px 10px}#log{margin:0;padding:8px;"
    "white-space:pre-wrap;word-break:break-word;height:calc(100% - 34px);"
    "overflow:auto;box-sizing:border-box}#s{color:#888}</style>"
    "<div id=bar><button id=p>Pause</button><button id=c>Clear</button>"
    "<span id=s></span></div><pre id=log></pre><script>"
    "let since=0,paused=false;const log=document.getElementById('log'),"
    "s=document.getElementById('s');"
    "async function poll(){if(paused)return;try{const r=await fetch('api/logs?since='+since,"
    "{cache:'no-store'});const t=await r.text();const i=t.indexOf('\\n');if(i<0)return;"
    "const m=t.slice(0,i).match(/^#next (\\d+)$/);if(!m)return;since=+m[1];"
    "const b=t.slice(i+1);if(b){log.textContent+=b;log.scrollTop=log.scrollHeight;}"
    "s.textContent='';}catch(e){s.textContent='no connection';}}"
    "setInterval(poll,800);poll();"
    "document.getElementById('p').onclick=()=>{paused=!paused;"
    "document.getElementById('p').textContent=paused?'Resume':'Pause';};"
    "document.getElementById('c').onclick=()=>{log.textContent='';};"
    "</script></html>";

static esp_err_t logs_page_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, s_logs_page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t logs_api_get(httpd_req_t *req)
{
    char query[64];
    char since_buf[16] = "0";
    if (httpd_req_get_url_query_len(req) > 0 &&
        httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        (void)httpd_query_key_value(query, "since", since_buf, sizeof(since_buf));
    }
    uint32_t since = (uint32_t)strtoul(since_buf, NULL, 10);

    size_t capacity = 12000;
    char *body = heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (body == NULL) {
        capacity = 4096;
        body = heap_caps_malloc(capacity, MALLOC_CAP_8BIT);
    }
    if (body == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no memory");
        return ESP_FAIL;
    }

    uint32_t next = since;
    size_t len = log_buffer_read(since, body, capacity, &next);
    char header[40];
    int header_len = snprintf(header, sizeof(header), "#next %u\n", (unsigned)next);

    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send_chunk(req, header, header_len);
    if (err == ESP_OK && len > 0) {
        err = httpd_resp_send_chunk(req, body, len);
    }
    if (err == ESP_OK) {
        err = httpd_resp_send_chunk(req, NULL, 0);
    }
    heap_caps_free(body);
    return err;
}

static bool authorized(httpd_req_t *req)
{
    if (!s_auth_required) {
        return true;
    }
    size_t len = httpd_req_get_hdr_value_len(req, "Authorization");
    if (len == 0 || len >= AUTH_MAX) {
        return false;
    }
    char value[AUTH_MAX];
    if (httpd_req_get_hdr_value_str(req, "Authorization", value, sizeof(value)) != ESP_OK) {
        return false;
    }
    return strcmp(value, s_auth_header) == 0;
}

static esp_err_t deny(httpd_req_t *req)
{
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"JC3248W535\"");
    return httpd_resp_sendstr(req, "authentication required\n");
}

static bool check_auth(httpd_req_t *req)
{
    if (authorized(req)) {
        return true;
    }
    deny(req);
    return false;
}

static void url_decode(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    while (*in && o + 1 < cap) {
        if (*in == '%' && isxdigit((unsigned char)in[1]) && isxdigit((unsigned char)in[2])) {
            char hex[3] = {in[1], in[2], 0};
            out[o++] = (char)strtol(hex, NULL, 16);
            in += 3;
        } else if (*in == '+') {
            out[o++] = ' ';
            in++;
        } else {
            out[o++] = *in++;
        }
    }
    out[o] = '\0';
}

static bool query_value(httpd_req_t *req, const char *key, char *out, size_t cap)
{
    size_t qlen = httpd_req_get_url_query_len(req);
    if (qlen == 0 || qlen + 1 > 512) {
        return false;
    }
    char query[512];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        return false;
    }
    char value[256];
    if (httpd_query_key_value(query, key, value, sizeof(value)) != ESP_OK) {
        return false;
    }
    url_decode(value, out, cap);
    return true;
}

// Only a bare file name with a playable extension may be written. This keeps
// a request from escaping the card mount or dropping arbitrary files there.
static bool sanitize_name(const char *in, char *out, size_t cap)
{
    const char *base = in;
    const char *slash = strrchr(base, '/');
    if (slash) {
        base = slash + 1;
    }
    const char *bslash = strrchr(base, '\\');
    if (bslash) {
        base = bslash + 1;
    }
    size_t len = strlen(base);
    if (len == 0 || len >= cap || !files_is_playable(base)) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)base; *p; p++) {
        if (*p < 0x20) {
            return false;
        }
    }
    if (!strcmp(base, ".") || !strcmp(base, "..")) {
        return false;
    }
    memcpy(out, base, len + 1);
    return true;
}

static void json_escape(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && o + 7 < cap; p++) {
        switch (*p) {
        case '"':
        case '\\':
            out[o++] = '\\';
            out[o++] = (char)*p;
            break;
        case '\n':
            out[o++] = '\\';
            out[o++] = 'n';
            break;
        case '\r':
            out[o++] = '\\';
            out[o++] = 'r';
            break;
        case '\t':
            out[o++] = '\\';
            out[o++] = 't';
            break;
        default:
            if (*p < 0x20) {
                o += snprintf(out + o, cap - o, "\\u%04x", *p);
            } else {
                out[o++] = (char)*p;
            }
            break;
        }
    }
    out[o] = '\0';
}

static esp_err_t page_get(httpd_req_t *req)
{
    if (!check_auth(req)) {
        return ESP_OK;
    }
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, s_page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t files_get(httpd_req_t *req)
{
    if (!check_auth(req)) {
        return ESP_OK;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "{\"files\":[");
    bool first = true;
    if (files_lock(1000)) {
        int count = files_count();
        for (int i = 0; i < count; i++) {
            const char *path = files_path(i);
            if (!path) {
                continue;
            }
            char escaped[FILE_PATH_MAX * 2];
            json_escape(path, escaped, sizeof(escaped));
            char item[FILE_PATH_MAX * 2 + 8];
            snprintf(item, sizeof(item), "%s\"%s\"", first ? "" : ",", escaped);
            httpd_resp_sendstr_chunk(req, item);
            first = false;
        }
        files_unlock();
    }
    httpd_resp_sendstr_chunk(req, "]}");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

static esp_err_t status_get(httpd_req_t *req)
{
    if (!check_auth(req)) {
        return ESP_OK;
    }
    char path[FILE_PATH_MAX] = "";
    char escaped[FILE_PATH_MAX * 2] = "";
    bool playing = app_control_get_playing(path, sizeof(path));
    json_escape(path, escaped, sizeof(escaped));
    char body[FILE_PATH_MAX * 2 + 160];
    snprintf(body, sizeof(body),
             "{\"playing\":%s,\"file\":\"%s\",\"sleeping\":%s,\"color_mode\":%s,"
             "\"files\":%d}",
             playing ? "true" : "false", escaped,
             app_control_get_sleeping() ? "true" : "false",
             app_control_get_color_mode() ? "true" : "false", files_count());
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, body);
}

static void upload_cleanup(bool was_suspended)
{
    // Do not clear a suspend that the USB mount path set on its own.
    if (!was_suspended && !usb_msc_host_active()) {
        fs_gate_set_suspend(false);
    }
}

static esp_err_t upload_post(httpd_req_t *req)
{
    if (!check_auth(req)) {
        return ESP_OK;
    }
    if (usb_msc_host_active()) {
        httpd_resp_set_status(req, "409 Conflict");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"usb host active\"}");
    }

    char raw_name[256];
    char name[NAME_MAX_LEN + 1];
    if (!query_value(req, "name", raw_name, sizeof(raw_name)) ||
        !sanitize_name(raw_name, name, sizeof(name))) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_sendstr(
            req, "{\"ok\":false,\"error\":\"name must be a playable file name\"}");
    }
    if (req->content_len == 0) {
        httpd_resp_set_status(req, "411 Length Required");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"empty body\"}");
    }
    if (req->content_len > (int)s_max_bytes) {
        httpd_resp_set_status(req, "413 Payload Too Large");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"file too large\"}");
    }

    char path[FILE_PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", SD_MOUNT_POINT, name);

    if (!s_buf) {
        s_buf = heap_caps_malloc(UPLOAD_BUF_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        if (!s_buf) {
            httpd_resp_set_status(req, "500 Internal Server Error");
            return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"no memory\"}");
        }
    }

    // Playback has to let go of the card before the body is written. The
    // suspend flag makes the player close its file; the gate then serializes
    // the write with the browser and the USB callback.
    bool was_suspended = fs_gate_suspended();
    fs_gate_set_suspend(true);
    if (!fs_gate_lock(8000)) {
        upload_cleanup(was_suspended);
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"card busy\"}");
    }

    FILE *f = fopen(path, "wb");
    if (!f) {
        fs_gate_unlock();
        upload_cleanup(was_suspended);
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"cannot create file\"}");
    }

    size_t remaining = (size_t)req->content_len;
    size_t total = 0;
    bool failed = false;
    int timeouts = 0;
    while (remaining > 0) {
        size_t want = remaining < UPLOAD_BUF_SIZE ? remaining : UPLOAD_BUF_SIZE;
        int len = httpd_req_recv(req, (char *)s_buf, want);
        if (len == HTTPD_SOCK_ERR_TIMEOUT) {
            if (++timeouts > 20) {
                failed = true;
                break;
            }
            continue;
        }
        if (len <= 0) {
            failed = true;
            break;
        }
        if (fwrite(s_buf, 1, (size_t)len, f) != (size_t)len) {
            failed = true;
            break;
        }
        total += (size_t)len;
        remaining -= (size_t)len;
        timeouts = 0;
    }

    if (fclose(f) != 0) {
        failed = true;
    }
    if (failed) {
        remove(path);
    }
    fs_gate_unlock();

    if (failed) {
        upload_cleanup(was_suspended);
        httpd_resp_set_status(req, "500 Internal Server Error");
        ESP_LOGE(TAG, "upload of %s failed after %u bytes", name, (unsigned)total);
        return httpd_resp_sendstr(req, "{\"ok\":false,\"error\":\"write failed\"}");
    }

    upload_cleanup(was_suspended);
    app_cmd_t cmd = {.type = APP_CMD_RESCAN};
    app_control_post(&cmd, 0);

    char escaped[NAME_MAX_LEN * 2];
    json_escape(name, escaped, sizeof(escaped));
    char body[NAME_MAX_LEN * 2 + 96];
    snprintf(body, sizeof(body), "{\"ok\":true,\"name\":\"%s\",\"size\":%u}", escaped,
             (unsigned)total);
    ESP_LOGI(TAG, "uploaded %s (%u bytes)", path, (unsigned)total);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, body);
}

esp_err_t http_upload_start(const http_upload_config_t *config)
{
    if (s_server) {
        return ESP_OK;
    }
    if (!config || config->port == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    s_max_bytes = config->max_upload_bytes ? config->max_upload_bytes : (64u * 1024u * 1024u);

    if (config->username && config->username[0]) {
        char cred[128];
        unsigned char encoded[160];
        size_t encoded_len = 0;
        snprintf(cred, sizeof(cred), "%s:%s", config->username,
                 config->password ? config->password : "");
        if (mbedtls_base64_encode(encoded, sizeof(encoded), &encoded_len,
                                  (const unsigned char *)cred, strlen(cred)) == 0) {
            snprintf(s_auth_header, sizeof(s_auth_header), "Basic %.*s", (int)encoded_len,
                     (const char *)encoded);
            s_auth_required = true;
        } else {
            ESP_LOGE(TAG, "could not encode upload credentials");
            return ESP_FAIL;
        }
    }

    httpd_config_t hcfg = HTTPD_DEFAULT_CONFIG();
    hcfg.server_port = config->port;
    // Every httpd instance needs its own UDP control port. The ESPHome
    // web_server already owns the default 32768, so starting this second
    // server with the default failed with "error in creating ctrl socket".
    hcfg.ctrl_port = 32769;
    hcfg.stack_size = 8192;
    hcfg.max_uri_handlers = 8;
    hcfg.lru_purge_enable = true;
    hcfg.recv_wait_timeout = 10;
    hcfg.send_wait_timeout = 10;

    esp_err_t err = httpd_start(&s_server, &hcfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "http server start failed: %s", esp_err_to_name(err));
        s_server = NULL;
        return err;
    }

    const httpd_uri_t page = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = page_get,
    };
    const httpd_uri_t files = {
        .uri = "/api/files",
        .method = HTTP_GET,
        .handler = files_get,
    };
    const httpd_uri_t status = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = status_get,
    };
    const httpd_uri_t upload = {
        .uri = "/upload",
        .method = HTTP_POST,
        .handler = upload_post,
    };
    const httpd_uri_t logs = {
        .uri = "/logs",
        .method = HTTP_GET,
        .handler = logs_page_get,
    };
    const httpd_uri_t logs_api = {
        .uri = "/api/logs",
        .method = HTTP_GET,
        .handler = logs_api_get,
    };
    httpd_register_uri_handler(s_server, &page);
    httpd_register_uri_handler(s_server, &files);
    httpd_register_uri_handler(s_server, &status);
    httpd_register_uri_handler(s_server, &upload);
    httpd_register_uri_handler(s_server, &logs);
    httpd_register_uri_handler(s_server, &logs_api);

    ESP_LOGI(TAG, "upload server on port %u%s", (unsigned)config->port,
             s_auth_required ? " (basic auth)" : " (no auth)");
    return ESP_OK;
}

bool http_upload_running(void)
{
    return s_server != NULL;
}
