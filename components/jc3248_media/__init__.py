from pathlib import Path

import esphome.codegen as cg
from esphome.components import esp32, logger
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.core import CORE

CODEOWNERS = []
DEPENDENCIES = ["esp32"]
AUTO_LOAD = []

CONF_JC_ID = "jc3248_media_id"
CONF_UPLOAD_PORT = "upload_port"
CONF_UPLOAD_USERNAME = "upload_username"
CONF_UPLOAD_PASSWORD = "upload_password"
CONF_MAX_UPLOAD_MB = "max_upload_mb"

jc3248_media_ns = cg.esphome_ns.namespace("jc3248_media")
JC3248Media = jc3248_media_ns.class_("JC3248Media", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(JC3248Media),
        cv.Optional(CONF_UPLOAD_PORT, default=8080): cv.port,
        cv.Optional(CONF_UPLOAD_USERNAME, default=""): cv.string,
        cv.Optional(CONF_UPLOAD_PASSWORD, default=""): cv.string,
        cv.Optional(CONF_MAX_UPLOAD_MB, default=64): cv.int_range(min=1, max=512),
    }
).extend(cv.COMPONENT_SCHEMA)


def _idf_components_dir() -> Path:
    """Locate the vendored IDF components next to (or above) the YAML."""
    candidates = (
        CORE.config_dir / "idf_components",
        CORE.config_dir.parent / "esphome" / "idf_components",
        CORE.config_dir.parent / "idf_components",
    )
    for candidate in candidates:
        if (candidate / "esp_tinyusb").is_dir():
            return candidate
    return candidates[0]


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # ESPHome strips a large set of IDF components by default. These are the
    # ones the panel, touch, card, USB drive and upload server need.
    for name in (
        "fatfs",
        "sdmmc",
        "esp_driver_sdmmc",
        "esp_driver_i2c",
        "esp_driver_ledc",
        "esp_lcd",
        "esp_http_server",
    ):
        esp32.include_builtin_idf_component(name)

    # opendir/readdir/closedir are compiled out of the VFS by default.
    esp32.require_vfs_dir()

    # The upload server also serves the log page at /logs. Advertising its
    # port makes Home Assistant put a "Visit" link on the device page.
    cg.add_define("USE_WEBSERVER")
    cg.add_define("USE_WEBSERVER_PORT", config[CONF_UPLOAD_PORT])

    # The /logs page receives the ESPHome log stream through a listener.
    logger.request_log_listener()

    esp32.add_idf_component(name="espressif/esp_lcd_touch", ref="^1.0.4")
    esp32.add_idf_component(name="espressif/esp_new_jpeg", ref="^1.0.2")

    idf_dir = _idf_components_dir()
    esp32.add_idf_component(
        name="esp_lcd_axs15231b", path=str(idf_dir / "esp_lcd_axs15231b")
    )
    esp32.add_idf_component(name="esp_tinyusb", path=str(idf_dir / "esp_tinyusb"))

    cg.add(var.set_upload_port(config[CONF_UPLOAD_PORT]))
    cg.add(var.set_upload_username(config[CONF_UPLOAD_USERNAME]))
    cg.add(var.set_upload_password(config[CONF_UPLOAD_PASSWORD]))
    cg.add(var.set_max_upload_bytes(config[CONF_MAX_UPLOAD_MB] * 1024 * 1024))
