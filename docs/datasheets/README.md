# Datasheets

| File | Part | Role on this board |
|---|---|---|
| [`NS4168.pdf`](NS4168.pdf) | Nsiway NS4168 | I2S input, built-in DAC, 2.5 W mono class-D amplifier. Fitted as U4 and drives the Speak connector. |
| [`AX98357A.pdf`](AX98357A.pdf) | AKM AX98357A | I2S class-D amplifier. The vendor datasheet folder includes it; the board uses the NS4168. |
| [`ESP32-S3.pdf`](ESP32-S3.pdf) | Espressif ESP32-S3 | The SoC inside the module: dual-core Xtensa LX7, Wi-Fi, BLE, USB OTG, I2S, SDMMC, LCD peripheral. |
| [`ESP32-S3-WROOM-1.pdf`](ESP32-S3-WROOM-1.pdf) | Espressif ESP32-S3-WROOM-1 | The module fitted on the board: 16 MB flash and 8 MB octal PSRAM in the N16R8 variant. |

## Display and touch

The display controller and the in-cell touch controller are both part of the AXS15231B.
The vendor package does not include a separate AXS15231B datasheet; the register-level
initialisation used by this project is documented in
[`esphome/idf_components/esp_lcd_axs15231b`](../../esphome/idf_components/esp_lcd_axs15231b)
and in the upstream bring-up driver
[`GlomarGadaffi/jc3248-display-driver`](https://github.com/GlomarGadaffi/jc3248-display-driver).

## Power management

U2 is marked as a power-bank controller in the schematic and is not covered by a
separate datasheet in the vendor package. The schematic in
[`../board/schematics/Power_schematic.jpg`](../board/schematics/Power_schematic.jpg)
shows the complete application circuit: USB input, charger, boost inductor, battery
connector and the 5 V output that feeds the board.
