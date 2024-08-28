#include <Arduino.h>
#include <ESP_Panel_Library.h>
#include <lvgl.h>
#include "consts.h"
#include "lvgl_port_v8.h"
#include "ui/ui.h"
#include "Inverters/DongleDiscovery.hpp"
#include "Inverters/Goodwe/GoodweDongleAPI.hpp"
#include "Inverters/Solax/SolaxDongleAPI.hpp"
#include "Inverters/Solax/SolaxWallboxDongleAPI.hpp"
#include "Shelly/Shelly.hpp"
#include "utils/UnitFormatter.hpp"
#include "utils/SolarChartDataProvider.hpp"
#include "utils/BacklightResolver.hpp"
#include <mat.h>
#include "DashboardUI.hpp"
#include "utils/SoftAP.hpp"
#include "utils/ShellyRuleResolver.hpp"

SET_LOOP_TASK_STACK_SIZE(48 * 1024);

DongleDiscovery dongleDiscovery;
ShellyAPI shellyAPI;
BacklightResolver backlightResolver;
SoftAP softAP;

InverterData_t inverterData;
WallboxData_t wallboxData;
DongleDiscoveryResult_t discoveryResult;
ShellyResult_t shellyResult;
SolarChartDataProvider solarChartDataProvider;
ShellyRuleResolver shellyRuleResolver;

ESP_Panel *panel = new ESP_Panel();
DashboardUI dashboardUI;


IRAM_ATTR bool onTouchInterruptCallback(void *user_data)
{
    backlightResolver.touch();

    return false;
}

InverterData_t createRandomMockData()
{
    InverterData_t inverterData;
    inverterData.status = DONGLE_STATUS_OK;
    inverterData.pv1Power = random(500, 5000);
    inverterData.pv2Power = random(500, 4000);
    inverterData.batteryPower = random(-300, 1000);
    inverterData.batteryTemperature = random(20, 26);
    inverterData.inverterTemperature = random(40, 52);
    inverterData.L1Power = random(0, 3000);
    inverterData.L2Power = random(0, 4000);
    inverterData.L3Power = random(0, 3000);
    inverterData.inverterPower = inverterData.L1Power + inverterData.L2Power + inverterData.L3Power;
    inverterData.loadPower = random(200, 1200);
    inverterData.loadToday = random(20, 60);
    inverterData.feedInPower = random(-1000, 1000);
    inverterData.soc = random(80, 85);
    inverterData.pvToday = random(30, 50);
    inverterData.pvTotal = random(1500, 2000);
    inverterData.batteryChargedToday = random(20, 25);
    inverterData.batteryDischargedToday = random(5, 15);
    inverterData.gridBuyToday = random(5, 16);
    inverterData.gridSellToday = random(6, 23);
    inverterData.sn = "1234567890";
    return inverterData;
}

int wifiSignalPercent() {
    if(WiFi.status() != WL_CONNECTED) {
        return 0;
    }

    int rssi = WiFi.RSSI();
    if (rssi <= -100)
    {
        return 0;
    }
    else if (rssi >= -50)
    {
        return 100;
    }
    else
    {
        return 2 * (rssi + 100);
    }
}

bool dashboardShown = false;

void timerCB(struct _lv_timer_t *timer)
{
    if (!dashboardShown && inverterData.status == DONGLE_STATUS_OK)
    {
        dashboardUI.show();
        dashboardShown = true;
    }

    if (dashboardShown)
    {
        dashboardUI.update(inverterData, shellyResult, solarChartDataProvider, wifiSignalPercent());
        backlightResolver.resolve(inverterData);
    }
    
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

    backlightResolver.setup(panel->getBacklight());

    lvgl_port_init(panel->getLcd(), panel->getTouch());
    ui_init();
    lv_scr_load_anim(ui_Splash, LV_SCR_LOAD_ANIM_FADE_IN, 500, 0, true);
    lv_label_set_text(ui_fwVersionLabel, String("v" + String(VERSION_NUMBER)).c_str());
    lv_label_set_text(ui_ESPIdLabel, softAP.getESPIdHex().c_str());

    lv_timer_t *timer = lv_timer_create(timerCB, 3000, NULL);
    //lv_log_register_print_cb([](const char *txt)
      //                       { log_i("%s\n", txt); });

    panel->getTouch()->attachInterruptCallback(onTouchInterruptCallback, NULL);

    softAP.start();
}

bool discoverDongles() {
    static long lastAttempt = 0;    
    bool hasDongles = false;
    for(int i = 0; i < DONGLE_DISCOVERY_MAX_RESULTS; i++) {
        if(dongleDiscovery.discoveries[i].type != DONGLE_TYPE_UNKNOWN) {
            hasDongles = true;
            break;
        }
    }
    int period = hasDongles? 30000 : 5000;
    if (lastAttempt == 0 || millis() - lastAttempt > period)
    {
        log_d("Discovering dongles");
        if(dongleDiscovery.discoverDongle()) {
            lastAttempt = millis();
            return true;
        }
    }
    return false;
}

void loadSolaxInverterData(DongleDiscoveryResult_t &discoveryResult) {
    static long lastAttempt = 0;
    if (lastAttempt == 0 || millis() - lastAttempt > 2000)
    {
        log_d("Loading Solax inverter data");
        if(dongleDiscovery.connectToDongle(discoveryResult, "")) {
            InverterData_t d = SolaxDongleAPI().loadData(discoveryResult.sn);
            if (d.status == DONGLE_STATUS_OK)
            {
                inverterData = d;
                solarChartDataProvider.addSample(millis(), inverterData.pv1Power + inverterData.pv2Power, inverterData.loadPower, inverterData.soc);
                shellyRuleResolver.addPowerSample(inverterData.pv1Power + inverterData.pv2Power, inverterData.soc, inverterData.batteryPower, inverterData.loadPower, inverterData.feedInPower);
            }
        }
        lastAttempt = millis();
    }
} 

void loadSolaxWallboxData(DongleDiscoveryResult_t &discoveryResult) {
    static long lastAttempt = 0;
    int wallboxRefreshPeriod = wallboxData.status == DONGLE_STATUS_OK && wallboxData.isConnected? 30000 : 5 * 60 * 1000;
    if (lastAttempt == 0 || millis() - lastAttempt > wallboxRefreshPeriod)
    {
        log_d("Loading Solax wallbox data");
        if(dongleDiscovery.connectToDongle(discoveryResult, "")) {
            WallboxData_t d = SolaxWallboxDongleAPI().loadData(discoveryResult.sn);
            if (d.status == DONGLE_STATUS_OK)
            {
                wallboxData = d;
            }
        }
        lastAttempt = millis();
    }
}

void loadGoodweInverterData(DongleDiscoveryResult_t &discoveryResult) {
    static long lastAttempt = 0;
    if (lastAttempt == 0 || millis() - lastAttempt > 2000)
    {
        log_d("Loading Goodwe inverter data");
        if(dongleDiscovery.connectToDongle(discoveryResult, "12345678")) {
            log_d("GoodWe wifi connected.");
            InverterData_t d = GoodweDongleAPI().loadData(discoveryResult.sn);
            if (d.status == DONGLE_STATUS_OK)
            {
                inverterData = d;
                solarChartDataProvider.addSample(millis(), inverterData.pv1Power + inverterData.pv2Power, inverterData.loadPower, inverterData.soc);
                shellyRuleResolver.addPowerSample(inverterData.pv1Power + inverterData.pv2Power, inverterData.soc, inverterData.batteryPower, inverterData.loadPower, inverterData.feedInPower);
            }
        }
        lastAttempt = millis();
    }
}

void pairShelly(DongleDiscoveryResult_t &discoveryResult) {
    static long lastAttempt = 0;
    if (lastAttempt == 0 || millis() - lastAttempt > 30000)
    {
        log_d("Pairing Shelly");
        if(dongleDiscovery.connectToDongle(discoveryResult, "")) {
            if(shellyAPI.setWiFiSTA(discoveryResult.ssid, softAP.getSSID(), softAP.getPassword())) {
                discoveryResult.type = DONGLE_TYPE_UNKNOWN;
                discoveryResult.sn = "";
                discoveryResult.ssid = "";
            }
        }
        lastAttempt = millis();
    }
}

void reloadShelly() {
    static long lastAttempt = 0;
    if (lastAttempt == 0 || millis() - lastAttempt > 5000)
    {
        log_d("Reloading Shelly data");
        shellyAPI.queryMDNS();
        shellyResult = shellyAPI.getState();        
        RequestedShellyState_t state = shellyRuleResolver.resolveShellyState();
        shellyAPI.updateState(state, 5 * 60);
        lastAttempt = millis();
    }
}

void processDongles() {
    for(int i = 0; i < DONGLE_DISCOVERY_MAX_RESULTS; i++) {
        if(dongleDiscovery.discoveries[i].type != DONGLE_TYPE_UNKNOWN) {
            switch (dongleDiscovery.discoveries[i].type)
            {
            case DONGLE_TYPE_SOLAX_INVERTER:
                loadSolaxInverterData(dongleDiscovery.discoveries[i]);
                break;
            case DONGLE_TYPE_SOLAX_WALLBOX:
                loadSolaxWallboxData(dongleDiscovery.discoveries[i]);
                break;
            case DONGLE_TYPE_GOODWE:
                loadGoodweInverterData(dongleDiscovery.discoveries[i]);
                break;
            case DONGLE_TYPE_SHELLY:
                pairShelly(dongleDiscovery.discoveries[i]);
                break;
            default:
                break;
            }
        }
    }

#if DEMO
    inverterData = createRandomMockData();
    solarChartDataProvider.addSample(millis(), inverterData.pv1Power + inverterData.pv2Power, inverterData.loadPower, inverterData.soc);
#endif
}

void loop()
{
    discoverDongles();
    processDongles();
   
    reloadShelly();

    delay(100);
}
