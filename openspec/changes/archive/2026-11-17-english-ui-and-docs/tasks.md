# Tasks

## 1. Firmware strings

- [x] 1.1 Translate the file browser, dialogs, status messages, error and USB screens in
  `browser.c` and the boot screen in `core.c`.
- [x] 1.2 Translate the HTTP upload and log pages in `http_upload.c` and the media player
  error in `jc3248_media.cpp`.

## 2. Home Assistant and documentation

- [x] 2.1 Translate the ESPHome entity names in `jc3248w535.yaml`.
- [x] 2.2 Update the dashboard example with English labels and the new entity IDs.
- [x] 2.3 Translate the remaining Russian text in `README.md` and `esphome/README.md`.

## 3. Font and screenshots

- [x] 3.1 Make the font comments and `tools/make_font.py` ASCII-only while keeping the
  Cyrillic glyphs.
- [x] 3.2 Regenerate the UI previews in `docs/screenshots/`.

## 4. Build and release

- [x] 4.1 Verify that no Cyrillic text remains, then rebuild the firmware.
- [x] 4.2 Replace the release assets and hashes with the English build.
