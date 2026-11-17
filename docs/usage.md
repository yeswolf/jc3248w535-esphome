# Usage guide

Everything between "it boots" and "it plays".

## On-device UI

- **Browser.** Tap a directory to enter it, `< PAGE` / `PAGE >` to flip pages. Directories
  are orange, playable files green. `..` goes up.
- **Playback.** Tap a file and it plays full screen on a loop. AVI is paced from its
  stream header; raw MJPEG runs as fast as it decodes, capped at 30 fps. Tap anywhere to
  stop.
- **Slideshow.** The **Slideshow** button or Home Assistant plays every playable file in
  order, looping. Still images are held for 10 seconds.
- **Delete.** Hold a file row for about 0.7 seconds, confirm, and the file is removed
  from the card. Directories are never deleted.
- **Format.** The red **FORMAT** button in the header, and the same action on the card
  error screen, recreates the FAT32 volume after a confirmation. Every file on the card
  becomes unreachable. Formatting is blocked while a USB host owns the card.
- **USB drive mode.** Connect USB-C to a phone or PC; the card appears as a drive and the
  screen switches to a notice. Unplug to return to the browser.

## Preparing media

- Use a **FAT32** card. The FatFS configuration does not mount exFAT; exFAT cards show
  the "No FAT32 partition" screen.
- Existing **MJPEG AVI** files (RIFF container, `MJPG` video) play directly, with no
  conversion. Audio tracks inside the AVI are ignored.
- Raw MJPEG is a plain concatenation of baseline JPEG frames.
- Frames must be exactly **480x320**. Other sizes are rejected; the upload page converts
  JPEG and PNG photos automatically, a direct `/upload` request does not.
- When copying over USB from a phone, use the phone's **Eject / Unmount** action before
  unplugging the cable. Without it the phone may not flush its cache.

```sh
# MJPEG in AVI
ffmpeg -i input.mp4 -vf "scale=480:320:force_original_aspect_ratio=decrease,pad=480:320:-1:-1" \
       -c:v mjpeg -q:v 6 -pix_fmt yuvj420p -an output.avi

# raw concatenated JPEG
ffmpeg -i input.mp4 -vf "scale=480:320:force_original_aspect_ratio=decrease,pad=480:320:-1:-1" \
       -c:v mjpeg -q:v 6 -pix_fmt yuvj420p -an output.mjpeg
```

## Home Assistant entities

The device exposes a file selector, a media player, transport buttons, sleep, brightness,
a solid-colour light, a now-playing sensor, a firmware update entity and diagnostics. The
full table, upload API and OTA workflow are documented in
[`../esphome/README.md`](../esphome/README.md).

| Entity | Type | What it does |
|---|---|---|
| **File** | select | Choose a playable file; playback starts immediately |
| **File to delete** | select | Choose a file without starting playback |
| **Delete file** | button | Delete the file chosen above |
| **Play** / **Stop** | button | Start and stop playback |
| **Slideshow** | button | Play every file in order, looping |
| **Previous** / **Next** | button | Move through the sorted playlist |
| **Sleep** | switch | Stop playback and turn the backlight off, keeping Wi-Fi and the API alive |
| **Brightness** | number 0–100 | Backlight PWM level |
| **Screen colour** | RGB light | Fill the panel with a solid colour |
| **Now playing** / **Playing** | text / binary sensor | Current path and playback state |
| **Firmware update** | update | Device-pull OTA from a Home Assistant manifest |

## Dashboard cards

Two ready-to-paste cards, both with generic entity IDs:

- [`../esphome/dashboard_card.yaml`](../esphome/dashboard_card.yaml) — full card: file
  selection, delete, media player, transport, slideshow, refresh, diagnostics and the
  device log iframe.
- [`../esphome/dashboard_card_mini.yaml`](../esphome/dashboard_card_mini.yaml) — compact
  card: media player, file selection, sleep, brightness and transport buttons.

Paste either file into a dashboard as a **Manual card**. The full card uses the
documentation address `192.0.2.10`; replace it with your device IP. Adjust entity IDs if
your installation generated different ones.

## Known limitations

- AVI must contain MJPEG video (`MJPG`). Other codecs are listed but cannot be decoded.
- OpenDML / `AVIX` segmented AVI (split files, usually over 2 GB) is not supported;
  standard RIFF AVI is.
- Frames must be exactly 480x320. A direct `/upload` request does not convert anything.
- No audio playback. The NS4168 amplifier is present, but the firmware only uses it as a
  silent, parked load.
- Raw MJPEG has no timing, so it runs at up to 30 fps; AVI is paced from its stream
  header.
- Touch coordinates can be mirrored on a different panel batch. Change `TOUCH_SWAP_XY` /
  `TOUCH_MIRROR_X` / `TOUCH_MIRROR_Y` in `components/jc3248_media/board.h`.
- ESPHome sleep is not deep sleep: playback stops and the backlight turns off, but Wi-Fi
  and the API stay up so Home Assistant can wake the device.
- After the file list changes, the ESPHome select entity asks API clients to reconnect.
  Home Assistant shows the device as briefly unavailable, then lists the new files; no
  manual reload is needed.
- The HTTP upload server accepts only playable file names and writes them to the card
  root. Authentication is optional; set `upload_username` / `upload_password` to require
  basic auth.
