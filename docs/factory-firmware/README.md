# JC3248W535 factory firmware

Merged ESP32-S3 images for the Guition JC3248W535C (3.5", 320x480, AXS15231B,
capacitive touch). Flash one at offset `0x0` with QIO / 80 MHz.

| File | Size | SHA-256 |
|---|---|---|
| [`images/JC3248W535C_I_Y_EN-80M.bin`](images/JC3248W535C_I_Y_EN-80M.bin) | 1,795,088 B | `6501bc647a01b4de2cf1e4922041e31dc926ef3ec7b26eb81cb1ae395ef86c9a` |
| [`images/JC3248W535C_I_Y.bin`](images/JC3248W535C_I_Y.bin) | 2,022,512 B | `41a39a2fef1a2af92b9d9b42984402445267b4b61a2f4288e7ff428140a14d79` |

Both passed `esptool.py image_info` as ESP32-S3 images with valid checksums. The
`EN-80M` image is the one the vendor instructions name as the factory restore; the
second file comes from the vendor `8-Burn operation/Burn files` folder.

## Restore

Hold **BOOT**, tap **RST**, release **BOOT**. Then:

```sh
esptool.py --chip esp32s3 -p /dev/ttyACM0 -b 460800 \
  write_flash --flash_mode qio --flash_freq 80m --flash_size 16MB \
  0x0 docs/factory-firmware/images/JC3248W535C_I_Y_EN-80M.bin
```

If the board is an 8 MB flash variant, use `--flash_size 8MB`; the image itself still
starts at `0x0`.

## Burn instructions

The vendor flash-download-tool screenshots are archived in
[`burn-instructions/`](burn-instructions/):

1. [Step 1](burn-instructions/burn-1.png)
2. [Step 2](burn-instructions/burn-2.png)
3. [Step 3](burn-instructions/burn-3.png)

## Stock SD-card fonts

The stock application loads its UI font from `/fonts/UI_FONT_16.bin`, first from the
on-flash `ffat` partition and then from the SD card. The merged images above end before
the `ffat` partition (it starts at `0x410000`), so a board whose flash was erased boots
with `No font file in flash/card !!`. Copy the `fonts` directory to the root of a FAT32
card to restore text rendering:

```sh
cp -r docs/factory-firmware/fonts /media/<card>/
```

`fonts/` is the OEM set from the manufacturer's SD-card archive. Only `UI_FONT_16.bin`
is referenced by this firmware image; the other four (`EX_FONT_16.bin`, `SYHT_16.bin`,
`SYHT_BOLD_16.bin`, `SYST_BOLD_16.bin`) are the rest of the vendor set.

## Source

- `JC3248W535C_I_Y.bin`: vendor package `8-Burn operation/Burn files`, mirrored in
  [`NorthernMan54/JC3248W535EN`](https://github.com/NorthernMan54/JC3248W535EN).
- `JC3248W535C_I_Y_EN-80M.bin`: factory image referenced by the DONE.LAND
  JC3248W535 write-up, mirrored in
  [`Kabir470/ESP32-s3-display-custom-design`](https://github.com/Kabir470/ESP32-s3-display-custom-design).
- `fonts/`: manufacturer SD-card archive mirrored in
  [`nkinnan/manufacturer-firmware-and-sd-card-contents_Guition-K5-Knob-Series-JC3636K518`](https://github.com/nkinnan/manufacturer-firmware-and-sd-card-contents_Guition-K5-Knob-Series-JC3636K518).
