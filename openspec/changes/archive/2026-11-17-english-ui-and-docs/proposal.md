# Proposal

## Why

The project is public and aimed at an international audience, but the on-device UI,
the web pages, the Home Assistant entity names and parts of the documentation are in
Russian. Non-Latin entity IDs are awkward to use in automations and dashboards, and the
rendered screenshots show a language most users cannot read. The project should present
one language consistently: English.

## What Changes

- Translate every user-visible string in the firmware to English: the file browser,
  status messages, dialogs, card-error and USB screens, the boot screen and the HTTP
  upload/log pages.
- Translate the Home Assistant entity names in `esphome/jc3248w535.yaml`. Entity IDs
  change accordingly; the dashboard example and documentation are updated to match.
  **BREAKING** for automations that use the old Russian-derived entity IDs.
- Translate the remaining Russian text in `README.md` and `esphome/README.md`.
- Keep Cyrillic glyph coverage in the bitmap font so Cyrillic file names still render,
  but make the generated font comments and the generator source ASCII-only.
- Regenerate the UI previews in `docs/screenshots/` from the English strings.
- Rebuild the firmware and replace the release assets with the English build.

## Capabilities

_None. This is a language and presentation change; device behavior is unchanged._

## Impact

- C sources: `browser.c`, `core.c`, `http_upload.c`, `jc3248_media.cpp`.
- ESPHome configuration and dashboard example; Home Assistant entity IDs.
- Documentation, font comments and screenshots.
- New firmware images and release asset hashes.
