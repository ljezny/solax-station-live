#include <Arduino.h>
#include <ESP_Panel_Library.h>
#include <lvgl.h>
#include "lvgl_port_v8.h"
#include "ui/ui.h"
#include "Solax/SolaxDongleDiscovery.hpp"
#include "Solax/SolaxDongleAPI.hpp"
#include "utils/UnitFormatter.hpp"
#include "utils/ChartDataProvider.hpp"

#define WAIT 1000
SET_LOOP_TASK_STACK_SIZE(32 * 1024);

SolaxDongleAPI dongleAPI;
SolaxDongleDiscovery dongleDiscovery;

SolaxDongleInverterData_t inverterData;
SolaxDongleDiscoveryResult_t discoveryResult;
ESP_Panel *panel = new ESP_Panel();
ChartDataProvider *chartDataProvider = new ChartDataProvider();

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
    inverterData.feedInPower = random(-1000, 1000);
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

    if(inverterData.pv1Power + inverterData.pv2Power > 0) {
        lv_obj_set_style_opa(ui_pvBall, 255, 0);
    } else {
        lv_obj_set_style_opa(ui_pvBall, 0, 0);
    }

    if(inverterData.loadPower > 0) {
        lv_obj_set_style_opa(ui_toLoadBall, 255, 0);
    } else {
        lv_obj_set_style_opa(ui_toLoadBall, 0, 0);
    }
    
    if(inverterData.batteryPower > 0) {
        lv_obj_set_style_opa(ui_toBatteryBall, 255, 0);
        lv_obj_set_style_opa(ui_fromBatteryBall, 0, 0);
    } else if (inverterData.batteryPower == 0) {
        lv_obj_set_style_opa(ui_toBatteryBall, 0, 0);
        lv_obj_set_style_opa(ui_fromBatteryBall, 0, 0);
    } else {
        lv_obj_set_style_opa(ui_toBatteryBall, 0, 0);
        lv_obj_set_style_opa(ui_fromBatteryBall, 255, 0);
    }

    if(inverterData.feedInPower > 0) {
        lv_obj_set_style_opa(ui_toGridBall, 255, 0);
        lv_obj_set_style_opa(ui_fromGridBall, 0, 0);
    } else if (inverterData.feedInPower == 0) {
        lv_obj_set_style_opa(ui_toGridBall, 0, 0);
        lv_obj_set_style_opa(ui_fromGridBall, 0, 0);
    } else {
        lv_obj_set_style_opa(ui_toGridBall, 0, 0);
        lv_obj_set_style_opa(ui_fromGridBall, 255, 0);
    }
    lv_label_set_text(ui_pvLabel, format(POWER, inverterData.pv1Power + inverterData.pv2Power).formatted.c_str());
    lv_label_set_text(ui_pv1Label, format(POWER, inverterData.pv1Power, 1.0f, true).formatted.c_str());
    lv_label_set_text(ui_pv2Label, format(POWER, inverterData.pv2Power, 1.0f, true).formatted.c_str());
    lv_label_set_text_fmt(ui_inverterTemperatureLabel, "%d°C", inverterData.inverterTemperature);
    lv_label_set_text(ui_inverterPowerLabel, format(POWER, inverterData.inverterPower).formatted.c_str());
    lv_label_set_text(ui_inverterPowerL1Label, format(POWER, inverterData.L1Power).formatted.c_str());
    lv_label_set_text(ui_inverterPowerL2Label, format(POWER, inverterData.L2Power).formatted.c_str());
    lv_label_set_text(ui_inverterPowerL3Label, format(POWER, inverterData.L3Power).formatted.c_str());
    lv_label_set_text(ui_loadPowerLabel, format(POWER, inverterData.loadPower).formatted.c_str());
    lv_label_set_text(ui_feedInPowerLabel, format(POWER, inverterData.feedInPower).formatted.c_str());
    lv_label_set_text_fmt(ui_socLabel, "%d%%", inverterData.soc);
    lv_label_set_text(ui_batteryPowerLabel, format(POWER, inverterData.batteryPower).formatted.c_str());
    lv_label_set_text_fmt(ui_batteryTemperatureLabel, "%d°C", inverterData.batteryTemperature);
    lv_label_set_text_fmt(ui_selfUsePercentLabel, "%d%%", selfUsePercent);
    lv_label_set_text(ui_yieldTodayLabel, format(ENERGY, inverterData.yieldToday * 1000.0,1).formatted.c_str());
    lv_label_set_text(ui_loadTotalLabel, format(ENERGY, inverterData.loadTotal * 1000.0,1).formatted.c_str());
    lv_label_set_text(ui_gridSellTodayLabel, ("+ " + format(ENERGY, inverterData.gridSellToday * 1000.0,1).formatted).c_str());
    lv_label_set_text(ui_gridBuyTodayLabel, ("-" + format(ENERGY, inverterData.gridBuyToday * 1000.0,1).formatted).c_str());
    lv_label_set_text(ui_batteryChargedTodayLabel, ("+" + format(ENERGY, inverterData.batteryChargedToday * 1000.0,1).formatted).c_str());
    lv_label_set_text(ui_batteryDischargedTodayLabel, ("-" + format(ENERGY, inverterData.batteryDischargedToday * 1000.0,1).formatted).c_str());
    lv_label_set_text(ui_yieldTotalLabel, format(ENERGY, inverterData.yieldTotal * 1000.0,1).formatted.c_str());
    lv_label_set_text(ui_loadTotalLabel, format(ENERGY, inverterData.loadToday * 1000.0,1).formatted.c_str());
    lv_label_set_text(ui_gridSellTotalLabel, ("+ " + format(ENERGY, inverterData.gridSellTotal * 1000.0,1).formatted).c_str());
    lv_label_set_text(ui_gridBuyTotalLabel, ("- " + format(ENERGY, inverterData.gridBuyTotal * 1000.0,1).formatted).c_str());
    lv_label_set_text(ui_batteryChargedTotalLabel, ("+ " + format(ENERGY, inverterData.batteryChargedTotal * 1000.0,1).formatted).c_str());
    lv_label_set_text(ui_batteryDischargedTotalLabel, ("- " + format(ENERGY, inverterData.batteryDischargedTotal * 1000.0,1).formatted).c_str());

    while(lv_chart_get_series_next(ui_Chart1, NULL)) {
        lv_chart_remove_series(ui_Chart1, chartDataProvider->getSeries(0));
    }
    

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

bool uiInitialized = false;
bool dashboardShown = false;

void timerCB(struct _lv_timer_t *timer) {
    if(!uiInitialized) {
        uiInitialized = true;
        ui_init();
    }
    if(!dashboardShown && inverterData.status == 0) {        
        lv_disp_load_scr(ui_Dashboard);
        dashboardShown = true;
    }
    
    updateDashboardUI();

    esp_lcd_rgb_panel_restart(panel->getLcd()->getHandle());
}

void setup()
{
    Serial.begin(115200);
  
    Serial.println("Initialize panel device");
    
    panel->init();
#if LVGL_PORT_AVOID_TEAR
    // When avoid tearing function is enabled, configure the RGB bus according to the LVGL configuration
    ESP_PanelBus_RGB *rgb_bus = static_cast<ESP_PanelBus_RGB *>(panel->getLcd()->getBus());
    rgb_bus->configRgbFrameBufferNumber(LVGL_PORT_DISP_BUFFER_NUM);
    rgb_bus->configRgbBounceBufferSize(LVGL_PORT_RGB_BOUNCE_BUFFER_SIZE);
#endif
    panel->begin();

    lvgl_port_lock(-1);
    lvgl_port_init(panel->getLcd(), panel->getTouch());
    lvgl_port_unlock();
    
    lv_timer_t * timer = lv_timer_create(timerCB, 5000, NULL); //60fps
}

void loop()
{
    discoveryResult = dongleDiscovery.discoverDongle();
    if (discoveryResult.result)
    {
        inverterData = dongleAPI.loadData(discoveryResult.sn);
        if(inverterData.status == 1) {
            chartDataProvider->addSample(millis(), inverterData.pv1Power + inverterData.pv2Power, inverterData.loadPower, inverterData.soc);
        }
    }
    
    delay(WAIT);
}
