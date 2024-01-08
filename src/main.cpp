
#include "Arduino.h"

#include "SD.h"
#include "SPI.h"
#include <GxEPD.h>

#include "time.h"
#include <WiFi.h>

#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>

#include "FS.h"
#include <LittleFS.h>

#include "https_request.h"

const char *ntpServer             = "pool.ntp.org"; // local ntp server
const uint32_t gmtOffset_sec      = 1 * 3600;       // GMT+08:00
const uint16_t daylightOffset_sec = 0;

#include <GxDEPG0213BN/GxDEPG0213BN.h>

#include <Fonts/FreeMonoBold18pt7b.h>

#include <GxIO/GxIO.h>
#include <GxIO/GxIO_SPI/GxIO_SPI.h>

#define SPI_MOSI          23
#define SPI_MISO          -1
#define SPI_CLK           18

#define ELINK_SS          5
#define ELINK_BUSY        4
#define ELINK_RESET       16
#define ELINK_DC          17

#define SDCARD_SS         13
#define SDCARD_CLK        14
#define SDCARD_MOSI       15
#define SDCARD_MISO       2

#define BUTTON_PIN        39

#define TIME_TO_SLEEP_MIN 15

typedef enum {
  RIGHT_ALIGNMENT = 0,
  LEFT_ALIGNMENT,
  CENTER_ALIGNMENT,
} Text_alignment;

GxIO_Class io(SPI, /*CS=5*/ ELINK_SS, /*DC=*/ELINK_DC, /*RST=*/ELINK_RESET);
GxEPD_Class display(io, /*RST=*/ELINK_RESET, /*BUSY=*/ELINK_BUSY);

const uint8_t Whiteboard[1700] = {0x00};

uint16_t Year = 0, Month = 0, Day = 0, Hour = 0, Minute = 0, Second = 0;

void displayText(const String &str, uint16_t y, uint8_t alignment)
{
  int16_t x = 0;
  int16_t x1, y1;
  uint16_t w, h;
  display.setCursor(x, y);
  display.getTextBounds(str, x, y, &x1, &y1, &w, &h);

  switch (alignment) {
  case RIGHT_ALIGNMENT:
    display.setCursor(display.width() - w - x1, y);
    break;
  case LEFT_ALIGNMENT:
    display.setCursor(0, y);
    break;
  case CENTER_ALIGNMENT:
    display.setCursor(display.width() / 2 - ((w + x1) / 2), y);
    break;
  default:
    break;
  }

  display.println(str);
}

esp_err_t getTimeFromNTP()
{
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo)) {
    log_e("Failed to obtain time");
    return ESP_FAIL;
  }

  return ESP_OK;
}

void setup()
{
  Serial.begin(115200);

  if (!LittleFS.begin(true)) {
    log_e("LittleFS Mount Failed");
    assert(false);
  }

  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason) {
  case ESP_SLEEP_WAKEUP_EXT0:
    log_i("Wakeup caused by external signal using RTC_IO");
    break;
  case ESP_SLEEP_WAKEUP_EXT1:
    log_i("Wakeup caused by external signal using RTC_CNTL");
    break;
  case ESP_SLEEP_WAKEUP_TIMER:
    log_i("Wakeup caused by timer");
    break;
  default:
    log_i("Wakeup was not caused by deep sleep: %d\n", wakeup_reason);
    break;
  }

  log_i("Connecting to %s ", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  log_i(" CONNECTED");

  ArduinoOTA.setHostname(HOSTNAME);

  ArduinoOTA
      .onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
          type = "sketch";
        else // U_SPIFFS
          type = "filesystem";

        // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS
        // using SPIFFS.end()
        log_i("Start updating %s", type.c_str());
      })
      .onEnd([]() { log_i("\nEnd"); })
      .onProgress([](unsigned int progress, unsigned int total) {
        log_i("Progress: %u%%\r", (progress / (total / 100)));
      })
      .onError([](ota_error_t error) {
        log_e("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR)
          log_e("Auth Failed");
        else if (error == OTA_BEGIN_ERROR)
          log_e("Begin Failed");
        else if (error == OTA_CONNECT_ERROR)
          log_e("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR)
          log_e("Receive Failed");
        else if (error == OTA_END_ERROR)
          log_e("End Failed");
      });

  ArduinoOTA.begin();

  SPI.begin(SPI_CLK, SPI_MISO, SPI_MOSI, ELINK_SS);
  display.init(); // enable diagnostic output on Serial

  display.setRotation(1);
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);
  display.setFont(&FreeMonoBold18pt7b);
  display.setCursor(0, 0);

  display.fillScreen(GxEPD_WHITE);
  display.update();
}

void loop()
{
  if (getTimeFromNTP() == ESP_FAIL) {
    displayText("Could not get time", 60, CENTER_ALIGNMENT);
    display.updateWindow(22, 30, 222, 90, true);
    display.drawBitmap(Whiteboard, 22, 31, 208, 60, GxEPD_BLACK);
  }

  displayText("ok", 60, CENTER_ALIGNMENT);
  display.updateWindow(22, 30, 222, 90, true);
  display.drawBitmap(Whiteboard, 22, 31, 208, 60, GxEPD_BLACK);

  ArduinoOTA.handle();

  https_request_init();

  esp_sleep_enable_ext0_wakeup(GPIO_NUM_39, LOW);
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP_MIN * 60 * 1000 * 1000);
  esp_deep_sleep_start();
}