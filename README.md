# tiny-moon

GPS-driven Moon phase display for an Adafruit QT Py RP2040 plugged into the
Seeed Studio Round Display for XIAO.

The firmware calculates the current lunar phase with Astronomy Engine and
displays the matching frame from an indexed NASA image sequence.

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

Astronomy Engine returns a lunar phase angle where 0 degrees is new Moon, 90 is
first quarter, 180 is full Moon, and 270 is last quarter. When GPS mode is
enabled, the display waits at startup for a valid position and UTC date.

The complete 0-360 degree phase longitude is mapped across all 235 images, so
waxing and waning select different frames. The phase is checked once per
minute, and the display is redrawn only when its selected frame changes.

Swipe left across the Moon to open a status menu. It shows the named phase,
phase angle, selected image, coordinates, and UTC time. Swipe left again for
the NeoPixel menu. Tap its hue and brightness bars to select a color and
intensity, or use the on/off button. Swipe right to move back one screen. The
NeoPixel state is stored in `neopixel.txt` on the microSD card and survives a
restart.

The complete interface, including the Moon and menus, is permanently rotated
90 degrees clockwise. The CHSC6X touch controller is polled directly at I2C
address `0x2e` on GPIO24/25, uses coordinates transformed to match that
orientation, and automatically reconnects if it is not ready at startup.

The current development configuration bypasses GPS and uses Gainesville,
Florida (`29.6516`, `-82.3248`). The flash script embeds its current UTC build
time, and the RP2040 advances a software clock while powered. Because this
clock is not battery backed, rebooting starts again from the last build time;
run `./flash.sh` again to refresh it. Set `USE_GPS` to `true` in the sketch to
restore live MiniGPS position and UTC.

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
