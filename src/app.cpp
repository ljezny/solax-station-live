#include <Arduino.h>
#include <ESP_Panel_Library.h>
#include <lvgl.h>
#include "lvgl_port_v8.h"
#include "ui/ui.h"
#include "Solax/SolaxDongleDiscovery.hpp"
#include "Solax/SolaxDongleAPI.hpp"

#define WAIT 100
SET_LOOP_TASK_STACK_SIZE(32 * 1024);

SolaxDongleAPI dongleAPI;
SolaxDongleDiscovery dongleDiscovery;

SolaxDongleInverterData_t inverterData;
SolaxDongleDiscoveryResult_t discoveryResult;

bool firstLoad = true;

SolaxDongleInverterData_t createRandomMockData()
{
    SolaxDongleInverterData_t inverterData;
    inverterData.status = 0;
    inverterData.pv1Power = random(0, 1000);
    inverterData.pv2Power = random(0, 1000);
    inverterData.batteryPower = random(-1000, 1000);
    inverterData.batteryTemperature = random(0, 50);
    inverterData.inverterTemperature = random(0, 50);
    inverterData.L1Power = random(0, 1000);
    inverterData.L2Power = random(0, 1000);
    inverterData.L3Power = random(0, 1000);
    inverterData.inverterPower = random(0, 1000);
    inverterData.loadPower = random(0, 1000);
    inverterData.loadToday = random(0, 1000);
    inverterData.feedInPower = random(0, 1000);
    inverterData.soc = random(0, 100);
    inverterData.yieldToday = random(0, 1000);
    inverterData.yieldTotal = random(0, 1000);
    inverterData.batteryChargedToday = random(0, 1000);
    inverterData.batteryDischargedToday = random(0, 1000);
    inverterData.gridBuyToday = random(0, 1000);
    inverterData.gridSellToday = random(0, 1000);
    return inverterData;
}


void updateDashboardUI() {
    int selfUsePercent = inverterData.loadPower > 0 ? (100 * (inverterData.loadPower + inverterData.feedInPower)) / inverterData.loadPower : 0;
    selfUsePercent = constrain(selfUsePercent, 0, 100);
    

    // lv_obj_t *line = lv_line_create(ui_LeftContainer);
    // lv_point_t p1 = {ui_gridContainer->coords.x1, ui_gridContainer->coords.y1};
    // lv_point_t p2 = {ui_inverterContainer->coords.x1, ui_inverterContainer->coords.y1};
    // const lv_point_t points[] = {p1, p2};
    // lv_line_set_points(line, points, 2);
    

    lv_label_set_text_fmt(ui_pvLabel, "%d W", inverterData.pv1Power + inverterData.pv2Power);
    lv_label_set_text_fmt(ui_pv1Label, "%d W", inverterData.pv1Power);
    lv_label_set_text_fmt(ui_pv2Label, "%d W", inverterData.pv2Power);
    lv_label_set_text_fmt(ui_inverterTemperatureLabel, "%d°C", inverterData.inverterTemperature);
    lv_label_set_text_fmt(ui_inverterPowerLabel, "%d W", inverterData.inverterPower);
    lv_label_set_text_fmt(ui_inverterPowerL1Label, "%d W", inverterData.L1Power);
    lv_label_set_text_fmt(ui_inverterPowerL2Label, "%d W", inverterData.L2Power);
    lv_label_set_text_fmt(ui_inverterPowerL3Label, "%d W", inverterData.L3Power);
    lv_label_set_text_fmt(ui_loadPowerLabel, "%d W", inverterData.loadPower);
    lv_label_set_text_fmt(ui_feedInPowerLabel, "%d W", inverterData.feedInPower);
    lv_label_set_text_fmt(ui_socLabel, "%d%%", inverterData.soc);
    lv_label_set_text_fmt(ui_batteryPowerLabel, "%d W", inverterData.batteryPower);
    lv_label_set_text_fmt(ui_batteryTemperatureLabel, "%d°C", inverterData.batteryTemperature);
    lv_label_set_text_fmt(ui_selfUsePercentLabel, "%d%%", selfUsePercent);
//   lv_label_set_text_fmt(ui_pvTodayYield, "%s kWh", String(inverterData.yieldToday,1).c_str());
//   lv_label_set_text_fmt(ui_loadTodayLabel, "%s kWh", String(inverterData.loadToday,1).c_str());
//   lv_label_set_text_fmt(ui_gridSellTodayLabel, "+ %s kWh", String(inverterData.gridSellToday,1).c_str());
//   lv_label_set_text_fmt(ui_gridBuyTodayLabel, "- %s kWh", String(inverterData.gridBuyToday,1).c_str());
//   lv_label_set_text_fmt(ui_batteryChargedTodayLabel, "+ %s kWh", String(inverterData.batteryChargedToday,1).c_str());
//   lv_label_set_text_fmt(ui_batteryDischargedTodayLabel, "- %s kWh", String(inverterData.batteryDischargedToday,1).c_str());
    if(discoveryResult.result) {
        if(inverterData.status != 0) {
            lv_label_set_text_fmt(ui_statusLabel, "Error: %d", inverterData.status);
        } else {
            lv_label_set_text(ui_statusLabel, discoveryResult.sn.c_str());
        }
    } else {
        lv_label_set_text(ui_statusLabel, "Disconnected");
    }

    
}

void timerCB(struct _lv_timer_t *timer) {
    updateDashboardUI();
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

    lv_timer_t * timer = lv_timer_create(timerCB, 100, NULL);
    
    WiFi.begin("Wifi_SXBYETVWHZ");
}

void loop()
{
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
    }
    //discoveryResult = dongleDiscovery.discoverDongle();
    //if (discoveryResult.result)
    {
        inverterData = dongleAPI.loadData("SXBYETVWHZ");
    }
    //inverterData = createRandomMockData();
    //updateDashboardUI();
    
    delay(1000);
}
