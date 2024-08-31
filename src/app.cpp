#include <Arduino.h>
#include <lvgl.h>
#include <demos/lv_demos.h>
#include <examples/lv_examples.h>
#include <Wire.h>
#include <SPI.h>
#include "consts.h"
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

#include "gfx_conf.h"

static lv_disp_draw_buf_t draw_buf;
static lv_color_t disp_draw_buf1[screenWidth * screenHeight / 10];
static lv_color_t disp_draw_buf2[screenWidth * screenHeight / 10];
static lv_disp_drv_t disp_drv;

SET_LOOP_TASK_STACK_SIZE(48 * 1024);

DongleDiscovery dongleDiscovery;
ShellyAPI shellyAPI;
BacklightResolver backlightResolver;
SoftAP softAP;

InverterData_t inverterData;
InverterData_t previousInverterData;
WallboxData_t wallboxData;
DongleDiscoveryResult_t discoveryResult;
ShellyResult_t shellyResult;
ShellyResult_t previousShellyResult;
SolarChartDataProvider solarChartDataProvider;
ShellyRuleResolver shellyRuleResolver;

DashboardUI dashboardUI;

/* Display flushing */
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    tft.pushImageDMA(area->x1, area->y1, w, h, (lgfx::rgb565_t *)&color_p->full);

    lv_disp_flush_ready(disp);
}

void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data)
{
    uint16_t touchX, touchY;
    bool touched = tft.getTouch(&touchX, &touchY);
    if (!touched)
    {
        data->state = LV_INDEV_STATE_REL;
    }
    else
    {
        data->state = LV_INDEV_STATE_PR;

        /*Set the coordinates*/
        data->point.x = touchX;
        data->point.y = touchY;

        Serial.print("Data x ");
        Serial.println(touchX);

        Serial.print("Data y ");
        Serial.println(touchY);

        backlightResolver.touch();
    }
}

InverterData_t createRandomMockData()
{
    InverterData_t inverterData;
    inverterData.status = DONGLE_STATUS_OK;
    inverterData.pv1Power = random(2000, 2500);
    inverterData.pv2Power = random(3500, 4000);
    inverterData.batteryPower = random(-50, 50);
    inverterData.batteryTemperature = random(20, 26);
    inverterData.inverterTemperature = random(40, 52);
    inverterData.L1Power = random(500, 600);
    inverterData.L2Power = random(300, 400);
    inverterData.L3Power = random(1000, 1200);
    inverterData.inverterPower = inverterData.L1Power + inverterData.L2Power + inverterData.L3Power;
    inverterData.loadPower = random(800, 1200);
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

int wifiSignalPercent()
{
    if (WiFi.status() != WL_CONNECTED)
    {
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
        previousShellyResult = shellyResult;
        dashboardUI.update(inverterData, previousInverterData.status == DONGLE_STATUS_OK ? previousInverterData : inverterData, shellyResult, previousShellyResult, solarChartDataProvider, wifiSignalPercent());
        previousInverterData = inverterData;
        previousShellyResult = shellyResult;
        backlightResolver.resolve(inverterData);
    }
}

void lvglTimerTask(void *param) {
    for(;;) {
        lv_timer_handler();
        vTaskDelay(5);
    }
}


void setup()
{
    Serial.begin(115200);

    Serial.println("Initialize panel device");

    pinMode(38, OUTPUT);
    digitalWrite(38, LOW);
    pinMode(17, OUTPUT);
    digitalWrite(17, LOW);
    pinMode(18, OUTPUT);
    digitalWrite(18, LOW);
    pinMode(42, OUTPUT);
    digitalWrite(42, LOW);

    // Display Prepare
    tft.begin();
    tft.fillScreen(TFT_BLACK);
    tft.setRotation(2);
    delay(200);

    lv_init();
    delay(100);

    lv_disp_draw_buf_init(&draw_buf, disp_draw_buf1, disp_draw_buf2, screenWidth * screenHeight / 10);
    /* Initialize the display */
    lv_disp_drv_init(&disp_drv);
    /* Change the following line to your display resolution */
    disp_drv.hor_res = screenWidth;
    disp_drv.ver_res = screenHeight;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.full_refresh = 1;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    /* Initialize the (dummy) input device driver */
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    ui_init();
    lv_scr_load(ui_Splash);
    lv_label_set_text(ui_fwVersionLabel, String("v" + String(VERSION_NUMBER)).c_str());
    lv_label_set_text(ui_ESPIdLabel, softAP.getESPIdHex().c_str());

    xTaskCreatePinnedToCore(lvglTimerTask, "lvglTimerTask", 6 * 1024, NULL, 10, NULL, 0);
    lv_timer_t *timer = lv_timer_create(timerCB, dashboardUI.UI_REFRESH_PERIOD_MS, NULL);

    softAP.start();
}   

bool discoverDongles()
{
    static long lastAttempt = 0;
    bool hasDongles = false;
    for (int i = 0; i < DONGLE_DISCOVERY_MAX_RESULTS; i++)
    {
        if (dongleDiscovery.discoveries[i].type != DONGLE_TYPE_UNKNOWN)
        {
            hasDongles = true;
            break;
        }
    }
    int period = hasDongles ? 30000 : 5000;
    if (lastAttempt == 0 || millis() - lastAttempt > period)
    {
        log_d("Discovering dongles");
        if (dongleDiscovery.discoverDongle())
        {
            lastAttempt = millis();
            return true;
        }
    }
    return false;
}

void loadSolaxInverterData(DongleDiscoveryResult_t &discoveryResult)
{
    static long lastAttempt = 0;
    if (lastAttempt == 0 || (millis() - lastAttempt > 5000))
    {
        log_d("Loading Solax inverter data");
        lastAttempt = millis();
        if (dongleDiscovery.connectToDongle(discoveryResult, ""))
        {
            InverterData_t d = SolaxDongleAPI().loadData(discoveryResult.sn);

            if (d.status == DONGLE_STATUS_OK)
            {
                inverterData = d;
                solarChartDataProvider.addSample(millis(), inverterData.pv1Power + inverterData.pv2Power, inverterData.loadPower, inverterData.soc);
                shellyRuleResolver.addPowerSample(inverterData.pv1Power + inverterData.pv2Power, inverterData.soc, inverterData.batteryPower, inverterData.loadPower, inverterData.feedInPower);
            } else if(d.status == DONGLE_STATUS_UNSUPPORTED_DONGLE) {
                log_d("Unsupported dongle");
                discoveryResult.type = DONGLE_TYPE_IGNORE;                
            }
        }
        else
        {
            inverterData.status = DONGLE_STATUS_WIFI_DISCONNECTED;
        }
    }
}

// void loadSolaxWallboxData(DongleDiscoveryResult_t &discoveryResult)
// {
//     static long lastAttempt = 0;
//     int wallboxRefreshPeriod = wallboxData.status == DONGLE_STATUS_OK && wallboxData.isConnected ? 30000 : 5 * 60 * 1000;
//     if (lastAttempt == 0 || millis() - lastAttempt > wallboxRefreshPeriod)
//     {
//         log_d("Loading Solax wallbox data");
//         if (dongleDiscovery.connectToDongle(discoveryResult, ""))
//         {
//             WallboxData_t d = SolaxWallboxDongleAPI().loadData(discoveryResult.sn);
//             if (d.status == DONGLE_STATUS_OK)
//             {
//                 wallboxData = d;
//             }
//         }
//         else
//         {
//             wallboxData.status = DONGLE_STATUS_WIFI_DISCONNECTED;
//         }
//         lastAttempt = millis();
//     }
// }

void loadGoodweInverterData(DongleDiscoveryResult_t &discoveryResult)
{
    static long lastAttempt = 0;
    if (lastAttempt == 0 || millis() - lastAttempt > 1000)
    {
        log_d("Loading Goodwe inverter data");
        if (dongleDiscovery.connectToDongle(discoveryResult, "12345678") || dongleDiscovery.connectToDongle(discoveryResult, "Live" + softAP.getPassword()))
        {
            log_d("GoodWe wifi connected.");

            InverterData_t d = GoodweDongleAPI().loadData(discoveryResult.sn);

            if (d.status == DONGLE_STATUS_OK)
            {
                inverterData = d;
                solarChartDataProvider.addSample(millis(), inverterData.pv1Power + inverterData.pv2Power, inverterData.loadPower, inverterData.soc);
                shellyRuleResolver.addPowerSample(inverterData.pv1Power + inverterData.pv2Power, inverterData.soc, inverterData.batteryPower, inverterData.loadPower, inverterData.feedInPower);
            }
        }
        else
        {
            inverterData.status = DONGLE_STATUS_WIFI_DISCONNECTED;
        }
        lastAttempt = millis();
    }
}

void pairShelly(DongleDiscoveryResult_t &discoveryResult)
{
    static long lastAttempt = 0;
    if (lastAttempt == 0 || millis() - lastAttempt > 30000)
    {
        log_d("Pairing Shelly");
        if (dongleDiscovery.connectToDongle(discoveryResult, ""))
        {
            if (shellyAPI.setWiFiSTA(discoveryResult.ssid, softAP.getSSID(), softAP.getPassword()))
            {
                discoveryResult.type = DONGLE_TYPE_UNKNOWN;
                discoveryResult.sn = "";
                discoveryResult.ssid = "";
            }
        }
        lastAttempt = millis();
    }
}

void reloadShelly()
{
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

void processDongles()
{
    for (int i = 0; i < DONGLE_DISCOVERY_MAX_RESULTS; i++)
    {
        if (dongleDiscovery.discoveries[i].type != DONGLE_TYPE_UNKNOWN)
        {
            switch (dongleDiscovery.discoveries[i].type)
            {
            case DONGLE_TYPE_SOLAX:
                loadSolaxInverterData(dongleDiscovery.discoveries[i]);
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
}
