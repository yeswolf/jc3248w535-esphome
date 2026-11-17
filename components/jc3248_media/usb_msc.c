#include "usb_msc.h"

#include <string.h>

#include "esp_log.h"
#include "ff.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"

#include "app_control.h"
#include "board.h"

static const char *TAG = "usb_msc";

static tinyusb_msc_storage_handle_t s_storage;
static usb_msc_prepare_cb_t s_prepare_cb;
static volatile bool s_host_active;
static volatile bool s_fs_ready;
static volatile bool s_format_required;
static bool s_driver_installed;

static void storage_event_cb(tinyusb_msc_storage_handle_t handle, tinyusb_msc_event_t *event,
                             void *arg)
{
    (void)arg;
    switch (event->id) {
    case TINYUSB_MSC_EVENT_MOUNT_START:
        // Both directions pass through here; in either case no file may stay
        // open while the storage switches owners.
        if (s_prepare_cb) {
            s_prepare_cb();
        }
        break;

    case TINYUSB_MSC_EVENT_MOUNT_COMPLETE: {
        tinyusb_msc_mount_point_t mp = TINYUSB_MSC_STORAGE_MOUNT_USB;
        if (tinyusb_msc_get_storage_mount_point(handle, &mp) == ESP_OK) {
            s_host_active = (mp == TINYUSB_MSC_STORAGE_MOUNT_USB);
            s_fs_ready = !s_host_active;
            s_format_required = false;
            ESP_LOGI(TAG, "storage mounted to %s", s_host_active ? "USB host" : "application");
            if (!s_host_active) {
                // The mount completes asynchronously after usb_msc_start()
                // returns. Build the playlist from this callback instead of
                // scanning too early during boot; the browser task handles
                // the command. This also refreshes the list when a USB host
                // releases the card back to the application.
                app_cmd_t cmd;
                memset(&cmd, 0, sizeof(cmd));
                cmd.type = APP_CMD_RESCAN;
                app_control_post(&cmd, 0);
            }
        }
        break;
    }

    case TINYUSB_MSC_EVENT_FORMAT_REQUIRED:
        s_fs_ready = false;
        s_format_required = true;
        ESP_LOGE(TAG, "card has no filesystem the firmware can mount");
        break;

    case TINYUSB_MSC_EVENT_MOUNT_FAILED:
    case TINYUSB_MSC_EVENT_FORMAT_FAILED:
        s_fs_ready = false;
        ESP_LOGE(TAG, "storage mount/format failed");
        break;

    default:
        break;
    }
}

esp_err_t usb_msc_start(sdmmc_card_t *card, usb_msc_prepare_cb_t prepare)
{
    if (!card) {
        return ESP_ERR_INVALID_ARG;
    }
    s_prepare_cb = prepare;

    static char base_path[] = SD_MOUNT_POINT;
    tinyusb_msc_storage_config_t cfg = {
        .medium.card = card,
        .fat_fs = {
            .base_path = base_path,
            .config = {
                .format_if_mount_failed = false,
                .max_files = 8,
                .allocation_unit_size = 16 * 1024,
            },
            .do_not_format = true,
            .format_flags = 0,
        },
        // Start detached so the callback is installed before the first mount;
        // then mount explicitly to the application.
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,
    };

    esp_err_t err = tinyusb_msc_new_storage_sdmmc(&cfg, &s_storage);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MSC storage init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = tinyusb_msc_set_storage_callback(storage_event_cb, NULL);
    if (err != ESP_OK) {
        return err;
    }

    err = tinyusb_msc_set_storage_mount_point(s_storage, TINYUSB_MSC_STORAGE_MOUNT_APP);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mount to app failed: %s", esp_err_to_name(err));
        return err;
    }

    if (!s_driver_installed) {
        tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
        // MSC callbacks descend into SDSPI and the SPI driver.
        tusb_cfg.task.size = 8192;
        err = tinyusb_driver_install(&tusb_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "TinyUSB install failed: %s", esp_err_to_name(err));
            return err;
        }
        s_driver_installed = true;
    }

    ESP_LOGI(TAG, "USB MSC started");
    return ESP_OK;
}

bool usb_msc_host_active(void)
{
    return s_host_active;
}

bool usb_msc_fs_ready(void)
{
    return s_fs_ready;
}

bool usb_msc_format_required(void)
{
    return s_format_required;
}

esp_err_t usb_msc_format(void)
{
    if (!s_storage) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_host_active) {
        // The host owns the card; formatting underneath it would corrupt the
        // host's view of the volume.
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGW(TAG, "formatting the card as FAT32");
    esp_err_t err = tinyusb_msc_format_storage(s_storage);
    if (err == ESP_OK) {
        s_fs_ready = true;
        s_format_required = false;
        ESP_LOGI(TAG, "card formatted and mounted");
    } else {
        ESP_LOGE(TAG, "format failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t usb_msc_format_full(void)
{
    if (!s_storage) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_host_active) {
        return ESP_ERR_INVALID_STATE;
    }

    // No file may stay open while the FAT is recreated.
    if (s_prepare_cb) {
        s_prepare_cb();
    }

    ESP_LOGW(TAG, "full FAT32 format requested");
    const size_t workbuf_size = 4096;
    void *workbuf = ff_memalloc(workbuf_size);
    if (!workbuf) {
        return ESP_ERR_NO_MEM;
    }

    // f_mkfs() clears the mounted FATFS object and writes a fresh MBR and
    // FAT32 volume: both FATs, the root directory and every directory entry
    // are replaced. No file from the old card stays reachable.
    const MKFS_PARM opt = {FM_FAT32, 2, 0, 0, 0};
    FRESULT fr = f_mkfs("0:", &opt, workbuf, workbuf_size);
    ff_memfree(workbuf);
    if (fr != FR_OK) {
        ESP_LOGE(TAG, "f_mkfs failed: %d", (int)fr);
        return ESP_FAIL;
    }

    // f_mkfs invalidated the FATFS object held by the VFS. Detach the medium
    // and attach it again so the application mounts the new filesystem.
    esp_err_t err = tinyusb_msc_set_storage_mount_point(
        s_storage, TINYUSB_MSC_STORAGE_MOUNT_USB);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "detach after format failed: %s", esp_err_to_name(err));
        return err;
    }
    err = tinyusb_msc_set_storage_mount_point(
        s_storage, TINYUSB_MSC_STORAGE_MOUNT_APP);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "remount after format failed: %s", esp_err_to_name(err));
        return err;
    }
    if (!s_fs_ready) {
        ESP_LOGE(TAG, "formatted card did not remount");
        return ESP_FAIL;
    }

    s_format_required = false;
    ESP_LOGI(TAG, "full format complete");
    return ESP_OK;
}
