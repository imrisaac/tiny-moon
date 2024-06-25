#include <Adafruit_GPS.h>
#include "SPI.h"
#include <SD.h>
#include <TFT_eSPI.h>
#include <PNGdec.h>
#include <astronomy.h>

#define MAX_IMAGE_WIDTH 240

TFT_eSPI tft = TFT_eSPI();

File myfile;
PNG png; // PNG decoder instance

void setup() {
  Serial.begin(115200);
  while (!Serial);  // wait for Serial to be ready
  Serial.println("Serial connected");

  tft.init();
  tft.fillScreen(0);
  tft.setRotation(1);

  pinMode(D2, OUTPUT);
  while (!SD.begin(D2)) {
    Serial.println("Unable to access SD Card");
    tft.println("Unable to access SD Card");
    delay(1000); // Add delay to avoid flooding the serial output
  }
  Serial.println("SD Card access OK");
}

void * myOpen(const char *filename, int32_t *size) {
  Serial.printf("Attempting to open %s...", filename);
  myfile = SD.open(filename);
  if (!myfile) {
    Serial.printf("Failed to open file: %s\n", filename);
    *size = 0;
    return NULL;
  }
  *size = myfile.size();
  Serial.printf("File size: %d bytes\n", *size);
  return &myfile;
}

void myClose(void *handle) {
  if (myfile) myfile.close();
  Serial.println("File closed");
}

int32_t myRead(PNGFILE *handle, uint8_t *buffer, int32_t length) {
  Serial.printf("Reading %d bytes from file...\n", length);
  if (!myfile) return 0;
  int32_t bytesRead = myfile.read(buffer, length);
  Serial.printf("Read %d bytes\n", bytesRead);
  return bytesRead;
}

int32_t mySeek(PNGFILE *handle, int32_t position) {
  Serial.printf("Seeking to position %d...\n", position);
  if (!myfile) return 0;
  bool success = myfile.seek(position);
  Serial.printf("Seek %s\n", success ? "successful" : "failed");
  return success ? position : -1;
}

void pngDraw(PNGDRAW *pDraw) {
  Serial.printf("Drawing PNG line at y=%d\n", pDraw->y);
  uint16_t lineBuffer[MAX_IMAGE_WIDTH];
  Serial.println("Getting line as RGB565...");
  png.getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_BIG_ENDIAN, 0xffffffff);
  Serial.println("Pushing image to TFT...");
  tft.pushImage(0, pDraw->y, pDraw->iWidth, 1, lineBuffer);  // Assuming image_xpos and image_ypos are always 0
  Serial.println("Line drawn");
}

void loop() {
  int rc;
  File dir = SD.open("/");

  while (1) {
    File entry = dir.openNextFile();
    if (!entry) {
      Serial.println("No more files.");
      break;
    }
    if (!entry.isDirectory()) {
      const char *name = entry.name();
      const int len = strlen(name);
      if (len > 3 && strcmp(name + len - 3, "png") == 0) {
        Serial.printf("Found PNG file: %s\n", name);
        rc = png.open(name, myOpen, myClose, myRead, mySeek, pngDraw);
        if (rc == PNG_SUCCESS) {
          uint32_t dt = millis();
          Serial.println("Starting PNG decode...");
          rc = png.decode(NULL, 0);
          Serial.printf("Decoding took %d ms\n", millis() - dt);
          if (rc != PNG_SUCCESS) {
            Serial.printf("Error decoding PNG file: %s, rc=%d\n", name, rc);
          } else {
            Serial.println("PNG decoding successful");
          }
        } else {
          Serial.printf("Error opening PNG file: %s, rc=%d\n", name, rc);
        }
        delay(2000);
      }
    }
    entry.close();
  }
}
