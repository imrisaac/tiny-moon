// Test code for Adafruit GPS That Support Using I2C
//
// This code shows how to parse data from the I2C GPS
//
// Pick one up today at the Adafruit electronics shop
// and help support open source hardware & software! -ada

#include <Adafruit_GPS.h>
#include "SPI.h"
#include <TFT_eSPI.h>              // Hardware-specific library
#include <SD.h>
#include "I2C_BM8563.h"
#include <PNGdec.h>
#include <astronomy.h>

#define MAX_IMAGE_WIDTH 240

TFT_eSPI tft = TFT_eSPI();
Adafruit_GPS GPS(&Wire);
I2C_BM8563 rtc(I2C_BM8563_DEFAULT_ADDRESS, Wire);

// Set GPSECHO to 'false' to turn off echoing the GPS data to the Serial console
// Set to 'true' if you want to debug and listen to the raw GPS sentences
#define GPSECHO false

File myfile;
PNG png; // PNG decoder inatance
uint32_t timer = millis();
int16_t image_xpos = 0;
int16_t image_ypos = 0;
astro_time_t timeInfoRTC;
astro_time_t timeInfoGPS;
astro_time_t timeInfoFile;

void setup()
{
  //while (!Serial);  // uncomment to have the sketch wait until Serial is ready

  // connect at 115200 so we can read the GPS fast enough and echo without dropping chars
  // also spit it out
  Serial.begin(115200);
  Serial.println("Adafruit I2C GPS library basic test!");

  tft.begin();
  tft.setRotation(1);

  while (!SD.begin(D2)) {
    Serial.println("Unable to access SD Card");
    tft.println("Unable to access SD Card");
    delay(1000);
  }

  // 9600 NMEA is the default baud rate for Adafruit MTK GPS's- some use 4800
  GPS.begin(0x10);  // The I2C address to use is 0x10
  // uncomment this line to turn on RMC (recommended minimum) and GGA (fix data) including altitude
  GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCGGA);
  // uncomment this line to turn on only the "minimum recommended" data
  //GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCONLY);
  // For parsing data, we don't suggest using anything but either RMC only or RMC+GGA since
  // the parser doesn't care about other sentences at this time
  // Set the update rate
  GPS.sendCommand(PMTK_SET_NMEA_UPDATE_1HZ); // 1 Hz update rate
  // For the parsing code to work nicely and have time to sort thru the data, and
  // print it out we don't suggest using anything higher than 1 Hz

  // Request updates on antenna status, comment out to keep quiet
  GPS.sendCommand(PGCMD_ANTENNA);

  delay(1000);

  // Ask for firmware version
  GPS.println(PMTK_Q_RELEASE);
}

void loop() // run over and over again
{
  int rc, filecount = 0;
  File dir = SD.open("/");
  astro_time_t time;
  astro_utc_t utc;
  astro_angle_result_t phase;
  astro_moon_quarter_t mq;
  astro_illum_t illum;

  while(1){
    // read data from the GPS in the 'main loop'
    char c = GPS.read();
    // if you want to debug, this is a good time to do it!
    if (GPSECHO)
      if (c) Serial.print(c);
    // if a sentence is received, we can check the checksum, parse it...
    if (GPS.newNMEAreceived()) {
      // a tricky thing here is if we print the NMEA sentence, or data
      // we end up not listening and catching other sentences!
      // so be very wary if using OUTPUT_ALLDATA and trying to print out data
      Serial.println(GPS.lastNMEA()); // this also sets the newNMEAreceived() flag to false
      if (!GPS.parse(GPS.lastNMEA())) // this also sets the newNMEAreceived() flag to false
        return; // we can fail to parse a sentence in which case we should just wait for another
    }

    // approximately every 2 seconds or so, print out the current stats
    if (millis() - timer > 2000) {
      timer = millis(); // reset the timer
      Serial.print("\nTime: ");
      if (GPS.hour < 10) { Serial.print('0'); }
      Serial.print(GPS.hour, DEC); Serial.print(':');
      if (GPS.minute < 10) { Serial.print('0'); }
      Serial.print(GPS.minute, DEC); Serial.print(':');
      if (GPS.seconds < 10) { Serial.print('0'); }
      Serial.print(GPS.seconds, DEC); Serial.print('.');
      if (GPS.milliseconds < 10) {
        Serial.print("00");
      } else if (GPS.milliseconds > 9 && GPS.milliseconds < 100) {
        Serial.print("0");
      }
      Serial.println(GPS.milliseconds);
      Serial.print("Date: ");
      Serial.print(GPS.day, DEC); Serial.print('/');
      Serial.print(GPS.month, DEC); Serial.print("/20");
      Serial.println(GPS.year, DEC);
      Serial.print("Fix: "); Serial.print((int)GPS.fix);
      Serial.print(" quality: "); Serial.println((int)GPS.fixquality);
      if (GPS.fix) {
        Serial.print("Location: ");
        Serial.print(GPS.latitude, 4); Serial.print(GPS.lat);
        Serial.print(", ");
        Serial.print(GPS.longitude, 4); Serial.println(GPS.lon);
        Serial.print("Speed (knots): "); Serial.println(GPS.speed);
        Serial.print("Angle: "); Serial.println(GPS.angle);
        Serial.print("Altitude: "); Serial.println(GPS.altitude);
        Serial.print("Satellites: "); Serial.println((int)GPS.satellites);
      }
      
      utc.year = GPS.year+2000;
      utc.month = GPS.month;
      utc.day = GPS.day;
      utc.hour = GPS.hour;
      utc.minute = GPS.minute;
      utc.second = GPS.seconds;
      Serial.printf("utc gps   = %d-%d-%d %d:%d:%d\n", GPS.year, GPS.month, GPS.day, GPS.hour, GPS.minute, GPS.seconds);
      Serial.printf("utc astro = %d-%d-%d %d:%d:%d\n", utc.year, utc.month, utc.day, utc.hour, utc.minute, utc.second);

      time = Astronomy_TimeFromUtc(utc);
      phase = Astronomy_MoonPhase(time);
      Serial.println(time.ut);
      
      illum = Astronomy_Illumination(BODY_MOON, time);
      if (illum.status != ASTRO_SUCCESS){
        Serial.printf("Astronomy_Illumination error %d\n", illum.status);
      }
      Serial.printf(" : Moon's illuminated fraction = %0.2lf%%.", 100.0 * illum.phase_fraction);
      
      Serial.print("Moon Phase: "); 
      Serial.println(phase.angle);

      File entry = dir.openNextFile();
      if (!entry) break;
      if (entry.isDirectory() == false) {
        const char *name = entry.name();
        const int len = strlen(name);
        if (len > 3 && strcmp(name + len - 3, "png") == 0) {
          Serial.print("File: ");
          Serial.println(name);
          rc = png.open((const char *)name, myOpen, myClose, myRead, mySeek, pngDraw);
          if (rc == PNG_SUCCESS) {
            uint32_t dt = millis();
            rc = png.decode(NULL, 0);
            Serial.print(millis() - dt); Serial.println("ms");
          }
          filecount = filecount + 1;
          //delay(2000);
        }
      }
      entry.close();
    }
  }
}

void * myOpen(const char *filename, int32_t *size) {
  Serial.printf("Attempting to open %s\n", filename);
  myfile = SD.open(filename);
  *size = myfile.size();
  return &myfile;
}
void myClose(void *handle) {
  if (myfile) myfile.close();
}
int32_t myRead(PNGFILE *handle, uint8_t *buffer, int32_t length) {
  if (!myfile) return 0;
  return myfile.read(buffer, length);
}
int32_t mySeek(PNGFILE *handle, int32_t position) {
  if (!myfile) return 0;
  return myfile.seek(position);
}

//=========================================v==========================================
//                                      pngDraw
//====================================================================================
// This next function will be called during decoding of the png file to
// render each image line to the TFT.  If you use a different TFT library
// you will need to adapt this function to suit.
// Callback function to draw pixels to the display
void pngDraw(PNGDRAW *pDraw) {
  int16_t xpos = 0;
  int16_t ypos = 0;
  uint16_t lineBuffer[MAX_IMAGE_WIDTH];
  png.getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_BIG_ENDIAN, 0xffffffff);
  tft.pushImage(xpos, ypos + pDraw->y, pDraw->iWidth, 1, lineBuffer);
}