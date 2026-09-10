#pragma once

// Adafruit QT Py RP2040 plugged into the Seeed Studio Round Display for XIAO.
// The display is wired by socket position, so these are RP2040 GPIO numbers
// for the corresponding QT Py pads rather than Seeed XIAO D-pin aliases.
#define USER_SETUP_LOADED

#define GC9A01_DRIVER
#define TFT_RGB_ORDER TFT_RGB
#define TFT_WIDTH 240
#define TFT_HEIGHT 240

#define TFT_SCLK 6   // QT Py SCK, XIAO socket D8
#define TFT_MISO 4   // QT Py MI,  XIAO socket D9
#define TFT_MOSI 3   // QT Py MO,  XIAO socket D10
#define TFT_CS 28    // QT Py A1,  XIAO socket D1
#define TFT_DC 26    // QT Py A3,  XIAO socket D3
#define TFT_BL 20    // QT Py TX,  XIAO socket D6
#define TFT_BACKLIGHT_ON HIGH
#define TFT_RST -1

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 20000000
