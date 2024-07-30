/* 
 * Display only test for LaskaKit ESPD-3.5" 320x480, ILI9488 
 * example from TFT_eSPI library is used
 * 
 * How to steps:
 * 1. Copy file Setup300_ILI9488_ESPD-3_5.h from https://github.com/LaskaKit/ESPD-35/tree/main/SW to Arduino/libraries/TFT_eSPI/User_Setups/
 * 2. in Arduino/libraries/TFT_eSPI/User_Setup_Select.h 
      a. comment: #include <User_Setup.h> 
      b. add: #include <User_Setups/Setup300_ILI9488_ESPD-3_5.h>  // Setup file for LaskaKit ESPD-3.5" 320x480, ILI9488 
 * 
 * Email:podpora@laskakit.cz
 * Web:laskakit.cz
*/

// Delay between demo pages
#define WAIT 2000                // Delay between tests, set to 0 to demo speed, 2000 to see what it does!

#define TFT_DISPLAY_RESOLUTION_X  320
#define TFT_DISPLAY_RESOLUTION_Y  480
#define CENTRE_Y                  TFT_DISPLAY_RESOLUTION_Y/2

#include <TFT_eSPI.h>            // Hardware-specific library
#include <SPI.h>
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>

SET_LOOP_TASK_STACK_SIZE(32 * 1024);

// TFT SPI
#define TFT_LED          33      // TFT backlight pin
#define TFT_LED_PWM      255     // dutyCycle 0-255 last minimum was 15

TFT_eSPI tft = TFT_eSPI();       // Invoke custom library with default width and height

#define TFT_GREY 0x7BEF

typedef struct {
  int status = -1;
  int pv1Power;
  int pv2Power;
  int soc;
  int batteryPower;
  int L1Power;
  int L2Power;
  int L3Power;
  int32_t feedInPower;
  int batteryTemperature;
  double yieldToday;
  uint32_t yieldTotal;
} InverterData_t;

InverterData_t inverterData;

uint32_t read32BitUnsigned(uint32_t a, uint32_t b) {
  return b + 65536 * a;
}

int32_t read32BitSigned(int32_t a, int32_t b) {
  if (a < 32768) {
    return b + 65536 * a;
  } else {
    return b + 65536 * a - 4294967296;
  }
}

void drawDashboard() {
  int margin = 16;
  
  tft.fillRectVGradient(0, 0, 160, 320, TFT_ORANGE, TFT_YELLOW);
  tft.setFreeFont(&FreeSansBold24pt7b);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(margin, margin + FreeSansBold24pt7b.yAdvance);
  tft.print("PV");
  
  tft.setCursor(margin, 2 * (margin + FreeSansBold24pt7b.yAdvance));
  //tft.printf("%d kW", inverterData.yieldToday);
  

  tft.fillRect(160, 0, 480 - 160, 320, TFT_WHITE);
  tft.setTextColor(TFT_BLACK);

  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setCursor(160 + margin, FreeSansBold12pt7b.yAdvance + margin);
  tft.printf("%d W", inverterData.pv1Power);
  tft.setCursor(325 + margin, margin + FreeSansBold12pt7b.yAdvance);
  tft.printf("%d W", inverterData.pv1Power);
  tft.setFreeFont(&FreeSansBold24pt7b);
  tft.setCursor(160 + margin, margin + FreeSansBold12pt7b.yAdvance + margin + FreeSansBold24pt7b.yAdvance);
  tft.printf("%d W", inverterData.pv1Power + inverterData.pv2Power);
  tft.setCursor(160 + margin, margin + FreeSansBold12pt7b.yAdvance + margin + FreeSansBold24pt7b.yAdvance * 2 + margin);
  tft.printf("%d %%", inverterData.soc);

  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.setCursor(160 + margin, margin + FreeSansBold12pt7b.yAdvance * 2 + margin + FreeSansBold24pt7b.yAdvance * 2 + margin * 2);
  tft.printf("%d W", inverterData.L1Power);
  tft.setCursor(160 + 106 * 1 + margin, margin + FreeSansBold12pt7b.yAdvance * 2 + margin + FreeSansBold24pt7b.yAdvance * 2 + margin * 2);
  tft.printf("%d W", inverterData.L2Power);
  tft.setCursor(160 + 106 * 2 + margin, margin + FreeSansBold12pt7b.yAdvance * 2 + margin + FreeSansBold24pt7b.yAdvance * 2 + margin * 2);
  tft.printf("%d W", inverterData.L3Power);

  tft.setFreeFont(&FreeSansBold24pt7b);
  tft.setCursor(160 + margin, margin + FreeSansBold12pt7b.yAdvance * 2 + margin + FreeSansBold24pt7b.yAdvance * 3 + margin * 2);
  //tft.printf("%d W", inverterData.feedInPower);

  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setCursor(170, 310);
  tft.print(WiFi.status() == WL_CONNECTED ? "Status: " + String(inverterData.status) : "WiFi disconnected");   
}

InverterData_t loadData() {
  String url = "http://5.8.8.8";
  HTTPClient http;
  if (http.begin(url)) {
    int httpCode = http.POST("optType=ReadRealTimeData&pwd=SXBYETVWHZ");
    if (httpCode == HTTP_CODE_OK) {
      StaticJsonDocument<8192> doc;
      String payload = http.getString();
      DeserializationError err = deserializeJson(doc, payload);
      if(err == DeserializationError::Ok) {
        inverterData.status = 0;
        inverterData.pv1Power = doc["Data"][14].as<int>();
        inverterData.pv2Power = doc["Data"][15].as<int>();
        inverterData.batteryPower = doc["Data"][41].as<int>();
        inverterData.batteryTemperature = doc["Data"][105].as<int>();
        inverterData.L1Power = doc["Data"][6].as<int>();
        inverterData.L2Power = doc["Data"][7].as<int>();
        inverterData.L3Power = doc["Data"][8].as<int>();
        inverterData.soc = doc["Data"][103].as<int>();
        inverterData.yieldToday = doc["Data"][70].as<double>() / 10.0;
        inverterData.yieldTotal = read32BitUnsigned(doc["Data"][68].as<uint32_t>(), doc["Data"][69].as<uint32_t>());
        inverterData.feedInPower = read32BitSigned(doc["Data"][34].as<int32_t>(), doc["Data"][35].as<int32_t>());
      } else {
        inverterData.status = -3;
      }
    } else {
      inverterData.status = -2;
    }
    http.end();
  } else {
    inverterData.status = -1;
  }
  return inverterData;
}

void setup() {
// configure backlight LED PWM functionalitites
  ledcSetup(1, 5000, 8);              // ledChannel, freq, resolution
  ledcAttachPin(TFT_LED, 1);          // ledPin, ledChannel
  ledcWrite(1, TFT_LED_PWM);          // dutyCycle 0-255

  Serial.begin(115200);
  delay(3000);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_WHITE);

  WiFi.begin("Wifi_SXBYETVWHZ");
}

void loop() {
  if(WiFi.status() == WL_CONNECTED) {
    inverterData = loadData();
  }
  drawDashboard();
  delay(WAIT);
}