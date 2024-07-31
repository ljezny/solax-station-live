#define WAIT 1000

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

bool firstLoad = true;

/* Display flushing */
void display_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) 
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

void updateUI() {
  lv_label_set_text_fmt(ui_pvLabel, "%d W", inverterData.pv1Power + inverterData.pv2Power);
  lv_label_set_text_fmt(ui_pvStringsLabel, "%d W  %d W", inverterData.pv1Power, inverterData.pv2Power);
  lv_label_set_text_fmt(ui_loadLabel, "%d W", inverterData.loadPower);
  lv_label_set_text_fmt(ui_l1Label, "%d W", inverterData.L1Power);
  lv_label_set_text_fmt(ui_l2Label, "%d W", inverterData.L2Power);
  lv_label_set_text_fmt(ui_l3Label, "%d W", inverterData.L3Power);
  lv_label_set_text_fmt(ui_feedinLabel, "%d W", inverterData.feedInPower);
  lv_label_set_text_fmt(ui_socLabel, "%d %%", inverterData.soc);
  lv_label_set_text_fmt(ui_batteryPowerLabel, "%d W", inverterData.batteryPower);
  lv_label_set_text_fmt(ui_pvTodayYield, "%s kWh", String(inverterData.yieldToday,1).c_str());
  lv_label_set_text_fmt(ui_loadTodayLabel, "%s kWh", String(inverterData.loadToday,1).c_str());
  lv_label_set_text_fmt(ui_gridSellTodayLabel, "+ %s kWh", String(inverterData.gridSellToday,1).c_str());
  lv_label_set_text_fmt(ui_gridBuyTodayLabel, "- %s kWh", String(inverterData.gridBuyToday,1).c_str());
  lv_label_set_text_fmt(ui_batteryChargedTodayLabel, "+ %s kWh", String(inverterData.batteryChargedToday,1).c_str());
  lv_label_set_text_fmt(ui_batteryDischargedTodayLabel, "- %s kWh", String(inverterData.batteryDischargedToday,1).c_str());
  lv_label_set_text(ui_statusLabel, discoveryResult.result ? discoveryResult.sn.c_str() : "Disconnected");
  lv_refr_now(NULL);
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

  lv_init();
  
  lv_disp_draw_buf_init(&draw_buf, buf, NULL, TFT_DISPLAY_RESOLUTION_X * 10);

   /*Initialize the display*/
  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  /*Change the following line to your display resolution*/
  disp_drv.hor_res = TFT_DISPLAY_RESOLUTION_X;
  disp_drv.ver_res = TFT_DISPLAY_RESOLUTION_Y;
  disp_drv.flush_cb = display_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  /*Initialize touch driver*/
  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);

  ui_init();

  updateUI();
  lv_timer_handler();
}

void loop() {
  discoveryResult = dongleDiscovery.discoverDongle();
  if(discoveryResult.result) {
    inverterData = dongleAPI.loadData(discoveryResult.sn);
  }

  if(firstLoad && inverterData.status == 0) {
    lv_disp_load_scr( ui_Dashboard);
    firstLoad = false;
  }
  
  updateUI();

  lv_timer_handler(); /* let the GUI do its work */
  delay(WAIT);
}