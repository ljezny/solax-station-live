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

#include "Solax/SolaxDongleDiscovery.hpp"
#include "Solax/SolaxDongleAPI.hpp"

SET_LOOP_TASK_STACK_SIZE(32 * 1024);

// TFT SPI
#define TFT_LED          33      // TFT backlight pin
#define TFT_LED_PWM      255     // dutyCycle 0-255 last minimum was 15

TFT_eSPI tft = TFT_eSPI();       // Invoke custom library with default width and height

#define TFT_GREY 0x7BEF

SolaxDongleAPI dongleAPI;
SolaxDongleDiscovery dongleDiscovery;

SolaxDongleInverterData_t inverterData;
SolaxDongleDiscoveryResult_t discoveryResult;


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
}

void loop() {
  discoveryResult = dongleDiscovery.discoverDongle();
  if(discoveryResult.result) {
    inverterData = dongleAPI.loadData(discoveryResult.sn);
  }

  drawDashboard();

  delay(WAIT);
}