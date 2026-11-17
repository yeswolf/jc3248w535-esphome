#!/bin/sh
# Publish an ESPHome OTA image and the ESP-Web-Tools manifest expected by the
# `update: platform: http_request` entity.
#
# Copy the destination directory to Home Assistant's /config/www/ (it is
# served at /local/) or to any HTTP server the device can reach.
#
# usage: publish_ota.sh <firmware.ota.bin> <destination-dir> <version> [name] [chip-family]
set -eu

if [ "$#" -lt 3 ]; then
    echo "usage: $0 <firmware.ota.bin> <destination-dir> <version> [name] [chip-family]" >&2
    exit 2
fi

BIN=$1
DEST=$2
VERSION=$3
NAME=${4:-Guition JC3248W535 3.5in IPS}
CHIP=${5:-ESP32-S3}
# Keep the JSON valid when the name contains quotes or backslashes.
NAME_JSON=$(printf '%s' "$NAME" | sed 's/\\/\\\\/g; s/"/\\"/g')

if [ ! -f "$BIN" ]; then
    echo "firmware image not found: $BIN" >&2
    exit 1
fi

mkdir -p "$DEST"
cp "$BIN" "$DEST/firmware.ota.bin"
MD5=$(md5sum "$DEST/firmware.ota.bin" | awk '{print $1}')

cat > "$DEST/manifest.json" <<EOF
{
  "name": "$NAME_JSON",
  "version": "$VERSION",
  "builds": [
    {
      "chipFamily": "$CHIP",
      "ota": {
        "path": "firmware.ota.bin",
        "md5": "$MD5"
      }
    }
  ]
}
EOF

echo "published $NAME $VERSION"
echo "  $DEST/manifest.json"
echo "  $DEST/firmware.ota.bin (md5 $MD5)"
echo
echo "If $DEST is Home Assistant's /config/www/jc3248w535, the manifest URL is:"
echo "  http://<home-assistant>:8123/local/jc3248w535/manifest.json"
echo "Set that URL as firmware_manifest_url in esphome/secrets.yaml."
