# Proposal

## Why

The repository already contains a full Home Assistant dashboard card, but the README
never points at it, and there is no compact variant for users who only want playback
controls. The card examples also have to stay free of personal addresses and entity IDs.

## What Changes

- Add `esphome/dashboard_card_mini.yaml`, a compact card with the media player, file
  selector, now-playing sensor, sleep, brightness and transport buttons.
- Link both the full and compact cards from `README.md` and `esphome/README.md`.
- Keep both committed cards anonymized: generic entity IDs and, in the full card, the
  documentation address `192.0.2.10` instead of a real device IP.

## Capabilities

_None. This is a documentation and example-configuration change._

## Impact

- `README.md` and `esphome/README.md` gain a dashboard-card section.
- New file `esphome/dashboard_card_mini.yaml`.
- No firmware or entity changes.
