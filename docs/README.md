# Documentation archive

Everything known about the Guition **JC3248W535C** board in one place: schematics,
vendor PDFs, IC datasheets, pinout and mechanical drawings, factory firmware and burn
instructions.

Files are reproduced from the sources listed below. Contents are unchanged; some file
names were normalised to lower case and to a common naming scheme.

## Sections

| Path | Contents |
|---|---|
| [`usage.md`](usage.md) | On-device UI, media preparation, Home Assistant entities and cards, limitations |
| [`board/`](board/README.md) | Board description, ports, pinout, schematics, specification, photos, mechanical drawings |
| [`datasheets/`](datasheets/README.md) | Datasheets for every IC on the board and for the ESP32-S3 module |
| [`factory-firmware/`](factory-firmware/README.md) | Vendor factory images, restore and burn instructions, stock SD-card fonts |
| [`development/`](development/README.md) | Notes about the vendored ESP-IDF components and their patches |

## Provenance

| Material | Source |
|---|---|
| Vendor package: specification, manual, schematics, pinout, structure diagrams, IC datasheets, burn files and burn instructions | [`NorthernMan54/JC3248W535EN`](https://github.com/NorthernMan54/JC3248W535EN), a PlatformIO port of the manufacturer's LVGL demo package |
| `JC3248W535C_I_Y_EN-80M.bin` factory image | DONE.LAND JC3248W535 write-up, mirrored by [`Kabir470/ESP32-s3-display-custom-design`](https://github.com/Kabir470/ESP32-s3-display-custom-design) |
| Stock SD-card fonts | Manufacturer SD-card archive, mirrored by [`nkinnan/manufacturer-firmware-and-sd-card-contents_Guition-K5-Knob-Series-JC3636K518`](https://github.com/nkinnan/manufacturer-firmware-and-sd-card-contents_Guition-K5-Knob-Series-JC3636K518) |
| Everything else in this repository | This project |

The upstream mirror also carries the complete Arduino demo package, the LVGL and
Arduino libraries used by the demos, prebuilt ESP-IDF archives and sample MP3/MJPEG
media. Those third-party build inputs are not duplicated here; the full package is one
clone away at the link above.

Vendor documents remain the property of Shenzhen Jingcai Intelligent Co., Ltd.

## Complete vendor package

[`vendor/JC3248W535EN/`](vendor/JC3248W535EN/) is the upstream package as received:

- the numbered vendor folders `2-Specification` through `8-Burn operation`, including
  the full Windows flash-download tool, its documentation and the burn-files folder;
- the Arduino demo sources in `src/` and the PlatformIO board definition in `boards/`;
- the upstream `README.md` and `LICENSE`.

The only parts left out of the mirror are the bundled LVGL/Arduino libraries, the
prebuilt ESP-IDF archives and the sample MP3/MJPEG media from `1-Demo` and `libraries/`
(about 950 MB). They remain available in the upstream repository linked above.

The curated folders at the top of `docs/` point at the most useful files with readable
names; `vendor/` is the untouched reference copy.

## Full file map

| File | What it is |
|---|---|
| `board/schematics/ESP32_schematic.jpg` | Complete board schematic, sheet 1: ESP32-S3 module, QSPI display, touch, SD card, audio amplifier |
| `board/schematics/Power_schematic.jpg` | Power section: USB input, charger and 5 V boost |
| `board/specifications/JC3248W535-Specifications-EN.pdf` | Vendor specification: display, memory, interfaces, dimensions |
| `board/specifications/Getting-started-JC3248W535.pdf` | Vendor getting-started manual |
| `board/pinout/GPIO_pinout.jpg` | GPIO allocation table, including the I2S amplifier pins |
| `board/pinout/JC3248W535-IO-1.png` | IO pin distribution, sheet 1 |
| `board/pinout/JC3248W535-IO-2.png` | IO pin distribution, sheet 2 |
| `board/pinout/board-back.jpg` | Board back side with every connector labelled |
| `board/pinout/board-front.jpg` | Board front side and outer dimensions |
| `board/pinout/Arduino-IDE-setup.jpg` | Arduino IDE board settings from the vendor manual |
| `board/mechanical/slot-method-1.dxf` | Panel slot drawing, method 1 |
| `board/mechanical/slot-method-2.dxf` | Panel slot drawing, method 2 |
| `datasheets/NS4168.pdf` | Nsiway NS4168, the I2S mono class-D amplifier fitted on the board |
| `datasheets/AX98357A.pdf` | AX98357A I2S amplifier from the vendor datasheet folder |
| `datasheets/ESP32-S3.pdf` | Espressif ESP32-S3 datasheet |
| `datasheets/ESP32-S3-WROOM-1.pdf` | Espressif ESP32-S3-WROOM-1 module datasheet |
| `factory-firmware/images/JC3248W535C_I_Y.bin` | Vendor factory image from the burn-files folder |
| `factory-firmware/images/JC3248W535C_I_Y_EN-80M.bin` | Vendor factory restore image referenced by the vendor instructions |
| `factory-firmware/burn-instructions/burn-1.png` | Burn instruction, step 1 |
| `factory-firmware/burn-instructions/burn-2.png` | Burn instruction, step 2 |
| `factory-firmware/burn-instructions/burn-3.png` | Burn instruction, step 3 |
| `factory-firmware/fonts/*.bin` | Stock application fonts for the SD card |
| `development/patches/esp_tinyusb-2.4.0-card-writes.patch` | The card-write patch applied to the vendored esp_tinyusb |
