#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GPS.h>
#include <SdFat.h>
#include <TFT_eSPI.h>
#include <PNGdec.h>
#include <astronomy.h>

constexpr uint8_t GPS_ADDRESS = 0x10;
constexpr uint8_t SD_CS_PIN = 27;  // QT Py A2, XIAO socket D2
constexpr uint16_t IMAGE_COUNT = 235;
constexpr uint16_t MAX_IMAGE_WIDTH = 240;
constexpr uint32_t PHASE_UPDATE_INTERVAL_MS = 60000;
constexpr uint32_t GPS_STATUS_INTERVAL_MS = 10000;

TFT_eSPI tft;
Adafruit_GPS gps(&Wire1);  // STEMMA QT is Wire1 on the QT Py RP2040.
SdFs sd;
FsFile imageFile;
PNG png;

bool gpsConnected = false;
int currentImage = -1;
uint32_t lastPhaseUpdate = 0;
uint32_t lastGpsStatus = 0;

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

int drawPngLine(PNGDRAW *line) {
  uint16_t lineBuffer[MAX_IMAGE_WIDTH];
  png.getLineAsRGB565(line, lineBuffer, PNG_RGB565_BIG_ENDIAN, 0xffffffff);
  tft.pushImage(0, line->y, line->iWidth, 1, lineBuffer);
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
  const int decodeResult = png.decode(nullptr, 0);
  png.close();

  if (decodeResult != PNG_SUCCESS) {
    Serial.printf("Could not decode %s (PNG error %d)\n", filename,
                  decodeResult);
    return false;
  }

  Serial.printf("Rendered %s in %lu ms\n", filename,
                static_cast<unsigned long>(millis() - startedAt));
  return true;
}

bool hasValidGpsData() {
  return gps.fix && gps.month >= 1 && gps.month <= 12 && gps.day >= 1 &&
         gps.day <= 31;
}

void updateMoonImage() {
  const astro_time_t time = Astronomy_MakeTime(
      2000 + gps.year, gps.month, gps.day, gps.hour, gps.minute,
      gps.seconds + gps.milliseconds / 1000.0);
  const astro_angle_result_t phase = Astronomy_MoonPhase(time);

  if (phase.status != ASTRO_SUCCESS) {
    Serial.printf("Moon phase calculation failed (status %d)\n", phase.status);
    return;
  }

  // Astronomy Engine returns 0=new, 90=first quarter, 180=full,
  // 270=last quarter. Divide that complete cycle across the SD image set.
  int imageNumber = 1 + static_cast<int>(phase.angle * IMAGE_COUNT / 360.0);
  if (imageNumber > IMAGE_COUNT) {
    imageNumber = IMAGE_COUNT;
  }

  Serial.printf(
      "GPS %04u-%02u-%02u %02u:%02u:%02u UTC, %.6f, %.6f; "
      "phase %.3f deg -> image %04d\n",
      2000 + gps.year, gps.month, gps.day, gps.hour, gps.minute, gps.seconds,
      gps.latitudeDegrees, gps.longitudeDegrees, phase.angle, imageNumber);

  if (imageNumber == currentImage) {
    return;
  }

  char filename[16];
  snprintf(filename, sizeof(filename), "%04d.png", imageNumber);
  if (renderImage(filename)) {
    currentImage = imageNumber;
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

  tft.init();
  tft.setRotation(1);
  showStatus("tiny-moon", "Starting GPS...");

  configureGps();
}

void loop() {
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
