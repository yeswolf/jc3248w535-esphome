# Development notes

## Vendored IDF components

ESPHome compiles `components/jc3248_media/` as an external component and pulls the
following IDF components from `esphome/idf_components/`:

| Component | Origin |
|---|---|
| `esp_lcd_axs15231b` | Bring-up driver for the AXS15231B QSPI panel and in-cell touch, based on [`GlomarGadaffi/jc3248-display-driver`](https://github.com/GlomarGadaffi/jc3248-display-driver) |
| `esp_tinyusb` | Espressif esp_tinyusb 2.4.0 with the card-write patch below |

## esp_tinyusb card-write patch

[`patches/esp_tinyusb-2.4.0-card-writes.patch`](patches/esp_tinyusb-2.4.0-card-writes.patch)
contains the changes that are already applied to the vendored copy:

- `tud_msc_write10_cb` writes the sector synchronously instead of leaving it in the
  shared deferred buffer.
- The SDMMC block layer rejects unaligned transfers instead of writing at the start of
  the block.

Both matter when a phone copies files over USB and unmounts the card immediately
afterwards. The patch is kept in the tree so the vendored component can be regenerated
from an unmodified esp_tinyusb release.

## Architecture

The firmware architecture, entity model, build and flashing instructions are documented
in [`../../esphome/README.md`](../../esphome/README.md).
