/**
 * config.h - display UI configuration on the Raspberry Pi 5 (replaces ../../../esp32-ui/src/config.h)
 * -----------------------------------------------------------------------------
 * 3.5" ILI9488 (SPI, 480 x 320) with XPT2046 touch on SPI0 (enable with dtparam=spi=on):
 *   display  /dev/spidev0.0 (CE0, pin 24)   DC GPIO24 (pin 18)  RST GPIO25 (pin 22)  LED GPIO18 (pin 12)
 *   touch    /dev/spidev0.1 (CE1, pin 26)   T_IRQ GPIO23 (pin 16, optional)
 *   SCK GPIO11 (pin 23)  MOSI GPIO10 (pin 19)  MISO GPIO9 (pin 21, touch T_DO only)
 * Joystick: X / Y on the ADS1115 (I2C1), button GPIO17 (pin 11). Buzzer GPIO27 (pin 13) via NPN.
 * The whole frame is one sprite in RAM (no bands); it is converted to the panel's
 * 18-bit colour and sent over spidev, and kept for the website's display mirror.
 * -----------------------------------------------------------------------------
 */
#pragma once

#define DISPLAY_ILI9488_SPI

#define TFT_SPI_DEV "/dev/spidev0.0"
#define TOUCH_SPI_DEV "/dev/spidev0.1"
#define TFT_SPI_HZ 20000000 // default; ui.txt tft_spi_mhz overrides (the Pi housing is outside the
                           // control box: ~45 cm cable. Try 32 / 40 if the picture stays clean)
#define TOUCH_SPI_HZ 2000000
#define TFT_DC 24
#define TFT_RST 25
#define TFT_BL 18
#define TOUCH_IRQ 23
#define JOY_BUTTON 17
#define BUZZER_PIN 27

#define SCREEN_W 480
#define SCREEN_H 320
#define DISPLAY_ROTATION 1 // 1 or 3: landscape (3 = turned by 180 degrees), Settings -> Display
#define UI_BAND_H SCREEN_H
#define MAX_UPLOAD_BYTES (8 * 1024 * 1024)

#define LINK_LOG 0

// The website: http://<hostname>.local (set the Pi's hostname to this, avahi announces it)
#define MDNS_NAME "pickandplace"
#define WEB_PORT 80
