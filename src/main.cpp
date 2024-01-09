
#include "Arduino.h"

#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#include "FS.h"
#include "SPIFFS.h"
#include "time.h"

// Eink display stuff
#include "SPI.h"
#include <GxDEPG0213BN/GxDEPG0213BN.h>
#include <GxEPD.h>
#include <GxIO/GxIO.h>
#include <GxIO/GxIO_SPI/GxIO_SPI.h>

// FreeFonts from Adafruit_GFX
#include "Fonts/Org_01.h"
#include <Fonts/FreeMonoBold9pt7b.h>

#include "https_request.h"

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

#define TIME_TO_SLEEP_MIN 15

#define SYMBOL_PATH       "/symbols.txt"

typedef enum {
  RIGHT_ALIGNMENT = 0,
  LEFT_ALIGNMENT,
  CENTER_ALIGNMENT,
} Text_alignment;

const char *ntpServer             = "pool.ntp.org"; // local ntp server
const uint32_t gmtOffset_sec      = 1 * 3600;       // GMT+01:00
const uint16_t daylightOffset_sec = 0;

GxIO_Class io(SPI, /*CS=5*/ ELINK_SS, /*DC=*/ELINK_DC, /*RST=*/ELINK_RESET);
GxEPD_Class display(io, /*RST=*/ELINK_RESET, /*BUSY=*/ELINK_BUSY);

static void displayText(const String &str, uint16_t y, uint8_t alignment)
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

static void draw_battery(uint32_t batt)
{
  const uint8_t start  = 225;
  const uint8_t height = 10;
  const uint8_t radius = 2;
  const uint8_t width  = 21;

  if (batt > 4000) {
    display.fillRoundRect(start, height, width, height, radius, GxEPD_BLACK);
  } else if (batt > 3500) {
    uint8_t full_width = width / 3 * 2;
    display.fillRoundRect(start, height, full_width, height, radius,
                          GxEPD_BLACK);
    display.drawRoundRect(start + full_width, height, width - full_width,
                          height, radius, GxEPD_BLACK);
  } else {
    uint8_t full_width = width / 3;
    display.fillRoundRect(start, height, full_width, height, radius,
                          GxEPD_BLACK);
    display.drawRoundRect(start + full_width, height, width - full_width,
                          height, radius, GxEPD_BLACK);
  }
}

static void update_battery()
{
  analogReadResolution(12);
  uint32_t batt_mv = analogReadMilliVolts(GPIO_NUM_35) * 2;
  log_i("Battery %lumV", batt_mv);
  draw_battery(batt_mv);
}

static void go_to_sleep(uint32_t sleep_time_min = TIME_TO_SLEEP_MIN)
{
  log_i("Going to sleep for %d minutes", sleep_time_min);

  display.powerDown();
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_39, LOW);
  esp_sleep_enable_timer_wakeup(sleep_time_min * 60 * 1000 * 1000);
  delay(500); // don't know if needed but....
  esp_deep_sleep_start();
}

static esp_err_t getTimeFromNTP()
{
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo)) {
    log_e("Failed to obtain time");
    return ESP_FAIL;
  }

  if (timeinfo.tm_hour > 17) {
    go_to_sleep(12 * 60);
  }

  char human_time[sizeof("08/04-08:04")] = {'\0'};
  sprintf(human_time, "%02d/%02d-%02d:%02d", timeinfo.tm_mday,
          timeinfo.tm_mon + 1, timeinfo.tm_hour, timeinfo.tm_min);
  displayText(human_time, 20, LEFT_ALIGNMENT);

  return ESP_OK;
}

static void handle_error(const char *err_msg)
{
  log_e("%s", err_msg);
  displayText(err_msg, 60, CENTER_ALIGNMENT);
  display.update();
  go_to_sleep();
}

static void ota_init(void)
{

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
      .onEnd([]() { log_i("End"); })
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
}

static void process_symbols(char *line)
{
  log_d("%s", line);

  static size_t y = 0;
  char *symbol_buf;
  char *data = strtok_r((char *)line, ";", &symbol_buf);

  if (data != NULL) {
    // i.e semicolon delimited 0AI4.LON;Carslberg;851.04
    float quote;

    if (https_request_get_symbol_quote(data, &quote) == ESP_FAIL) {
      handle_error("Failed to get data!");
    }

    log_i("%s value %.1f", data, quote);

    // Get friendly name
    data = strtok_r(NULL, ";", &symbol_buf);
    displayText(data, 50 + y * 20, LEFT_ALIGNMENT);

    // Get bought price
    char last_pc[sizeof("-1000.0%")];
    data = strtok_r(NULL, ";", &symbol_buf);
    sprintf(last_pc, "%.01f%%", (quote - atof(data)) / atof(data) * 100);
    displayText(last_pc, 50 + y * 20, RIGHT_ALIGNMENT);
    y++;
  }
}

void setup()
{
  Serial.begin(115200);

  log_i("Connecting to %s ", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  SPI.begin(SPI_CLK, SPI_MISO, SPI_MOSI, ELINK_SS);
  display.init(); // enable diagnostic output on Serial

  display.setRotation(1);
  display.setTextColor(GxEPD_BLACK);
  display.setFont(&Org_01);
  display.setCursor(0, 0);
  display.setTextSize(2);

  if (!SPIFFS.begin(true)) {
    handle_error("SPIFFS Mount Failed");
  }

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    if (millis() > 10 * 1000) {
      handle_error("Could not connect to WiFi");
    }
  }

  log_i("Connected!");

  ota_init();

  if (getTimeFromNTP() == ESP_FAIL) {
    handle_error("Could not get time");
  }

  File file = SPIFFS.open(SYMBOL_PATH);
  if (!file || file.isDirectory()) {
    handle_error("Failed to open file for reading");
  }

  size_t file_size = file.size();
  uint8_t *buf     = (uint8_t *)malloc(file_size);
  while (file.available()) {
    log_i("Read from file: %d bytes", file.read(buf, file_size));
  }
  file.close();

  char *line_buf;
  char *line = strtok_r((char *)buf, "\r\n", &line_buf);

  https_request_init();

  // @todo potentially dynamically change font if multiple lines
  display.setTextSize(3);

  while (line != NULL) {
    process_symbols(line);
    line = strtok_r(NULL, "\r\n", &line_buf);
  }

  update_battery();
  display.update();
  free(buf);

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason) {
  case ESP_SLEEP_WAKEUP_EXT0:
    log_i("Wakeup caused by external signal using RTC_IO");
    break;
  case ESP_SLEEP_WAKEUP_TIMER:
  default:
    log_i("Wakeup reason: %d", wakeup_reason);
    go_to_sleep();
    break;
  }
}

void loop()
{
  ArduinoOTA.handle();

  // keep on for X min for OTA and config
  if (millis() > 10 * 60 * 1000) {
    go_to_sleep();
  }
}