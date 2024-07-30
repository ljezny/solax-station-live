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

#define TFT_DISPLAY_RESOLUTION_X  480
#define TFT_DISPLAY_RESOLUTION_Y  320
#define CENTRE_Y                  TFT_DISPLAY_RESOLUTION_Y/2

#include <TFT_eSPI.h>            // Hardware-specific library
#include <SPI.h>
#include <Arduino.h>
#include "lv_conf.h"
#include "ui/ui.h"
#include "Solax/SolaxDongleDiscovery.hpp"
#include "Solax/SolaxDongleAPI.hpp"

SET_LOOP_TASK_STACK_SIZE(32 * 1024);

// TFT SPI
#define TFT_LED          33      // TFT backlight pin
#define TFT_LED_PWM      255     // dutyCycle 0-255 last minimum was 15

TFT_eSPI tft = TFT_eSPI();       // Invoke custom library with default width and height

#define TFT_GREY 0x7BEF

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[TFT_DISPLAY_RESOLUTION_X * 10];

SolaxDongleAPI dongleAPI;
SolaxDongleDiscovery dongleDiscovery;

SolaxDongleInverterData_t inverterData;
SolaxDongleDiscoveryResult_t discoveryResult;


// void drawDashboard() {
//   int margin = 16;
  
//   tft.fillRectVGradient(0, 0, 160, 320, TFT_ORANGE, TFT_YELLOW);
//   tft.setFreeFont(&FreeSansBold24pt7b);
//   tft.setTextColor(TFT_WHITE);
//   tft.setCursor(margin, margin + FreeSansBold24pt7b.yAdvance);
//   tft.print("PV");
  
//   tft.setCursor(margin, 2 * (margin + FreeSansBold24pt7b.yAdvance));
//   //tft.printf("%d kW", inverterData.yieldToday);
  

//   tft.fillRect(160, 0, 480 - 160, 320, TFT_WHITE);
//   tft.setTextColor(TFT_BLACK);

//   tft.setFreeFont(&FreeSansBold12pt7b);
//   tft.setCursor(160 + margin, FreeSansBold12pt7b.yAdvance + margin);
//   tft.printf("%d W", inverterData.pv1Power);
//   tft.setCursor(325 + margin, margin + FreeSansBold12pt7b.yAdvance);
//   tft.printf("%d W", inverterData.pv1Power);
//   tft.setFreeFont(&FreeSansBold24pt7b);
//   tft.setCursor(160 + margin, margin + FreeSansBold12pt7b.yAdvance + margin + FreeSansBold24pt7b.yAdvance);
//   tft.printf("%d W", inverterData.pv1Power + inverterData.pv2Power);
//   tft.setCursor(160 + margin, margin + FreeSansBold12pt7b.yAdvance + margin + FreeSansBold24pt7b.yAdvance * 2 + margin);
//   tft.printf("%d %%", inverterData.soc);

//   tft.setFreeFont(&FreeSansBold12pt7b);
//   tft.setCursor(160 + margin, margin + FreeSansBold12pt7b.yAdvance * 2 + margin + FreeSansBold24pt7b.yAdvance * 2 + margin * 2);
//   tft.printf("%d W", inverterData.L1Power);
//   tft.setCursor(160 + 106 * 1 + margin, margin + FreeSansBold12pt7b.yAdvance * 2 + margin + FreeSansBold24pt7b.yAdvance * 2 + margin * 2);
//   tft.printf("%d W", inverterData.L2Power);
//   tft.setCursor(160 + 106 * 2 + margin, margin + FreeSansBold12pt7b.yAdvance * 2 + margin + FreeSansBold24pt7b.yAdvance * 2 + margin * 2);
//   tft.printf("%d W", inverterData.L3Power);

//   tft.setFreeFont(&FreeSansBold24pt7b);
//   tft.setCursor(160 + margin, margin + FreeSansBold12pt7b.yAdvance * 2 + margin + FreeSansBold24pt7b.yAdvance * 3 + margin * 2);
//   //tft.printf("%d W", inverterData.feedInPower);

//   tft.setFreeFont(&FreeSansBold9pt7b);
//   tft.setCursor(170, 310);
//   tft.print(WiFi.status() == WL_CONNECTED ? "Status: " + String(inverterData.status) : "WiFi disconnected");   
// }

/* Display flushing */
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) 
{
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)&color_p->full, w * h, true);
  tft.endWrite();

  lv_disp_flush_ready(disp);
}

/*Read the touchpad*/
void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) 
{
  // if (!ts.touched()) 
  // {
  //   data->state = LV_INDEV_STATE_REL;
  // } 
  // else 
  // {
  //   uint16_t touchX, touchY;
  //   // Retrieve a point
  //   TS_Point p = ts.getPoint();
  //   touchX = p.x;
  //   touchY = p.y;

  //   data->state = LV_INDEV_STATE_PR;

  //   /*Set the coordinates*/
  //   data->point.x = touchX;
  //   data->point.y = touchY;
  // }
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
  //tft.fillScreen(TFT_WHITE);
  lv_init();
  
  lv_disp_draw_buf_init(&draw_buf, buf, NULL, TFT_DISPLAY_RESOLUTION_X * 10);

   /*Initialize the display*/
  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  /*Change the following line to your display resolution*/
  disp_drv.hor_res = TFT_DISPLAY_RESOLUTION_X;
  disp_drv.ver_res = TFT_DISPLAY_RESOLUTION_Y;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  /*Initialize touch driver*/
  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);

  ui_init();
}

void loop() {
  // discoveryResult = dongleDiscovery.discoverDongle();
  // if(discoveryResult.result) {
  //   inverterData = dongleAPI.loadData(discoveryResult.sn);
  // }
  lv_timer_handler(); /* let the GUI do its work */
  lv_label_set_text(ui_Label1, String(millis()).c_str());
  lv_refr_now(NULL);
  //delay(WAIT);
}