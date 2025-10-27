#include <Arduino.h>
#include <lvgl.h>

#include <SPI.h>
#include "consts.h"
#include "ui/ui.h"
#include "Inverters/WiFiDiscovery.hpp"
#include "Inverters/Goodwe/GoodweDongleAPI.hpp"
#include "Inverters/Solax/SolaxModbusDongleAPI.hpp"
#include "Inverters/SofarSolar/SofarSolarDongleAPI.hpp"
#include "Inverters/Deye/DeyeDongleAPI.hpp"
#include "Inverters/Victron/VictronDongleAPI.hpp"
#include "Wallbox/EcoVolterProV2.hpp"
#include "Wallbox/SolaxWallboxLocalAPI.hpp"
#include "Shelly/Shelly.hpp"
#include "utils/UnitFormatter.hpp"
#include "utils/SolarChartDataProvider.hpp"
#include "utils/BacklightResolver.hpp"
#include "DashboardUI.hpp"
#include "SplashUI.hpp"
#include "WiFiSetupUI.hpp"
#include "utils/SoftAP.hpp"
#include "utils/SmartControlRuleResolver.hpp"
#include "utils/MedianPowerSampler.hpp"
#include "Spot/ElectricityPriceLoader.hpp"

#define UI_REFRESH_INTERVAL 5000            // Define the UI refresh interval in milliseconds
#define INVERTER_DATA_REFRESH_INTERVAL 5000 // Seems that 3s is problematic for some dongles (GoodWe), so we use 5s
#define SHELLY_REFRESH_INTERVAL 3000
#define ELECTRICITY_PRICE_REFRESH_INTERVAL 15 * 60 * 1000 // 15 minutes

#define WALLBOX_DISCOVERY_REFRESH_INTERVAL 30000
#define WALLBOX_STATUS_REFRESH_INTERVAL 5000

#include "gfx_conf.h"
#include "Touch/Touch.hpp"
#include <mutex>

SemaphoreHandle_t lvgl_mutex = xSemaphoreCreateMutex();
static lv_disp_draw_buf_t draw_buf;
static lv_color_t disp_draw_buf1[screenWidth * screenHeight / 10];
static lv_color_t disp_draw_buf2[screenWidth * screenHeight / 10];
static lv_disp_drv_t disp_drv;

SET_LOOP_TASK_STACK_SIZE(8 * 1024); // use freeStack

WiFiDiscovery dongleDiscovery;
ShellyAPI shellyAPI;
EcoVolterProAPIV2 ecoVolterAPI;
SolaxWallboxLocalAPI solaxWallboxAPI;
BacklightResolver backlightResolver;
SoftAP softAP;
Touch touch;

InverterData_t inverterData;
InverterData_t previousInverterData;
WallboxResult_t wallboxData;
WallboxResult_t previousWallboxData;
ShellyResult_t shellyResult;
ShellyResult_t previousShellyResult;
ElectricityPriceResult_t electricityPriceResult;
SolarChartDataProvider solarChartDataProvider;
MedianPowerSampler shellyMedianPowerSampler;
MedianPowerSampler wallboxMedianPowerSampler;
MedianPowerSampler uiMedianPowerSampler;
SmartControlRuleResolver shellyRuleResolver(shellyMedianPowerSampler);
SmartControlRuleResolver wallboxRuleResolver(wallboxMedianPowerSampler);

SplashUI *splashUI = NULL;
WiFiSetupUI *wifiSetupUI = NULL;
DashboardUI *dashboardUI = NULL;

WiFiDiscoveryResult_t wifiDiscoveryResult;

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
    inverterData.dongleFWVersion = "3.005.01";
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
        vTaskDelay(10);
    }
}

bool showSettings = false;
void onSettingsShow(lv_event_t *e)
{
    showSettings = true;
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

    backlightResolver.setup();

    // Display Prepare
    tft.begin();
    tft.fillScreen(TFT_BLACK);
    tft.setRotation(2);
    delay(200);

    lv_init();
    delay(100);

    // touch setup
    touch.init();

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
    splashUI = new SplashUI();
    dashboardUI = new DashboardUI(onSettingsShow);
    wifiSetupUI = new WiFiSetupUI(dongleDiscovery);
    xTaskCreate(lvglTimerTask, "lvglTimerTask", 12 * 1024, NULL, 10, NULL);
}

void setup()
{
    Serial.begin(115200);

    setupLVGL();
    setupWiFi();

    esp_log_level_set("wifi", ESP_LOG_VERBOSE);
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
        dongleDiscovery.scanWiFi(true);
    }
    return run;
}

InverterData_t loadInverterData(WiFiDiscoveryResult_t &discoveryResult)
{
    static SolaxModbusDongleAPI solaxModbusDongleAPI = SolaxModbusDongleAPI();
    static GoodweDongleAPI goodweDongleAPI = GoodweDongleAPI();
    static SofarSolarDongleAPI sofarSolarDongleAPI = SofarSolarDongleAPI();
    static DeyeDongleAPI deyeDongleAPI = DeyeDongleAPI();
    static VictronDongleAPI victronDongleAPI = VictronDongleAPI();
    long millisBefore = millis();
    InverterData_t d;
    switch (discoveryResult.type)
    {
    case CONNECTION_TYPE_SOLAX:
        d = solaxModbusDongleAPI.loadData(discoveryResult.inverterIP);
        break;
    case CONNECTION_TYPE_GOODWE:
        d = goodweDongleAPI.loadData(discoveryResult.inverterIP);
        break;
    case CONNECTION_TYPE_SOFAR:
        d = sofarSolarDongleAPI.loadData(discoveryResult.inverterIP, discoveryResult.sn);
        break;
    case CONNECTION_TYPE_DEYE:
        d = deyeDongleAPI.loadData(discoveryResult.inverterIP, discoveryResult.sn);
        break;
    case CONNECTION_TYPE_VICTRON:
        d = victronDongleAPI.loadData(discoveryResult.inverterIP);
        break;
    default:
        d.status = DONGLE_STATUS_UNSUPPORTED_DONGLE;
        break;
    }
    log_d("Inverter data loaded in %d ms", millis() - millisBefore);
    return d;
}

bool loadInverterDataTask()
{
    static long lastAttempt = 0;
    bool run = false;
    static int failures = 0;
    static int incrementalDelayTimeOnError = 0;
    if (lastAttempt == 0 || (millis() - lastAttempt) > (INVERTER_DATA_REFRESH_INTERVAL + incrementalDelayTimeOnError))
    {
        log_d("Loading inverter data");
        lastAttempt = millis();
        run = true;
#if DEMO
        inverterData = createRandomMockData();
        solarChartDataProvider.addSample(millis(), inverterData.pv1Power + inverterData.pv2Power, inverterData.loadPower, inverterData.soc);
        // dongleDiscovery.preferedInverterWifiDongleIndex = 0;
        return run;
#endif

        if (dongleDiscovery.connectToDongle(wifiDiscoveryResult))
        {
            log_d("Dongle wifi connected.");

            InverterData_t d = loadInverterData(wifiDiscoveryResult);

            if (d.status == DONGLE_STATUS_OK)
            {
                incrementalDelayTimeOnError = 0; // reset additional delay if connection was successful
                failures = 0;
                inverterData = d;
                solarChartDataProvider.addSample(millis(), inverterData.pv1Power + inverterData.pv2Power + inverterData.pv3Power + inverterData.pv4Power, inverterData.loadPower, inverterData.soc);
                shellyMedianPowerSampler.addPowerSample(inverterData.pv1Power + inverterData.pv2Power + inverterData.pv3Power + inverterData.pv4Power, inverterData.soc, inverterData.batteryPower, inverterData.loadPower, inverterData.feedInPower);
                uiMedianPowerSampler.addPowerSample(inverterData.pv1Power + inverterData.pv2Power + inverterData.pv3Power + inverterData.pv4Power, inverterData.soc, inverterData.batteryPower, inverterData.loadPower, inverterData.feedInPower);
                wallboxMedianPowerSampler.addPowerSample(inverterData.pv1Power + inverterData.pv2Power + inverterData.pv3Power + inverterData.pv4Power, inverterData.soc, inverterData.batteryPower, inverterData.loadPower, inverterData.feedInPower);
                dongleDiscovery.storeLastConnectedSSID(wifiDiscoveryResult.ssid);
            }
            else
            {
                failures++;
                incrementalDelayTimeOnError += 2000; // increase delay if connection failed
                log_d("Failed to load data from  dongle. Failures: %d", failures);
                if (failures > 10)
                {
                    failures = 0;
                    dongleDiscovery.disconnect();
                }
            }
        }
        else
        {
            incrementalDelayTimeOnError += 5000; // increase delay if connection failed
            dongleDiscovery.disconnect();
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
            if (dongleDiscovery.discoveries[i].ssid.isEmpty())
            {
                continue;
            }
            log_d("Checking SSID: %s", dongleDiscovery.discoveries[i].ssid.c_str());
            if (shellyAPI.isShellySSID(dongleDiscovery.discoveries[i].ssid))
            {
                if (dongleDiscovery.connectToDongle(dongleDiscovery.discoveries[i]))
                {
                    if (shellyAPI.setWiFiSTA(dongleDiscovery.discoveries[i].ssid, softAP.getSSID(), softAP.getPassword()))
                    {
                        dongleDiscovery.discoveries[i].ssid = ""; // clear SSID to avoid reconnecting
                    }
                    WiFi.disconnect();
                }
            }
        }
        if (shellyAPI.getPairedCount() < softAP.getNumberOfConnectedDevices())
        {
            log_d("Connected devices: %d, paired devices: %d", softAP.getNumberOfConnectedDevices(), shellyAPI.getPairedCount());
            shellyAPI.queryMDNS(WiFi.softAPIP(), WiFi.softAPSubnetMask());
        }
        lastAttempt = millis();
        run = true;
    }
    return run;
}

bool reloadShellyTask()
{
    static long lastAttempt = 0;
    bool run = false;
    if (lastAttempt == 0 || millis() - lastAttempt > SHELLY_REFRESH_INTERVAL)
    {
        log_d("Reloading Shelly data");
        shellyResult = shellyAPI.getState();
        RequestedSmartControlState_t state = shellyRuleResolver.resolveSmartControlState(1500, 100, 500, 100);
        if (state != SMART_CONTROL_UNKNOWN)
        {
            delay(1000);
            shellyAPI.updateState(state, 5 * 60);

            // state should change
            if ((shellyResult.activeCount == 0 && state > SMART_CONTROL_FULL_OFF) || (shellyResult.activeCount > 0 && state < SMART_CONTROL_KEEP_CURRENT_STATE))
            {
                shellyResult = shellyAPI.getState(); // reload state after update
            }
        }

        lastAttempt = millis();
        run = true;
    }
    return run;
}

bool loadElectricityPriceTask()
{
    static long lastAttempt = 0;
    bool run = false;
    if (lastAttempt == 0 || millis() - lastAttempt > ELECTRICITY_PRICE_REFRESH_INTERVAL)
    {
        log_d("Loading electricity price data");

        electricityPriceResult = ElectricityPriceLoader().getElectricityPrice(OTE_CZ, false);
        
        lastAttempt = millis();
        run = true;
    }
    return run;
}

bool loadEcoVolterTask()
{
    static long lastAttempt = 0;
    bool run = false;
    static int failureCounter = 0;
    int period = ecoVolterAPI.isDiscovered() ? WALLBOX_STATUS_REFRESH_INTERVAL : WALLBOX_DISCOVERY_REFRESH_INTERVAL;
    if (lastAttempt == 0 || millis() - lastAttempt > period)
    {
        if (!ecoVolterAPI.isDiscovered())
        {
            ecoVolterAPI.queryMDNS();
        }
        if (ecoVolterAPI.isDiscovered())
        {
            log_d("Loading EcoVolter data");
            wallboxData = ecoVolterAPI.getData();
            if (wallboxData.updated == 0)
            {
                failureCounter++;
                if (failureCounter >= 3)
                {
                    failureCounter = 0;
                    ecoVolterAPI.resetDiscovery(); // reset discovery if we cannot load data
                }
            }
            else
            {
                failureCounter = 0;
            }
        }
        lastAttempt = millis();
        run = true;
    }
    return run;
}

void resolveEcoVolterSmartCharge()
{
    log_d("Resolving EcoVolter Smart Charge");
    bool smartEnabled = false;
    xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
    smartEnabled = dashboardUI->isWallboxSmartChecked();
    xSemaphoreGive(lvgl_mutex);

    if (ecoVolterAPI.isDiscovered() && wallboxData.type == WALLBOX_TYPE_ECOVOLTER_PRO_V2 && wallboxData.evConnected)
    {
        if (smartEnabled)
        {
            RequestedSmartControlState_t state = wallboxRuleResolver.resolveSmartControlState(wallboxData.phases * 230 * 6, wallboxData.phases * 230 * 1, wallboxData.phases * 230 * 6, wallboxData.phases * 230 * 1);
            if (state != SMART_CONTROL_UNKNOWN)
            {
                switch (state)
                {
                case SMART_CONTROL_FULL_ON:
                    log_d("Setting EcoVolter to FULL ON");
                    if (wallboxData.chargingCurrent == 0)
                    {
                        ecoVolterAPI.setTargetCurrent(6); // start at six
                    }
                    else
                    {
                        ecoVolterAPI.setTargetCurrent(max(6, (wallboxData.targetChargingCurrent + 1))); // increase when requested
                    }
                    break;
                case SMART_CONTROL_PARTIAL_ON:
                    log_d("Setting EcoVolter to PARTIAL ON");
                    if (wallboxData.chargingCurrent > 0)
                    {
                        ecoVolterAPI.setTargetCurrent(max(6, (wallboxData.targetChargingCurrent + 1)));
                    }
                    else
                    {
                        log_d("EcoVolter not charging, cannot set to PARTIAL ON");
                    }
                    break;
                case SMART_CONTROL_KEEP_CURRENT_STATE:
                    log_d("Keeping EcoVolter current state");
                    // Do nothing, keep current state
                    break;
                case SMART_CONTROL_PARTIAL_OFF:
                    log_d("Setting EcoVolter to PARTIAL OFF");
                    if (wallboxData.chargingCurrent > 0)
                    {
                        ecoVolterAPI.setTargetCurrent(max(6, (wallboxData.targetChargingCurrent - 1)));
                    }
                    break;
                case SMART_CONTROL_FULL_OFF:
                    log_d("Setting EcoVolter to FULL OFF");
                    ecoVolterAPI.setTargetCurrent(0);
                    break;
                default:
                    break;
                }
            }
        }
        else
        {
            log_d("Smart control disabled in UI");
        }
    }
    else
    {
        log_d("EcoVolter Pro not discovered, cannot resolve smart charge");
    }
}

bool loadSolaxWallboxTask()
{
    static long lastAttempt = 0;
    bool run = false;
    int period = solaxWallboxAPI.isDiscovered() ? WALLBOX_STATUS_REFRESH_INTERVAL : WALLBOX_DISCOVERY_REFRESH_INTERVAL;
    if (lastAttempt == 0 || millis() - lastAttempt > period)
    {
        if (!solaxWallboxAPI.isDiscovered())
        {
            solaxWallboxAPI.discoverWallbox();
        }
        if (solaxWallboxAPI.isDiscovered())
        {
            log_d("Loading Solax Wallbox data");
            wallboxData = solaxWallboxAPI.getStatus();
        }
        lastAttempt = millis();
        run = true;
    }
    return run;
}

void resolveSolaxSmartCharge()
{
    log_d("Resolving Solax Smart Charge");
    bool smartEnabled = false;
    xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
    smartEnabled = dashboardUI->isWallboxSmartChecked();
    xSemaphoreGive(lvgl_mutex);

    if (solaxWallboxAPI.isDiscovered() && wallboxData.type == WALLBOX_TYPE_SOLAX && wallboxData.evConnected)
    {
        if (smartEnabled)
        {
            RequestedSmartControlState_t state = wallboxRuleResolver.resolveSmartControlState(wallboxData.phases * 230 * 6, wallboxData.phases * 230 * 1, wallboxData.phases * 230 * 6, wallboxData.phases * 230 * 1);
            if (state != SMART_CONTROL_UNKNOWN)
            {
                switch (state)
                {
                case SMART_CONTROL_FULL_ON:
                    log_d("Setting Solax Wallbox to FULL ON");
                    if (wallboxData.chargingCurrent == 0)
                    {
                        solaxWallboxAPI.setMaxCurrent(6); // start at six
                        solaxWallboxAPI.setCharging(true);
                    }
                    else
                    {
                        solaxWallboxAPI.setMaxCurrent(max(6, (wallboxData.targetChargingCurrent + 1))); // increase when requested
                    }
                    break;
                case SMART_CONTROL_PARTIAL_ON:
                    log_d("Setting Solax Wallbox to PARTIAL ON");
                    if (wallboxData.chargingCurrent > 0)
                    {
                        solaxWallboxAPI.setMaxCurrent(max(6, (wallboxData.targetChargingCurrent + 1)));
                    }
                    else
                    {
                        log_d("Solax Wallbox not charging, cannot set to PARTIAL ON");
                    }
                    break;
                case SMART_CONTROL_KEEP_CURRENT_STATE:
                    log_d("Keeping Solax Wallbox current state");
                    // Do nothing, keep current state
                    break;
                case SMART_CONTROL_PARTIAL_OFF:
                    log_d("Setting Solax Wallbox to PARTIAL OFF");
                    if (wallboxData.chargingCurrent > 0)
                    {
                        solaxWallboxAPI.setMaxCurrent(max(6, (wallboxData.targetChargingCurrent - 1)));
                    }
                    break;
                case SMART_CONTROL_FULL_OFF:
                    log_d("Setting Solax Wallbox to FULL OFF");
                    solaxWallboxAPI.setCharging(false);
                    solaxWallboxAPI.setMaxCurrent(16); // reset to max for next possible manual charge
                    break;
                default:
                    break;
                }
            }
        }
        else
        {
            log_d("Smart control disabled in UI");
        }
    }
    else
    {
        log_d("EcoVolter Pro not discovered, cannot resolve smart charge");
    }
}

void syncTime()
{
    // use ntp arduino
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 5000))
    {
        log_e("Failed to obtain time");
        return;
    }
    log_d("Current time: %s", asctime(&timeinfo));
}

void logMemory()
{
    xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
    log_d("-- Memory Info --");
    log_d("Free heap: %d", ESP.getFreeHeap());
    log_d("Min free heap: %d", ESP.getMinFreeHeap());
    log_d("Min free stack: %d", uxTaskGetStackHighWaterMark(NULL));
    log_d("-- Memory Info End --");
    xSemaphoreGive(lvgl_mutex);
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
        splashUI->show();
        xSemaphoreGive(lvgl_mutex);
        break;
    case STATE_WIFI_SETUP:
        xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
        wifiSetupUI->show();
        xSemaphoreGive(lvgl_mutex);
        break;
    case STATE_DASHBOARD:
        xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
        dashboardUI->show();
        if (inverterData.status == DONGLE_STATUS_OK)
        {
            dashboardUI->update(inverterData, inverterData, uiMedianPowerSampler, shellyResult, shellyResult, wallboxData, wallboxData, solarChartDataProvider, electricityPriceResult, wifiSignalPercent());
            previousShellyResult = shellyResult;
            previousInverterData = inverterData;
            previousWallboxData = wallboxData;
            previousInverterData.millis = 0;
        }
        xSemaphoreGive(lvgl_mutex);
        break;
    }
}

void onLeaving(state_t oldState)
{
    log_d("Leaving state %d", oldState);
    switch (oldState)
    {
    case BOOT:
        break;
    case STATE_SPLASH:
        xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
        splashUI->updateText("");
        xSemaphoreGive(lvgl_mutex);
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
    {
        dongleDiscovery.scanWiFi();
        wifiDiscoveryResult = dongleDiscovery.getAutoconnectDongle();
        if (wifiDiscoveryResult.type != CONNECTION_TYPE_NONE)
        {
            log_d("Autoconnect dongle found: %s, type: %d", wifiDiscoveryResult.ssid.c_str(), wifiDiscoveryResult.type);
            moveToState(STATE_SPLASH);
        }
        else
        {
            moveToState(STATE_WIFI_SETUP);
        }
    }
    break;
    case STATE_SPLASH:
        xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
        splashUI->update(softAP.getESPIdHex(), String(VERSION_NUMBER));
        splashUI->updateText("Discovering dongles...");
        xSemaphoreGive(lvgl_mutex);

        if (wifiDiscoveryResult.type != CONNECTION_TYPE_NONE)
        {
            xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
            splashUI->updateText("Connecting... " + wifiDiscoveryResult.ssid);
            xSemaphoreGive(lvgl_mutex);

            if (dongleDiscovery.connectToDongle(wifiDiscoveryResult))
            {
                xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
                splashUI->updateText("Loading data... ");
                xSemaphoreGive(lvgl_mutex);

                syncTime();

                for (int retry = 0; retry < 3; retry++)
                {
                    inverterData = loadInverterData(wifiDiscoveryResult);
                    if (inverterData.status == DONGLE_STATUS_OK)
                    {
                        break;
                    }
                    delay(2000);
                }

                if (inverterData.status == DONGLE_STATUS_OK)
                {
                    moveToState(STATE_DASHBOARD);
                }
                else
                {
                    xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
                    splashUI->updateText("Failed to load data :-(");
                    xSemaphoreGive(lvgl_mutex);
                    delay(2000);

                    dongleDiscovery.disconnect();
                    moveToState(STATE_WIFI_SETUP);
                }
            }
            else
            {
                xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
                splashUI->updateText("Failed to connect :-(");
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
        if (wifiSetupUI->result.type != CONNECTION_TYPE_NONE)
        {
            wifiDiscoveryResult = wifiSetupUI->result;
            moveToState(STATE_SPLASH);
        }
#if DEMO
        moveToState(STATE_DASHBOARD);
#endif
        break;
    case STATE_DASHBOARD:
        if (showSettings)
        {
            showSettings = false;
            moveToState(STATE_WIFI_SETUP);
        }
        else if ((millis() - previousInverterData.millis) > UI_REFRESH_INTERVAL)
        {
            xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
            dashboardUI->update(inverterData, previousInverterData.status == DONGLE_STATUS_OK ? previousInverterData : inverterData, uiMedianPowerSampler, shellyResult, previousShellyResult, wallboxData, previousWallboxData, solarChartDataProvider, electricityPriceResult, wifiSignalPercent());
            xSemaphoreGive(lvgl_mutex);

            previousShellyResult = shellyResult;
            previousInverterData = inverterData;
            previousWallboxData = wallboxData;
            previousInverterData.millis = millis(); // this ensures that if we dont have new data, we will use the old data
            backlightResolver.resolve(inverterData);

            logMemory();
        }
        else
        {
            // only one task per state update
            if (loadInverterDataTask())
            {
                break;
            }
            if (discoverDonglesTask())
            {
                break;
            }
            if (pairShellyTask())
            {
                break;
            }
            if (reloadShellyTask())
            {
                break;
            }
            if (loadEcoVolterTask())
            {
                resolveEcoVolterSmartCharge();
                break;
            }
            if(loadElectricityPriceTask())
            {
                break;
            }

            if (!ecoVolterAPI.isDiscovered()) // ecovolter has priority
            {
                if (loadSolaxWallboxTask())
                {
                    resolveSolaxSmartCharge();
                    break;
                }
            }
        }

        break;
    }
}

void loop()
{
    updateState();
    delay(50);
}