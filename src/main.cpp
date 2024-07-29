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
#define WAIT 1000                // Delay between tests, set to 0 to demo speed, 2000 to see what it does!

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
#define TFT_LED_PWM      200     // dutyCycle 0-255 last minimum was 15

TFT_eSPI tft = TFT_eSPI();       // Invoke custom library with default width and height

#define TFT_GREY 0x7BEF

typedef struct {
  int status = -1;
  int pv1Power;
  int pv2Power;
  int soc;
  int batteryPower;

  int inverterTemperature;
  int batteryTemperature;
} InverterData_t;

InverterData_t inverterData;

void drawDashboard() {
  tft.fillScreen(TFT_WHITE);
  tft.fillRect(0, 0, 160, 320, TFT_ORANGE);
  tft.setTextSize(56);

  tft.setTextColor(TFT_WHITE);
  tft.setCursor(50, 50);
  
  tft.print("PV");

  tft.setTextColor(TFT_BLACK);
  tft.setCursor(170, 50);
  tft.printf("%d W", inverterData.pv1Power);
  tft.setCursor(170, 150);
  tft.printf("%d W", inverterData.pv1Power);
  tft.setCursor(170, 250);
  tft.printf("%d %%", inverterData.soc);
  
  tft.setTextSize(0);

  tft.setCursor(50, 280);
  tft.print(WiFi.status() == WL_CONNECTED ? "Status: " + String(inverterData.status) : "WiFi disconnected");   
}

InverterData_t loadData() {
  String url = "http://5.8.8.8";
  HTTPClient http;
  if (http.begin(url)) {
    int httpCode = http.POST("optType=ReadRealTimeData&pwd=502200");
    if (httpCode == HTTP_CODE_OK) {
      StaticJsonDocument<8192> doc;
      String payload = http.getString();
      DeserializationError err = deserializeJson(doc, payload);
      if(err == DeserializationError::Ok) {
        inverterData.status = 0;
        inverterData.pv1Power = doc["Data"][14].as<int>();
        inverterData.pv2Power = doc["Data"][15].as<int>();
        inverterData.batteryPower = doc["Data"][15].as<int>();
        inverterData.soc = doc["Data"][103].as<int>();
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
  
  WiFi.begin("Wifi_SXBYETVWHZ");
}

void loop() {
  if(WiFi.status() == WL_CONNECTED) {
    inverterData = loadData();
  }
  drawDashboard();
  delay(WAIT);
}