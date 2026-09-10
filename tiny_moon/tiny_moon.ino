#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GPS.h>
#include <Adafruit_NeoPixel.h>
#include <SdFat.h>
#include <TFT_eSPI.h>
#include <PNGdec.h>
#include <astronomy.h>

#ifndef BUILD_UNIX_TIME
#define BUILD_UNIX_TIME 0
#endif

// Development configuration: fixed Gainesville location and a software clock.
// Change this to true to restore live MiniGPS position and UTC.
constexpr bool USE_GPS = false;
constexpr double FIXED_LATITUDE = 29.6516;
constexpr double FIXED_LONGITUDE = -82.3248;
constexpr double UNIX_TO_J2000_DAYS = 10957.5;

constexpr uint8_t GPS_ADDRESS = 0x10;
constexpr uint8_t TOUCH_ADDRESS = 0x2e;
constexpr uint8_t TOUCH_SDA_PIN = 24;
constexpr uint8_t TOUCH_SCL_PIN = 25;
constexpr uint8_t SD_CS_PIN = 27;  // QT Py A2, XIAO socket D2
constexpr char NEOPIXEL_SETTINGS_FILE[] = "neopixel.txt";
constexpr uint16_t IMAGE_COUNT = 235;
constexpr uint16_t MAX_IMAGE_WIDTH = 240;
constexpr int16_t SCREEN_SIZE = 240;
constexpr uint8_t DISPLAY_ROTATION = 2;
constexpr int16_t DISPLAY_ROTATION_DEGREES = 90;
constexpr uint32_t PHASE_UPDATE_INTERVAL_MS = 60000;
constexpr uint32_t GPS_STATUS_INTERVAL_MS = 10000;
constexpr uint32_t TOUCH_POLL_INTERVAL_MS = 25;
constexpr uint32_t TOUCH_RECONNECT_INTERVAL_MS = 1000;
constexpr uint32_t TOUCH_RELEASE_DEBOUNCE_MS = 300;
constexpr uint32_t SWIPE_MAX_DURATION_MS = 3000;
constexpr int16_t SWIPE_THRESHOLD = 30;

enum class Screen : uint8_t {
  MOON,
  STATUS,
  NEOPIXEL,
};

TFT_eSPI tft;
Adafruit_GPS gps(&Wire1);  // STEMMA QT is Wire1 on the QT Py RP2040.
Adafruit_NeoPixel onboardPixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
SdFs sd;
FsFile imageFile;
PNG png;
uint16_t moonImage[SCREEN_SIZE * SCREEN_SIZE];

bool gpsConnected = false;
bool touchConnected = false;
bool touchTracking = false;
bool gestureHandled = false;
bool moonImageValid = false;
bool neopixelDirty = false;
bool neopixelEnabled = false;
Screen activeScreen = Screen::MOON;
int currentImage = -1;
uint16_t neopixelHue = 210;
uint8_t neopixelBrightness = 64;
uint32_t lastPhaseUpdate = 0;
uint32_t lastGpsStatus = 0;
uint32_t lastFixedClockMillis = 0;
uint32_t lastTouchPoll = 0;
uint32_t lastTouchConnectAttempt = 0;
uint32_t touchStartedAt = 0;
uint32_t lastTouchPressedAt = 0;
double fixedUnixTime = static_cast<double>(BUILD_UNIX_TIME);
double latestPhaseAngle = 0.0;
double latestLatitude = FIXED_LATITUDE;
double latestLongitude = FIXED_LONGITUDE;
int latestImageNumber = -1;
int16_t touchStartX = 0;
int16_t touchStartY = 0;
int16_t touchLastX = 0;
int16_t touchLastY = 0;
char latestTimestamp[28] = "Calculating...";

void showStatus(const char *firstLine, const char *secondLine = nullptr) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(firstLine, 120, secondLine ? 108 : 120, 2);
  if (secondLine) {
    tft.drawString(secondLine, 120, 132, 2);
  }
}

void *openImage(const char *filename, int32_t *size) {
  if (!imageFile.open(filename, O_RDONLY)) {
    Serial.printf("Failed to open %s\n", filename);
    *size = 0;
    return nullptr;
  }

  *size = static_cast<int32_t>(imageFile.fileSize());
  return &imageFile;
}

void closeImage(void *handle) {
  (void)handle;
  imageFile.close();
}

int32_t readImage(PNGFILE *handle, uint8_t *buffer, int32_t length) {
  (void)handle;
  return imageFile.read(buffer, length);
}

int32_t seekImage(PNGFILE *handle, int32_t position) {
  (void)handle;
  return imageFile.seekSet(position) ? position : -1;
}

void displayMoonBuffer() {
  if (!moonImageValid) {
    return;
  }

  const uint32_t startedAt = millis();
  // TFT rotation 2 rotates the complete interface 90 degrees clockwise from
  // the original rotation 1, so the Moon needs no separate pixel transform.
  tft.pushImage(0, 0, SCREEN_SIZE, SCREEN_SIZE, moonImage);

  Serial.printf("Displayed Moon with display at %d degrees in %lu ms\n",
                DISPLAY_ROTATION_DEGREES,
                static_cast<unsigned long>(millis() - startedAt));
}

int drawPngLine(PNGDRAW *line) {
  if (line->y >= SCREEN_SIZE || line->iWidth > MAX_IMAGE_WIDTH) {
    return 0;
  }
  png.getLineAsRGB565(
      line, &moonImage[line->y * SCREEN_SIZE], PNG_RGB565_BIG_ENDIAN,
      0xffffffff);
  return 1;
}

bool renderImage(const char *filename) {
  const int openResult = png.open(
      filename, openImage, closeImage, readImage, seekImage, drawPngLine);
  if (openResult != PNG_SUCCESS) {
    Serial.printf("Could not open %s (PNG error %d)\n", filename, openResult);
    return false;
  }

  const uint32_t startedAt = millis();
  memset(moonImage, 0, sizeof(moonImage));
  moonImageValid = false;
  const int decodeResult = png.decode(nullptr, 0);
  png.close();

  if (decodeResult != PNG_SUCCESS) {
    Serial.printf("Could not decode %s (PNG error %d)\n", filename,
                  decodeResult);
    return false;
  }

  moonImageValid = true;
  displayMoonBuffer();
  Serial.printf("Decoded %s in %lu ms\n", filename,
                static_cast<unsigned long>(millis() - startedAt));
  return true;
}

bool hasValidGpsData() {
  return gps.fix && gps.month >= 1 && gps.month <= 12 && gps.day >= 1 &&
         gps.day <= 31;
}

void updateFixedClock() {
  const uint32_t now = millis();
  fixedUnixTime += static_cast<uint32_t>(now - lastFixedClockMillis) / 1000.0;
  lastFixedClockMillis = now;
}

bool getObservation(
    astro_time_t *time, double *latitude, double *longitude) {
  if (USE_GPS) {
    if (!hasValidGpsData()) {
      return false;
    }
    *time = Astronomy_MakeTime(
        2000 + gps.year, gps.month, gps.day, gps.hour, gps.minute,
        gps.seconds + gps.milliseconds / 1000.0);
    *latitude = gps.latitudeDegrees;
    *longitude = gps.longitudeDegrees;
    return true;
  }

  *time = Astronomy_TimeFromDays(
      fixedUnixTime / 86400.0 - UNIX_TO_J2000_DAYS);
  *latitude = FIXED_LATITUDE;
  *longitude = FIXED_LONGITUDE;
  return true;
}

const char *phaseName(double angle) {
  if (angle < 22.5 || angle >= 337.5) {
    return "New Moon";
  }
  if (angle < 67.5) {
    return "Waxing Crescent";
  }
  if (angle < 112.5) {
    return "First Quarter";
  }
  if (angle < 157.5) {
    return "Waxing Gibbous";
  }
  if (angle < 202.5) {
    return "Full Moon";
  }
  if (angle < 247.5) {
    return "Waning Gibbous";
  }
  if (angle < 292.5) {
    return "Last Quarter";
  }
  return "Waning Crescent";
}

void drawStatusMenu() {
  constexpr uint16_t background = TFT_NAVY;
  char line[32];

  tft.fillScreen(background);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, background);
  tft.drawString("TINY MOON", 120, 34, 4);
  tft.drawFastHLine(48, 55, 144, TFT_DARKGREY);

  tft.setTextColor(TFT_CYAN, background);
  tft.drawString(
      latestImageNumber < 0 ? "Calculating..." : phaseName(latestPhaseAngle),
      120, 77, 2);

  tft.setTextColor(TFT_WHITE, background);
  if (latestImageNumber >= 0) {
    snprintf(line, sizeof(line), "Phase: %.1f deg", latestPhaseAngle);
    tft.drawString(line, 120, 101, 2);
    snprintf(line, sizeof(line), "Frame: %04d / %04d", latestImageNumber,
             IMAGE_COUNT);
    tft.drawString(line, 120, 123, 2);
  }

  tft.setTextColor(TFT_LIGHTGREY, background);
  tft.drawString(USE_GPS ? "GPS position" : "Gainesville, FL", 120, 148, 2);
  snprintf(line, sizeof(line), "%.4f, %.4f", latestLatitude, latestLongitude);
  tft.drawString(line, 120, 166, 1);
  tft.drawString(latestTimestamp, 120, 185, 2);

  tft.setTextColor(TFT_CYAN, background);
  tft.drawString("< NeoPixel", 74, 211, 1);
  tft.drawString("Moon >", 169, 211, 1);
  Serial.println("Status menu shown");
}

uint32_t neopixelColor(uint16_t hue, uint8_t brightness) {
  const uint16_t hue16 = static_cast<uint32_t>(hue) * 65535U / 359U;
  return onboardPixel.ColorHSV(hue16, 255, brightness);
}

uint16_t tftColorFromRgb(uint32_t color) {
  return tft.color565(
      static_cast<uint8_t>(color >> 16),
      static_cast<uint8_t>(color >> 8),
      static_cast<uint8_t>(color));
}

void applyNeopixel() {
  onboardPixel.setPixelColor(
      0, neopixelEnabled ? neopixelColor(neopixelHue, neopixelBrightness) : 0);
  onboardPixel.show();
  Serial.printf("NeoPixel %s: hue=%u brightness=%u\n",
                neopixelEnabled ? "on" : "off", neopixelHue,
                neopixelBrightness);
}

void drawNeopixelMenu() {
  constexpr uint16_t background = TFT_NAVY;
  constexpr int16_t barLeft = 20;
  constexpr int16_t barWidth = 201;
  char line[28];

  tft.fillScreen(background);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, background);
  tft.drawString("NEOPIXEL", 120, 27, 4);

  const uint32_t previewRgb =
      neopixelEnabled ? neopixelColor(neopixelHue, neopixelBrightness) : 0;
  tft.fillCircle(120, 68, 19, tftColorFromRgb(previewRgb));
  tft.drawCircle(120, 68, 20, TFT_WHITE);
  snprintf(line, sizeof(line), "%s   %u%%",
           neopixelEnabled ? "ON" : "OFF",
           static_cast<unsigned>(neopixelBrightness) * 100U / 255U);
  tft.setTextColor(neopixelEnabled ? TFT_GREEN : TFT_LIGHTGREY, background);
  tft.drawString(line, 120, 96, 2);

  tft.setTextColor(TFT_LIGHTGREY, background);
  tft.drawString("HUE", 120, 112, 1);
  for (int16_t offset = 0; offset < barWidth; ++offset) {
    const uint16_t hue = static_cast<uint32_t>(offset) * 359U / (barWidth - 1);
    tft.drawFastVLine(
        barLeft + offset, 121, 19,
        tftColorFromRgb(neopixelColor(hue, 255)));
  }
  const int16_t hueX =
      barLeft + static_cast<uint32_t>(neopixelHue) * (barWidth - 1) / 359U;
  tft.drawRect(hueX - 2, 119, 5, 23, TFT_WHITE);

  tft.setTextColor(TFT_LIGHTGREY, background);
  tft.drawString("BRIGHTNESS", 120, 152, 1);
  for (int16_t offset = 0; offset < barWidth; ++offset) {
    const uint8_t brightness =
        static_cast<uint32_t>(offset) * 255U / (barWidth - 1);
    tft.drawFastVLine(
        barLeft + offset, 161, 19,
        tftColorFromRgb(neopixelColor(neopixelHue, brightness)));
  }
  const int16_t brightnessX = barLeft +
      static_cast<uint32_t>(neopixelBrightness) * (barWidth - 1) / 255U;
  tft.drawRect(brightnessX - 2, 159, 5, 23, TFT_WHITE);

  const uint16_t toggleColor = neopixelEnabled ? TFT_GREEN : TFT_DARKGREY;
  tft.fillRoundRect(78, 187, 84, 25, 7, toggleColor);
  tft.setTextColor(neopixelEnabled ? TFT_BLACK : TFT_WHITE, toggleColor);
  tft.drawString(neopixelEnabled ? "TURN OFF" : "TURN ON", 120, 199, 1);

  tft.setTextColor(TFT_CYAN, background);
  tft.drawString("Back: swipe right", 120, 222, 1);
  Serial.println("NeoPixel menu shown");
}

void loadNeopixelSetting() {
  FsFile settingsFile;
  if (!settingsFile.open(NEOPIXEL_SETTINGS_FILE, O_RDONLY)) {
    Serial.println("No saved NeoPixel setting; starting off");
    return;
  }

  char value[24] = {};
  const int bytesRead = settingsFile.read(value, sizeof(value) - 1);
  settingsFile.close();
  if (bytesRead <= 0) {
    return;
  }

  unsigned savedHue;
  unsigned savedBrightness;
  unsigned savedEnabled;
  if (sscanf(value, "%u %u %u", &savedHue, &savedBrightness, &savedEnabled) == 3 &&
      savedHue <= 359 && savedBrightness <= 255 && savedEnabled <= 1) {
    neopixelHue = savedHue;
    neopixelBrightness = savedBrightness;
    neopixelEnabled = savedEnabled == 1;
    Serial.printf("Loaded NeoPixel: %s hue=%u brightness=%u\n",
                  neopixelEnabled ? "on" : "off", neopixelHue,
                  neopixelBrightness);
  }
}

void saveNeopixelSetting() {
  FsFile settingsFile;
  if (!settingsFile.open(
          NEOPIXEL_SETTINGS_FILE, O_WRONLY | O_CREAT | O_TRUNC)) {
    Serial.println("Could not save NeoPixel setting");
    return;
  }
  settingsFile.printf("%u %u %u\n", neopixelHue, neopixelBrightness,
                      neopixelEnabled ? 1 : 0);
  settingsFile.sync();
  settingsFile.close();
  neopixelDirty = false;
  Serial.println("Saved NeoPixel setting");
}

void configureNeopixel() {
  pinMode(NEOPIXEL_POWER, OUTPUT);
  digitalWrite(NEOPIXEL_POWER, HIGH);
  onboardPixel.begin();
  onboardPixel.setBrightness(255);
  applyNeopixel();
}

void updateMoonImage() {
  astro_time_t time;
  double latitude;
  double longitude;
  if (!getObservation(&time, &latitude, &longitude)) {
    return;
  }

  const astro_angle_result_t phase = Astronomy_MoonPhase(time);
  if (phase.status != ASTRO_SUCCESS) {
    Serial.printf("Moon phase calculation failed (status %d)\n", phase.status);
    return;
  }

  // 0=new, 90=first quarter, 180=full, 270=last quarter. Using the
  // complete 0..360 cycle preserves the distinction between waxing and waning.
  int imageNumber = 1 + static_cast<int>(phase.angle * IMAGE_COUNT / 360.0);
  if (imageNumber > IMAGE_COUNT) {
    imageNumber = IMAGE_COUNT;
  }

  char timestamp[24];
  Astronomy_FormatTime(time, TIME_FORMAT_SECOND, timestamp, sizeof(timestamp));
  latestPhaseAngle = phase.angle;
  latestLatitude = latitude;
  latestLongitude = longitude;
  latestImageNumber = imageNumber;
  snprintf(latestTimestamp, sizeof(latestTimestamp), "%s UTC", timestamp);
  Serial.printf(
      "%s, %.6f, %.6f; phase %.3f deg -> image %04d\n",
      timestamp, latitude, longitude, phase.angle, imageNumber);

  if (activeScreen == Screen::STATUS) {
    drawStatusMenu();
    return;
  }
  if (activeScreen == Screen::NEOPIXEL) {
    drawNeopixelMenu();
    return;
  }

  if (imageNumber == currentImage) {
    return;
  }

  char filename[16];
  snprintf(filename, sizeof(filename), "%04d.png", imageNumber);
  if (renderImage(filename)) {
    currentImage = imageNumber;
  }
}

void setScreen(Screen screen) {
  if (activeScreen == screen) {
    return;
  }

  if (activeScreen == Screen::NEOPIXEL && neopixelDirty) {
    saveNeopixelSetting();
  }

  activeScreen = screen;
  if (activeScreen == Screen::STATUS) {
    drawStatusMenu();
    return;
  }
  if (activeScreen == Screen::NEOPIXEL) {
    drawNeopixelMenu();
    return;
  }

  Serial.println("Moon screen shown");
  if (moonImageValid && currentImage == latestImageNumber) {
    displayMoonBuffer();
  } else {
    currentImage = -1;
    lastPhaseUpdate = millis();
    updateMoonImage();
  }
}

void handleNeopixelTap(int16_t x, int16_t y) {
  if (activeScreen != Screen::NEOPIXEL) {
    return;
  }

  bool changed = false;
  if (x >= 20 && x <= 220 && y >= 116 && y <= 145) {
    neopixelHue =
        static_cast<uint32_t>(x - 20) * 359U / static_cast<uint32_t>(200);
    neopixelEnabled = true;
    changed = true;
  } else if (x >= 20 && x <= 220 && y >= 156 && y <= 184) {
    neopixelBrightness =
        static_cast<uint32_t>(x - 20) * 255U / static_cast<uint32_t>(200);
    changed = true;
  } else if (x >= 78 && x <= 162 && y >= 186 && y <= 214) {
    neopixelEnabled = !neopixelEnabled;
    changed = true;
  }

  if (changed) {
    neopixelDirty = true;
    applyNeopixel();
    drawNeopixelMenu();
  }
}

void configureTouch() {
  Wire.setSDA(TOUCH_SDA_PIN);
  Wire.setSCL(TOUCH_SCL_PIN);
  Wire.begin();

  lastTouchConnectAttempt = millis();
  Wire.beginTransmission(TOUCH_ADDRESS);
  touchConnected = Wire.endTransmission() == 0;
  if (touchConnected) {
    Serial.println("CHSC6X touch ready at 0x2e");
  } else {
    Serial.println("CHSC6X touch not found at 0x2e");
  }
}

bool readTouch(bool *pressed, int16_t *x, int16_t *y) {
  const size_t count = Wire.requestFrom(TOUCH_ADDRESS, static_cast<uint8_t>(5));
  if (count != 5) {
    while (Wire.available()) {
      Wire.read();
    }
    return false;
  }

  uint8_t packet[5];
  for (uint8_t i = 0; i < sizeof(packet); ++i) {
    packet[i] = Wire.read();
  }

  *pressed = packet[0] == 0x01;
  if (*pressed) {
    // The controller reports unrotated coordinates. Rotation 2 reverses both
    // axes so touch positions continue to match the rotated interface.
    *x = constrain(static_cast<int16_t>(SCREEN_SIZE - 1 - packet[2]),
                   0, SCREEN_SIZE - 1);
    *y = constrain(static_cast<int16_t>(SCREEN_SIZE - 1 - packet[4]),
                   0, SCREEN_SIZE - 1);
  }
  return true;
}

void serviceTouch() {
  const uint32_t now = millis();
  if (!touchConnected) {
    if (now - lastTouchConnectAttempt >= TOUCH_RECONNECT_INTERVAL_MS) {
      lastTouchConnectAttempt = now;
      Wire.beginTransmission(TOUCH_ADDRESS);
      touchConnected = Wire.endTransmission() == 0;
      if (touchConnected) {
        Serial.println("CHSC6X touch connected after retry");
      }
    }
    return;
  }

  if (now - lastTouchPoll < TOUCH_POLL_INTERVAL_MS) {
    return;
  }
  lastTouchPoll = now;

  bool pressed = false;
  int16_t x = 0;
  int16_t y = 0;
  if (!readTouch(&pressed, &x, &y)) {
    // An occasional missed I2C response must not look like a finger release.
    return;
  }

  if (!pressed) {
    if (touchTracking && now - lastTouchPressedAt >= TOUCH_RELEASE_DEBOUNCE_MS) {
      const int16_t dx = touchLastX - touchStartX;
      const int16_t dy = touchLastY - touchStartY;
      const uint32_t duration = lastTouchPressedAt - touchStartedAt;
      Serial.printf("Touch up: dx=%d dy=%d duration=%lu ms\n", dx, dy,
                    static_cast<unsigned long>(duration));
      if (!gestureHandled && duration <= 750 &&
          abs(dx) < SWIPE_THRESHOLD && abs(dy) < SWIPE_THRESHOLD) {
        handleNeopixelTap(touchLastX, touchLastY);
      }
      touchTracking = false;
      gestureHandled = false;
    }
    return;
  }

  lastTouchPressedAt = now;
  if (!touchTracking) {
    touchTracking = true;
    gestureHandled = false;
    touchStartedAt = now;
    touchStartX = x;
    touchStartY = y;
    touchLastX = x;
    touchLastY = y;
    Serial.printf("Touch down: x=%d y=%d\n", x, y);
    return;
  }

  touchLastX = x;
  touchLastY = y;
  const int16_t dx = touchLastX - touchStartX;
  const int16_t dy = touchLastY - touchStartY;
  if (gestureHandled || now - touchStartedAt > SWIPE_MAX_DURATION_MS ||
      abs(dx) < SWIPE_THRESHOLD || abs(dx) <= abs(dy)) {
    return;
  }

  gestureHandled = true;
  Serial.printf("Swipe %s: dx=%d dy=%d\n", dx < 0 ? "left" : "right", dx,
                dy);
  if (dx < 0) {
    if (activeScreen == Screen::MOON) {
      setScreen(Screen::STATUS);
    } else if (activeScreen == Screen::STATUS) {
      setScreen(Screen::NEOPIXEL);
    }
  } else {
    if (activeScreen == Screen::NEOPIXEL) {
      setScreen(Screen::STATUS);
    } else if (activeScreen == Screen::STATUS) {
      setScreen(Screen::MOON);
    }
  }
}

void configureGps() {
  gpsConnected = gps.begin(GPS_ADDRESS);
  if (!gpsConnected) {
    Serial.println("MiniGPS not found at 0x10 on Wire1");
    showStatus("GPS not found", "Check Qwiic cable");
    return;
  }

  gps.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCGGA);
  gps.sendCommand(PMTK_SET_NMEA_UPDATE_1HZ);
  Serial.println("MiniGPS ready at 0x10 on Wire1");
  showStatus("Waiting for", "GPS satellite fix");
}

void setup() {
  Serial.begin(115200);
  const uint32_t serialStart = millis();
  while (!Serial && millis() - serialStart < 3000) {
    delay(10);
  }
  Serial.println("tiny-moon starting");

  // Keep both SPI devices deselected before bringing up their shared bus.
  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);

  while (!sd.begin(SdSpiConfig(SD_CS_PIN, SHARED_SPI, SD_SCK_MHZ(4), &SPI))) {
    Serial.println("Unable to access SD card; retrying...");
    delay(1000);
  }
  Serial.println("SD card ready");
  loadNeopixelSetting();
  configureNeopixel();

  tft.init();
  tft.setRotation(DISPLAY_ROTATION);
  showStatus("tiny-moon", "Starting...");
  configureTouch();

  if (USE_GPS) {
    configureGps();
  } else {
    lastFixedClockMillis = millis();
    Serial.printf(
        "GPS bypassed; using Gainesville, FL %.4f, %.4f\n",
        FIXED_LATITUDE, FIXED_LONGITUDE);
  }
}

void loop() {
  serviceTouch();

  if (!USE_GPS) {
    updateFixedClock();
    if (currentImage < 0 ||
        millis() - lastPhaseUpdate >= PHASE_UPDATE_INTERVAL_MS) {
      lastPhaseUpdate = millis();
      updateMoonImage();
    }
    return;
  }

  if (!gpsConnected) {
    if (millis() - lastGpsStatus >= 2000) {
      lastGpsStatus = millis();
      configureGps();
    }
    return;
  }

  gps.read();
  if (gps.newNMEAreceived() && !gps.parse(gps.lastNMEA())) {
    return;
  }

  if (!hasValidGpsData()) {
    if (millis() - lastGpsStatus >= GPS_STATUS_INTERVAL_MS) {
      lastGpsStatus = millis();
      Serial.printf("Waiting for GPS fix (%u satellites)\n", gps.satellites);
    }
    return;
  }

  if (currentImage < 0 ||
      millis() - lastPhaseUpdate >= PHASE_UPDATE_INTERVAL_MS) {
    lastPhaseUpdate = millis();
    updateMoonImage();
  }
}
