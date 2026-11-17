/*
 * Guition JC3248W535 (ESP32-S3-WROOM-1 N16R8).
 *
 * 3.5" 320x480 IPS, AXS15231B QSPI display + in-cell capacitive touch on I2C
 * (address 0x3B), microSD on the SDMMC host in 1-bit mode.
 *
 * The UI is laid out in landscape (480 wide, 320 high). The panel is rotated
 * with MADCTL MV|MX, matching Arduino_GFX rotation 1 for this controller.
 */
#pragma once

#include <driver/gpio.h>
#include <driver/spi_master.h>

// Logical (landscape) resolution used by the UI and the video player.
#define LCD_WIDTH 480
#define LCD_HEIGHT 320

// Native panel resolution (portrait).
#define PANEL_NATIVE_WIDTH 320
#define PANEL_NATIVE_HEIGHT 480

#define LCD_HOST SPI2_HOST
#define LCD_PIN_CS GPIO_NUM_45
#define LCD_PIN_SCLK GPIO_NUM_47
#define LCD_PIN_D0 GPIO_NUM_21
#define LCD_PIN_D1 GPIO_NUM_48
#define LCD_PIN_D2 GPIO_NUM_40
#define LCD_PIN_D3 GPIO_NUM_39
#define LCD_PIN_BL GPIO_NUM_1
#define LCD_PCLK_HZ (40 * 1000 * 1000)
#define LCD_ROTATION 1  // Arduino_GFX style: 0, 1, 2, 3

// AXS15231B touch, I2C. RST and INT are not wired on this board.
#define TOUCH_I2C_PORT I2C_NUM_0
#define TOUCH_PIN_SDA GPIO_NUM_4
#define TOUCH_PIN_SCL GPIO_NUM_8
#define TOUCH_ADDR 0x3B
#define TOUCH_I2C_HZ 400000

// Touch transform from the native portrait coordinates to the landscape UI.
// For rotation 1: mirror the native X axis, then swap X/Y.
#define TOUCH_SWAP_XY true
#define TOUCH_MIRROR_X true
#define TOUCH_MIRROR_Y false

// microSD: SDMMC, 1-bit (CLK/CMD/D0).
#define SD_PIN_CLK GPIO_NUM_12
#define SD_PIN_CMD GPIO_NUM_11
#define SD_PIN_D0 GPIO_NUM_13
// SDMMC 1-bit: try 40 MHz (the peripheral's ceiling) and fall back to 20 MHz
// in sd_card_init() if the card or wiring cannot take it.
#define SD_MAX_FREQ_KHZ 40000
#define SD_FALLBACK_FREQ_KHZ 20000

#define SD_MOUNT_POINT "/sdcard"
