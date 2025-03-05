#include <Arduino.h>
#include <lvgl.h>

#include <SPI.h>
#include "consts.h"
#include "ui/ui.h"
#include "Inverters/DongleDiscovery.hpp"
#include "Inverters/Goodwe/GoodweDongleAPI.hpp"
#include "Inverters/Solax/SolaxDongleAPI.hpp"
#include "Inverters/SofarSolar/SofarSolarDongleAPI.hpp"
#include "Inverters/Victron/VictronDongleAPI.hpp"
#include "Shelly/Shelly.hpp"
#include "utils/UnitFormatter.hpp"
#include "utils/SolarChartDataProvider.hpp"
#include "utils/BacklightResolver.hpp"
#include <mat.h>
#include "DashboardUI.hpp"
#include "SplashUI.hpp"
#include "WiFiSetupUI.hpp"
#include "utils/SoftAP.hpp"
#include "utils/ShellyRuleResolver.hpp"

#define UI_REFRESH_INTERVAL 5000 // Define the UI refresh interval in milliseconds
#define INVERTER_DATA_REFRESH_INTERVAL 2000
#define SHELLY_REFRESH_INTERVAL 15000

#include "gfx_conf.h"
#include "Touch/Touch.hpp"
#include <mutex>
#include <Wire.h>

SemaphoreHandle_t lvgl_mutex = xSemaphoreCreateMutex();
static lv_disp_draw_buf_t draw_buf;
static lv_color_t disp_draw_buf1[screenWidth * screenHeight / 10];
static lv_color_t disp_draw_buf2[screenWidth * screenHeight / 10];
static lv_disp_drv_t disp_drv;

SET_LOOP_TASK_STACK_SIZE(18 * 1024); // use freeStack

DongleDiscovery dongleDiscovery;
ShellyAPI shellyAPI;
BacklightResolver backlightResolver;
SoftAP softAP;
Touch touch;

InverterData_t inverterData;
InverterData_t previousInverterData;
WallboxData_t wallboxData;
ShellyResult_t shellyResult;
ShellyResult_t previousShellyResult;
SolarChartDataProvider solarChartDataProvider;
ShellyRuleResolver shellyRuleResolver;

SplashUI splashUI;
WiFiSetupUI wifiSetupUI(dongleDiscovery);
DashboardUI dashboardUI;

typedef enum
{
    BOOT,
    STATE_SPLASH,
    STATE_WIFI_SETUP,
    STATE_DASHBOARD
} state_t;
state_t state;
state_t previousState;

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
    if (!touch.hasTouch())
    {
        data->state = LV_INDEV_STATE_REL;
    }
    else
    {
        data->state = LV_INDEV_STATE_PR;
        data->point.x = touch.touchX;
        data->point.y = touch.touchY;

        backlightResolver.touch();
    }
}

InverterData_t createRandomMockData()
{
    InverterData_t inverterData;
    inverterData.millis = millis();
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
    inverterData.hasBattery = true;
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

void lvglTimerTask(void *param)
{
    for (;;)
    {
        xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
        lv_timer_handler();
        xSemaphoreGive(lvgl_mutex);
        vTaskDelay(5);
    }
}

void setupWiFi()
{
    WiFi.persistent(false);
    WiFi.setSleep(false);
    softAP.start();
}

void setupLVGL()
{
#if CROW_PANEL
    pinMode(38, OUTPUT);
    digitalWrite(38, LOW);
#endif
    // Display Prepare
    tft.begin();
    tft.fillScreen(TFT_BLACK);
    tft.setRotation(2);
    delay(200);

    lv_init();
    delay(100);

    // touch setup
    touch.init();
    backlightResolver.setup();
    
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

    xTaskCreatePinnedToCore(lvglTimerTask, "lvglTimerTask", 6 * 1024, NULL, 10, NULL, 1);
}

void setup()
{
    Serial.begin(460800);
    Wire.begin(TOUCH_GT911_SDA, TOUCH_GT911_SCL);

    setupLVGL();
    setupWiFi();
}

bool discoverDonglesTask()
{
    static long lastAttempt = 0;
    bool hasDongles = false;
    bool run = false;
    if (lastAttempt == 0 || millis() - lastAttempt > 60 * 1000)
    {
        run = true;
        lastAttempt = millis();
        dongleDiscovery.discoverDongle(true);
    }
    return false;
}

InverterData_t loadInverterData(DongleDiscoveryResult_t &discoveryResult)
{
    static SolaxDongleAPI solaxDongleAPI = SolaxDongleAPI();
    static GoodweDongleAPI goodweDongleAPI = GoodweDongleAPI();
    static SofarSolarDongleAPI sofarSolarDongleAPI = SofarSolarDongleAPI();
    static VictronDongleAPI victronDongleAPI = VictronDongleAPI();

    InverterData_t d;
    switch (discoveryResult.type)
    {
    case DONGLE_TYPE_SOLAX:
        d = solaxDongleAPI.loadData(discoveryResult.sn);
        break;
    case DONGLE_TYPE_GOODWE:
        d = goodweDongleAPI.loadData(discoveryResult.sn);
        break;
    case DONGLE_TYPE_SOFAR:
        d = sofarSolarDongleAPI.loadData(discoveryResult.sn);
        break;
    case DONGLE_TYPE_VICTRON:
        d = victronDongleAPI.loadData(discoveryResult.sn);
        break;
    }

    return d;
}

bool loadInverterDataTask()
{
    static long lastAttempt = 0;
    bool run = false;
    static int failures = 0;
    if (lastAttempt == 0 || millis() - lastAttempt > INVERTER_DATA_REFRESH_INTERVAL)
    {
        log_d("Loading inverter data");
        lastAttempt = millis();
        run = true;
#if DEMO
        inverterData = createRandomMockData();
        solarChartDataProvider.addSample(millis(), inverterData.pv1Power + inverterData.pv2Power, inverterData.loadPower, inverterData.soc);
        dongleDiscovery.preferedInverterWifiDongleIndex = 0;
        return;
#endif

        if (dongleDiscovery.preferedInverterWifiDongleIndex == -1)
        {
            return run;
        }

        DongleDiscoveryResult_t &discoveryResult = dongleDiscovery.discoveries[dongleDiscovery.preferedInverterWifiDongleIndex];

        if (dongleDiscovery.connectToDongle(discoveryResult))
        {
            log_d("Dongle wifi connected.");

            InverterData_t d = loadInverterData(discoveryResult);

            if (d.status == DONGLE_STATUS_OK)
            {
                failures = 0;
                inverterData = d;
                solarChartDataProvider.addSample(millis(), inverterData.pv1Power + inverterData.pv2Power + inverterData.pv3Power + inverterData.pv4Power, inverterData.loadPower, inverterData.soc);
                shellyRuleResolver.addPowerSample(inverterData.pv1Power + inverterData.pv2Power + inverterData.pv3Power + inverterData.pv4Power, inverterData.soc, inverterData.batteryPower, inverterData.loadPower, inverterData.feedInPower);
            }
            else
            {
                failures++;
                log_d("Failed to load data from  dongle. Failures: %d", failures);
                if (failures > 60)
                {
                    failures = 0;
                    WiFi.disconnect();
                }
            }
        }
        else
        {
            inverterData.status = DONGLE_STATUS_WIFI_DISCONNECTED;
        }
    }
    return run;
}

bool pairShellyTask()
{
    static long lastAttempt = 0;
    bool run = false;
    if (lastAttempt == 0 || millis() - lastAttempt > 30000)
    {
        log_d("Pairing Shelly");
        for (int i = 0; i < DONGLE_DISCOVERY_MAX_RESULTS; i++)
        {
            if (dongleDiscovery.discoveries[i].type == DONGLE_TYPE_SHELLY)
            {
                if (dongleDiscovery.connectToDongle(dongleDiscovery.discoveries[i]))
                {
                    if (shellyAPI.setWiFiSTA(dongleDiscovery.discoveries[i].ssid, softAP.getSSID(), softAP.getPassword()))
                    {
                        dongleDiscovery.discoveries[i].type = DONGLE_TYPE_IGNORE;
                    }
                }
            }
        }

        lastAttempt = millis();
        run = true;
    }
    return run;
}

bool reloadShellyTask()
{
    if (shellyAPI.getPairedCount() < softAP.getNumberOfConnectedDevices())
    {
        log_d("Connected devices: %d, paired devices: %d", softAP.getNumberOfConnectedDevices(), shellyAPI.getPairedCount());
        shellyAPI.queryMDNS();
    }
    static long lastAttempt = 0;
    bool run = false;
    if (lastAttempt == 0 || millis() - lastAttempt > SHELLY_REFRESH_INTERVAL)
    {
        log_d("Reloading Shelly data");
        shellyResult = shellyAPI.getState();
        RequestedShellyState_t state = shellyRuleResolver.resolveShellyState();
        shellyAPI.updateState(state, 5 * 60);
        lastAttempt = millis();
        run = true;
    }
    return run;
}

void logMemory()
{
    log_d("Free heap: %d", ESP.getFreeHeap());
    log_d("Min free heap: %d", ESP.getMinFreeHeap());
    log_d("Free stack: %d", uxTaskGetStackHighWaterMark(NULL));
}

bool resetWifiTask()
{
    static long lastAttempt = 0;
    bool run = false;
    if (millis() - lastAttempt > 300000) // every 5 minutes
    {
        lastAttempt = millis();
        run = true;
        logMemory();
        if (WiFi.status() == WL_CONNECTED)
        {
            log_d("Wifi connected, skipping reset");
            return run;
        }

        if (WiFi.scanComplete() == WIFI_SCAN_RUNNING)
        {
            log_d("Wifi Scan is running, skipping reset");
            return run;
        }

        log_d("Resetting wifi");
        WiFi.mode(WIFI_OFF);
        logMemory();
        delay(5000);
        softAP.start();
        logMemory();
    }
    return run;
}

void onEntering(state_t newState)
{
    log_d("Entering state %d", newState);
    switch (newState)
    {
    case BOOT:
        break;
    case STATE_SPLASH:
        xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
        splashUI.show();
        xSemaphoreGive(lvgl_mutex);
        break;
    case STATE_WIFI_SETUP:
        xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
        wifiSetupUI.show();
        xSemaphoreGive(lvgl_mutex);
        break;
    case STATE_DASHBOARD:
        xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
        dashboardUI.show();
        if (inverterData.status == DONGLE_STATUS_OK)
        {
            dashboardUI.update(inverterData, inverterData, shellyResult, shellyResult, solarChartDataProvider, wifiSignalPercent());
            previousShellyResult = shellyResult;
            previousInverterData = inverterData;
            previousInverterData.millis = 0;
        }
        xSemaphoreGive(lvgl_mutex);
        break;
    }
    logMemory();
}

void onLeaving(state_t oldState)
{
    log_d("Leaving state %d", oldState);
    switch (oldState)
    {
    case BOOT:
        break;
    case STATE_SPLASH:
        break;
    case STATE_WIFI_SETUP:
        break;
    case STATE_DASHBOARD:
        break;
    }
    logMemory();
}

void moveToState(state_t newState)
{
    onLeaving(state);
    previousState = state;
    state = newState;
    onEntering(state);
}

void updateState()
{
    switch (state)
    {
    case BOOT:
        moveToState(STATE_SPLASH);
        break;
    case STATE_SPLASH:
        xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
        splashUI.update(softAP.getESPIdHex(), String(VERSION_NUMBER));
        xSemaphoreGive(lvgl_mutex);

        if (dongleDiscovery.preferedInverterWifiDongleIndex == -1)
        {
            xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
            splashUI.updateText("Discovering dongles...");
            xSemaphoreGive(lvgl_mutex);

            dongleDiscovery.discoverDongle(false);
            dongleDiscovery.trySelectPreferedInverterWifiDongleIndex();
        }

        if (dongleDiscovery.preferedInverterWifiDongleIndex != -1)
        {
            DongleDiscoveryResult_t &discoveryResult = dongleDiscovery.discoveries[dongleDiscovery.preferedInverterWifiDongleIndex];

            xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
            splashUI.updateText("Connecting... " + discoveryResult.ssid);
            xSemaphoreGive(lvgl_mutex);

            if (dongleDiscovery.connectToDongle(discoveryResult))
            {
                xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
                splashUI.updateText("Loading data... ");
                xSemaphoreGive(lvgl_mutex);

                inverterData = loadInverterData(dongleDiscovery.discoveries[dongleDiscovery.preferedInverterWifiDongleIndex]);
                if (inverterData.status == DONGLE_STATUS_OK)
                {
                    moveToState(STATE_DASHBOARD);
                }
                else
                {
                    xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
                    splashUI.updateText("Failed to load data :-(");
                    xSemaphoreGive(lvgl_mutex);
                    delay(2000);

                    moveToState(STATE_WIFI_SETUP);
                }
            }
            else
            {
                xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
                splashUI.updateText("Failed to connect :-(");
                xSemaphoreGive(lvgl_mutex);
                delay(2000);

                moveToState(STATE_WIFI_SETUP);
            }
        }
        else
        {
            moveToState(STATE_WIFI_SETUP);
        }
        break;
    case STATE_WIFI_SETUP:
        if (wifiSetupUI.complete)
        {
            moveToState(STATE_SPLASH);
        }
#if DEMO
        moveToState(STATE_DASHBOARD);
#endif
        break;
    case STATE_DASHBOARD:
        if ((millis() - previousInverterData.millis) > UI_REFRESH_INTERVAL)
        {
            xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
            dashboardUI.update(inverterData, previousInverterData.status == DONGLE_STATUS_OK ? previousInverterData : inverterData, shellyResult, previousShellyResult, solarChartDataProvider, wifiSignalPercent());
            xSemaphoreGive(lvgl_mutex);

            previousShellyResult = shellyResult;
            previousInverterData = inverterData;

            backlightResolver.resolve(inverterData);
        }
        //only one task per state update
        if(loadInverterDataTask())
        {
            break;
        }
        if(discoverDonglesTask()) {
            break;
        }
        if(pairShellyTask()) {
            break;
        }
        if(reloadShellyTask()){
            break;
        }
        if(resetWifiTask()){
            break;
        }

        if (dongleDiscovery.preferedInverterWifiDongleIndex == -1)
        {
            moveToState(STATE_WIFI_SETUP);
        }

        break;
    }
}

void loop()
{
    updateState();
}