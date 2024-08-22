#include <Arduino.h>
#include <ESP_Panel_Library.h>
#include <lvgl.h>
#include "consts.h"
#include "lvgl_port_v8.h"
#include "ui/ui.h"
#include "Inverters/DongleDiscovery.hpp"
#include "Inverters/Goodwe/GoodweDongleAPI.hpp"
#include "Inverters/Solax/SolaxDongleAPI.hpp"
#include "Shelly/Shelly.hpp"
#include "utils/UnitFormatter.hpp"
#include "utils/SolarChartDataProvider.hpp"
#include "utils/BacklightResolver.hpp"
#include <mat.h>
#include "DashboardUI.hpp"
#include "utils/SoftAP.hpp"
#include "utils/ShellyRuleResolver.hpp"

SET_LOOP_TASK_STACK_SIZE(48 * 1024);


SolaxDongleAPI dongleAPI;
DongleDiscovery dongleDiscovery;
ShellyAPI shellyAPI;
BacklightResolver backlightResolver;
SoftAP softAP;

InverterData_t inverterData;
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
    inverterData.pv1Power = random(0, 5000);
    inverterData.pv2Power = random(0, 5000);
    inverterData.batteryPower = random(-3000, 3000);
    inverterData.batteryTemperature = random(0, 50);
    inverterData.inverterTemperature = random(0, 50);
    inverterData.L1Power = random(0, 3000);
    inverterData.L2Power = random(0, 4000);
    inverterData.L3Power = random(0, 3000);
    inverterData.inverterPower = random(0, 1000);
    inverterData.loadPower = random(0, 1000);
    inverterData.loadToday = random(0, 1000);
    inverterData.feedInPower = random(-1000, 1000);
    inverterData.soc = random(0, 100);
    inverterData.pvToday = random(0, 1000);
    inverterData.pvTotal = random(0, 1000);
    inverterData.batteryChargedToday = random(0, 1000);
    inverterData.batteryDischargedToday = random(0, 1000);
    inverterData.gridBuyToday = random(0, 1000);
    inverterData.gridSellToday = random(0, 1000);
    return inverterData;
}

void runReloadDataTask(void *pvParameters)
{
    for (;;)
    {
        int start = millis();
#if DEMO
        inverterData = createRandomMockData();
#else
        log_d("Reloading data");
        if (discoveryResult.result)
        {
            //inverterData.status = DONGLE_STATUS_WIFI_DISCONNECTED;
            int MAX_RETRIES = 5;
            for(int i = 0; i < MAX_RETRIES; i++) {
                InverterData_t d = dongleAPI.loadData(discoveryResult.sn);
                if(d.status == DONGLE_STATUS_OK) {
                    inverterData = d;
                    break;
                }
                delay(100);
            }
            if (inverterData.status == DONGLE_STATUS_OK)
            {
                solarChartDataProvider.addSample(millis(), inverterData.pv1Power + inverterData.pv2Power, inverterData.loadPower, inverterData.soc);
                shellyRuleResolver.addPowerSample(inverterData.pv1Power + inverterData.pv2Power, inverterData.soc, inverterData.batteryPower, inverterData.loadPower, inverterData.feedInPower);
            }
        }
#endif
        vTaskDelay(constrain((3000 - (millis() - start)), 0, 3000) / portTICK_PERIOD_MS);
    }
}

void runShellyReloadTask(void *pvParameters)
{
    long lastActivateShellyAttempt = 0;
    for (;;)
    {
        log_d("Reloading Shelly data");
        shellyResult = shellyAPI.getState();
        if (lastActivateShellyAttempt == 0 || millis() - lastActivateShellyAttempt > 30000)
        {
            RequestedShellyState_t state = shellyRuleResolver.resolveShellyState();
            shellyAPI.updateState(state, 5 * 60);
            lastActivateShellyAttempt = millis();
        }
        vTaskDelay(4000 / portTICK_PERIOD_MS);
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
        dashboardUI.update(inverterData, shellyResult, solarChartDataProvider);
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
    //lv_label_set_text(ui_fwVersionLabel, String("v" + String(VERSION_NUMBER)).c_str());
    //lv_label_set_text(ui_ESPIDLabel, softAP.getESPIdHex().c_str());

    lv_timer_t *timer = lv_timer_create(timerCB, 3000, NULL);
    lv_log_register_print_cb([](const char *txt)
                             { log_i("%s\n", txt); });

    xTaskCreate(runReloadDataTask, "ReloadDataTask", 32 * 1024, NULL, 1, NULL);
    xTaskCreate(runShellyReloadTask, "ShellyReloadTask", 32 * 1024, NULL, 1, NULL);

    panel->getTouch()->attachInterruptCallback(onTouchInterruptCallback, NULL);
}

void discoverDongle()
{
    static long lastAttempt = 0;

    if (lastAttempt == 0 || millis() - lastAttempt > 20000)
    {
        discoveryResult = dongleDiscovery.discoverDongle();
        lastAttempt = millis();
    } 
}

void checkNewShellyPairings()
{
    static long lastAttempt = 0;
    if (lastAttempt == 0 || millis() - lastAttempt > 30000)
    {
        log_d("Checking for new Shelly pairings");
        String shellyAPSSID = shellyAPI.findShellyAP();
        if (shellyAPSSID.length() > 0)
        {
            shellyAPI.pairShelly(shellyAPSSID, softAP.getSSID(), softAP.getPassword()); 
        }
        lastAttempt = millis();
    }
}

void discoverShellyPairings()
{
    static long lastAttempt = 0;
    if (lastAttempt == 0 || millis() - lastAttempt > 3000)
    {
        log_d("Discovering Shelly on local network");
        shellyAPI.discoverParings();
        lastAttempt = millis();
    }
}

void checkSoftAP()
{
    static long lastAttempt = 0;
    if (lastAttempt == 0 || millis() - lastAttempt > 10000)
    {
        softAP.ensureRunning();
        lastAttempt = millis();
    }
}

void loop()
{
    discoverDongle();
    checkNewShellyPairings();
    discoverShellyPairings();
    checkSoftAP();
}
