#include <Arduino.h>
#include <ESP_Panel_Library.h>
#include <lvgl.h>
#include "lvgl_port_v8.h"
#include "ui/ui.h"
#include "Solax/SolaxDongleDiscovery.hpp"
#include "Solax/SolaxDongleAPI.hpp"

#define WAIT 100

SolaxDongleAPI dongleAPI;
SolaxDongleDiscovery dongleDiscovery;

SolaxDongleInverterData_t inverterData;
SolaxDongleDiscoveryResult_t discoveryResult;

bool firstLoad = true;

//JEZNY - touches are disabled in ESP_PANEL_BOARD_CUSTOM for now!!!
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

void refreshDataTask( void * pvParameters )
{
    for( ;; )
    {
        discoveryResult = dongleDiscovery.discoverDongle();
        if (discoveryResult.result)
        {
            inverterData = dongleAPI.loadData(discoveryResult.sn);
        }

        delay(100);
    }
}

void setup()
{
    Serial.begin(115200);
  
    Serial.println("Initialize panel device");
    ESP_Panel *panel = new ESP_Panel();
    panel->init();
#if LVGL_PORT_AVOID_TEAR
    // When avoid tearing function is enabled, configure the RGB bus according to the LVGL configuration
    ESP_PanelBus_RGB *rgb_bus = static_cast<ESP_PanelBus_RGB *>(panel->getLcd()->getBus());
    rgb_bus->configRgbFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
    rgb_bus->configRgbBounceBufferSize(LVGL_PORT_RGB_BOUNCE_BUFFER_SIZE);
#endif
    panel->begin();
    lvgl_port_init(panel->getLcd(), panel->getTouch());
    
    lvgl_port_lock(-1);
    ui_init();
    lvgl_port_unlock();

    TaskHandle_t refreshDataTaskHandle = NULL;
    int ret = xTaskCreate(
                    refreshDataTask,       /* Function that implements the task. */
                    "refresh_data_task",          /* Text name for the task. */
                    32 * 1024,      /* Stack size in words, not bytes. */
                    ( void * ) 1,    /* Parameter passed into the task. */
                    tskIDLE_PRIORITY,/* Priority at which the task is created. */
                    &refreshDataTaskHandle);   
}

void loop()
{
    lvgl_port_lock(-1);
    if (firstLoad /*&& inverterData.status == 0*/)
    {
        lv_disp_load_scr(ui_Screen1);
        firstLoad = false;
    }

    updateUI();
    
    lv_timer_handler();

    lvgl_port_unlock();
    
    delay(1000);
}
