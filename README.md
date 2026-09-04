# tiny-moon

GPS-driven Moon phase display for an Adafruit QT Py RP2040 plugged into the
Seeed Studio Round Display for XIAO.

The firmware reads UTC time and position from an Adafruit MiniGPS connected to
the QT Py's STEMMA QT port, calculates the current lunar phase with Astronomy
Engine, and displays the matching 240x240 PNG from a FAT32 microSD card.

## Hardware

- Adafruit QT Py RP2040
- Seeed Studio Round Display for XIAO (GC9A01, 240x240)
- Adafruit MiniGPS at I2C address `0x10`, connected by STEMMA QT/Qwiic
- FAT32 microSD card containing `0001.png` through `0235.png` in its root

The STEMMA QT connector is `Wire1` on GPIO22/23. The XIAO socket uses a
different I2C controller. The display and SD card share SPI0; their complete
pin mapping is in `tiny_moon/qtpy_round_display_setup.h` and
`tiny_moon/tiny_moon.ino`.

## Behavior

At startup, the display waits for a valid GPS position and UTC date. Astronomy
Engine returns a lunar phase angle where 0 degrees is new Moon, 90 is first
quarter, 180 is full Moon, and 270 is last quarter. The angle is divided across
the 235 numbered images. The phase is checked once per minute, but the screen is
redrawn only when the selected image changes.

## Build and flash

Install Arduino CLI, the Earle Philhower RP2040 core, and these libraries:

- Adafruit GPS Library
- Astronomy Engine (`astronomy.h` and `astronomy.c`)
- PNGdec
- SdFat
- TFT_eSPI

Connect the QT Py over USB and run:

```sh
./flash.sh
```

The script normally detects the QT Py serial port. It can also be supplied:

```sh
./flash.sh /dev/ttyACM1
```

TFT_eSPI normally creates a second RP2040 SPI object that conflicts with the SD
card on this shared bus. The flash script copies TFT_eSPI to a temporary build
directory and applies `patches/tft-espi-rp2040-shared-spi.patch`; it does not
modify the installed Arduino library.

Set `ARDUINO_LIBRARIES_DIR` if user-installed Arduino libraries are not under
`~/Arduino/libraries`.

Moon imagery source: [NASA Scientific Visualization Studio](https://svs.gsfc.nasa.gov/4310/).
