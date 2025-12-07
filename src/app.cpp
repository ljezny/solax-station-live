#include <Arduino.h>
#include <lvgl.h>
#include <esp_task_wdt.h>
#include <sys/time.h>

#include <SPI.h>
#include "consts.h"
#include "ui/ui.h"
#include "Inverters/WiFiDiscovery.hpp"
#include "Inverters/Goodwe/GoodweDongleAPI.hpp"
#include "Inverters/Solax/SolaxModbusDongleAPI.hpp"
#include "Inverters/SofarSolar/SofarSolarDongleAPI.hpp"
#include "Inverters/Deye/DeyeDongleAPI.hpp"
#include "Inverters/Victron/VictronDongleAPI.hpp"
#include "Inverters/Growatt/GrowattDongleAPI.hpp"
#include "Wallbox/EcoVolterProV2.hpp"
#include "Wallbox/SolaxWallboxLocalAPI.hpp"
#include "Shelly/Shelly.hpp"
#include "utils/UnitFormatter.hpp"
#include "utils/SolarChartDataProvider.hpp"
#include "utils/BacklightResolver.hpp"
#include "DashboardUI.hpp"
#include "SplashUI.hpp"
#include "WiFiSetupUI.hpp"
#include "IntelligenceSetupUI.hpp"
#include "utils/SoftAP.hpp"
#include "utils/SmartControlRuleResolver.hpp"
#include "utils/MedianPowerSampler.hpp"
#include "Spot/ElectricityPriceLoader.hpp"
#include "utils/IntelligenceSettings.hpp"
#include "utils/IntelligenceResolver.hpp"
#include "utils/IntelligenceSimulator.hpp"
#include "utils/ConsumptionPredictor.hpp"
#include "utils/ProductionPredictor.hpp"
#include "utils/WebServer.hpp"
#include "utils/NVSMutex.hpp"

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
// Full-screen double buffers allocated in PSRAM for best FPS
static lv_color_t *disp_draw_buf1 = nullptr;
static lv_color_t *disp_draw_buf2 = nullptr;
static lv_disp_drv_t disp_drv;

static long lastElectricityPriceAttempt = 0;
static long lastWallboxDiscoveryAttempt = 0;
static long lastEcoVolterAttempt = 0;
static long lastWallboxStatusAttempt = 0;
static long lastShellyAttempt = 0;
static long lastShellyPairAttempt = 0;
static long lastWiFiScanAttempt = 0;
static long lastInverterDataAttempt = 0;

SET_LOOP_TASK_STACK_SIZE(8 * 1024); // use freeStack

WiFiDiscovery dongleDiscovery;
ShellyAPI shellyAPI;
EcoVolterProAPIV2 ecoVolterAPI;
SolaxWallboxLocalAPI solaxWallboxAPI;
BacklightResolver backlightResolver;
SoftAP softAP;
Touch touch;
WebServer webServer;

InverterData_t inverterData;
InverterData_t previousInverterData;
WallboxResult_t wallboxData;
WallboxResult_t previousWallboxData;
ShellyResult_t shellyResult;
ShellyResult_t previousShellyResult;
// Electricity price data - allocated in PSRAM due to size (2 days = ~3KB each)
ElectricityPriceTwoDays_t *electricityPriceResult = nullptr;
ElectricityPriceTwoDays_t *previousElectricityPriceResult = nullptr;
SolarChartDataProvider solarChartDataProvider;
static bool chartDataLoaded = false; // Track if chart data was loaded after time sync
static bool ntpTimeSynced = false;   // Track if time was synced via NTP
MedianPowerSampler shellyMedianPowerSampler;
MedianPowerSampler wallboxMedianPowerSampler;
MedianPowerSampler uiMedianPowerSampler;
SmartControlRuleResolver shellyRuleResolver(shellyMedianPowerSampler);
SmartControlRuleResolver wallboxRuleResolver(wallboxMedianPowerSampler);

// Intelligence components
ConsumptionPredictor consumptionPredictor;
ProductionPredictor productionPredictor;
IntelligenceResolver intelligenceResolver(consumptionPredictor, productionPredictor);
IntelligenceResult_t lastIntelligenceResult;
static InverterMode_t lastSentMode = INVERTER_MODE_UNKNOWN; // Last mode sent to inverter
static long lastIntelligenceAttempt = 0;
static int lastProcessedQuarter = -1;        // Track last quarter when we processed intelligence
#define INTELLIGENCE_REFRESH_INTERVAL 300000 // 5 minutes - recalculate every 5 minutes

// Pending mode change from UI - processed in mainUpdateTask to avoid blocking LVGL
static volatile InverterMode_t pendingModeChange = INVERTER_MODE_UNKNOWN;
static volatile bool pendingModeChangeRequest = false;

SplashUI *splashUI = NULL;
WiFiSetupUI *wifiSetupUI = NULL;
DashboardUI *dashboardUI = NULL;
IntelligenceSetupUI *intelligenceSetupUI = NULL;

WiFiDiscoveryResult_t wifiDiscoveryResult;

typedef enum
{
    BOOT,
    STATE_SPLASH,
    STATE_WIFI_SETUP,
    STATE_INTELLIGENCE_SETUP,
    STATE_DASHBOARD
} state_t;
state_t state;
state_t previousState;

bool showSettings = false;
bool showIntelligenceSettings = false;

/* Display flushing */
static uint32_t flushCount = 0;
static uint32_t lastFlushLog = 0;
static uint32_t maxFlushTime = 0;
static uint32_t totalFlushTime = 0;

void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    uint32_t startTime = micros();

    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    // Wait for previous DMA to complete before starting new one
    tft.waitDMA();

    // Start DMA transfer
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
    inverterData.inverterOutpuPowerL1 = random(500, 600);
    inverterData.inverterOutpuPowerL2 = random(300, 400);
    inverterData.inverterOutpuPowerL3 = random(1000, 1200);
    //inverterData.inverterPower = inverterData.L1Power + inverterData.L2Power + inverterData.L3Power;
    inverterData.loadPower = random(800, 1200);
    inverterData.loadToday = random(20, 60);
    inverterData.gridPowerL1 = random(-1000, 1000);
    inverterData.gridPowerL2 = random(-1000, 1000);
    inverterData.gridPowerL3 = random(-1000, 1000);
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
    // Subscribe this task to watchdog
    esp_task_wdt_add(NULL);

    static uint32_t lastLvglLog = 0;
    static uint32_t lvglCallCount = 0;
    static uint32_t maxLvglTime = 0;
    static uint32_t totalLvglTime = 0;
    static uint32_t maxMutexWait = 0;

    for (;;)
    {
        uint32_t mutexWaitStart = micros();

        // Try to take mutex with timeout - if it takes too long, something is blocking
        if (xSemaphoreTake(lvgl_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
        {
            // Mutex blocked - reset watchdog anyway to prevent timeout
            esp_task_wdt_reset();
            static uint32_t lastMutexWarning = 0;
            if (millis() - lastMutexWarning > 5000)
            {
                log_w("LVGL mutex timeout - something blocking UI!");
                lastMutexWarning = millis();
            }
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        uint32_t mutexWait = micros() - mutexWaitStart;
        if (mutexWait > maxMutexWait)
            maxMutexWait = mutexWait;

        uint32_t startTime = micros();
        lv_timer_handler();
        uint32_t elapsed = micros() - startTime;

        xSemaphoreGive(lvgl_mutex);
        totalLvglTime += elapsed;
        if (elapsed > maxLvglTime)
            maxLvglTime = elapsed;
        lvglCallCount++;

        // Log every 5 seconds
        if (millis() - lastLvglLog > 5000)
        {
            log_d("LVGL handler: %d calls (%.1f/s), avg: %dus, max: %dus, maxMutexWait: %dus",
                  lvglCallCount, lvglCallCount / 5.0f,
                  lvglCallCount > 0 ? totalLvglTime / lvglCallCount : 0,
                  maxLvglTime, maxMutexWait);
            lvglCallCount = 0;
            totalLvglTime = 0;
            maxLvglTime = 0;
            maxMutexWait = 0;
            lastLvglLog = millis();
        }

        // Reset watchdog to prevent timeout during long renders
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(15)); // Match LV_DISP_DEF_REFR_PERIOD (15ms = ~66 FPS)
    }
}

void onSettingsShow(lv_event_t *e)
{
    showSettings = true;
}

void onIntelligenceShow(lv_event_t *e)
{
    showIntelligenceSettings = true;
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

    // Use smaller buffers in internal DMA-capable RAM for faster rendering
    // 1/10 of screen = 800*48 pixels = ~77KB per buffer (fits in internal RAM)
    size_t bufferLines = 48; // Number of lines per buffer
    size_t bufferSize = screenWidth * bufferLines * sizeof(lv_color_t);

    // Try to allocate in internal DMA-capable RAM first
    disp_draw_buf1 = (lv_color_t *)heap_caps_malloc(bufferSize, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    disp_draw_buf2 = (lv_color_t *)heap_caps_malloc(bufferSize, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

    if (!disp_draw_buf1 || !disp_draw_buf2)
    {
        log_w("Failed to allocate in internal RAM, falling back to PSRAM");
        // Free any partial allocation
        if (disp_draw_buf1)
            heap_caps_free(disp_draw_buf1);
        if (disp_draw_buf2)
            heap_caps_free(disp_draw_buf2);

        // Fallback to PSRAM with full screen buffers
        bufferSize = screenWidth * screenHeight * sizeof(lv_color_t);
        disp_draw_buf1 = (lv_color_t *)heap_caps_malloc(bufferSize, MALLOC_CAP_SPIRAM);
        disp_draw_buf2 = (lv_color_t *)heap_caps_malloc(bufferSize, MALLOC_CAP_SPIRAM);
        lv_disp_draw_buf_init(&draw_buf, disp_draw_buf1, disp_draw_buf2, screenWidth * screenHeight);
        log_d("Display buffers in PSRAM: 2x %.1f KB", bufferSize / 1024.0f);
    }
    else
    {
        lv_disp_draw_buf_init(&draw_buf, disp_draw_buf1, disp_draw_buf2, screenWidth * bufferLines);
        log_d("Display buffers in internal DMA RAM: 2x %.1f KB (%d lines)", bufferSize / 1024.0f, bufferLines);
    }
    /* Initialize the display */
    lv_disp_drv_init(&disp_drv);
    /* Change the following line to your display resolution */
    disp_drv.hor_res = screenWidth;
    disp_drv.ver_res = screenHeight;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.full_refresh = 0; // Only redraw dirty areas for better FPS
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    /* Initialize the (dummy) input device driver */
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);

    // Set up NVS mutex to wait for display DMA before flash writes
    NVSMutex::setWaitDMACallback([]()
                                 { tft.waitDMA(); });

    ui_init();
    splashUI = new SplashUI();
    dashboardUI = new DashboardUI(onSettingsShow, onIntelligenceShow);
    wifiSetupUI = new WiFiSetupUI(dongleDiscovery);
    intelligenceSetupUI = new IntelligenceSetupUI();

    // Set callback for inverter mode change from dashboard menu
    // NOTE: This callback is called from LVGL event handler, so we must NOT do network
    // operations here. Instead, we queue the request and process it in mainUpdateTask.
    dashboardUI->setModeChangeCallback([](InverterMode_t mode, bool enableIntelligence)
                                       {
        log_d("Mode change requested: mode=%d, intelligence=%d", mode, enableIntelligence);
        
        // Update intelligence settings (NVS is fast, OK to do here)
        IntelligenceSettings_t settings = IntelligenceSettingsStorage::load();
        settings.enabled = enableIntelligence;
        IntelligenceSettingsStorage::save(settings);
        
        // If not intelligence mode, queue command for mainUpdateTask (network operation)
        if (!enableIntelligence && mode != INVERTER_MODE_UNKNOWN) {
            pendingModeChange = mode;
            pendingModeChangeRequest = true;
        }
        
        // Reset quarter tracking and trigger immediate intelligence task run
        lastProcessedQuarter = -1;
        lastIntelligenceAttempt = 0; });

    // Load predictors from storage
    // NOTE: Predictors use day-of-week (0-6), so they work even without correct time
    consumptionPredictor.loadFromPreferences();
    productionPredictor.loadFromPreferences();

    // NOTE: solarChartDataProvider.loadFromPreferences() is called after syncTime()
    // because it needs correct day-of-year to match saved data

    xTaskCreatePinnedToCore(lvglTimerTask, "lvglTimerTask", 12 * 1024, NULL, 24, NULL, 1);
}

void setup()
{
    Serial.begin(921600);

    // NOTE: loopTask is NOT subscribed to watchdog because it performs
    // long-running network operations (HTTPS, DNS, SSL handshake, etc.)
    // that can legitimately take several seconds. The LVGL task monitors
    // system responsiveness instead.

    // Allocate electricity price structures in PSRAM
    electricityPriceResult = (ElectricityPriceTwoDays_t *)heap_caps_calloc(1, sizeof(ElectricityPriceTwoDays_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    previousElectricityPriceResult = (ElectricityPriceTwoDays_t *)heap_caps_calloc(1, sizeof(ElectricityPriceTwoDays_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (!electricityPriceResult || !previousElectricityPriceResult)
    {
        log_e("Failed to allocate electricity price structures in PSRAM!");
    }
    else
    {
        log_d("Electricity price structures allocated in PSRAM (%d bytes each)", sizeof(ElectricityPriceTwoDays_t));
    }

    setupLVGL();
    setupWiFi();

    // Start web server on SoftAP
    webServer.begin(lvgl_mutex);
    webServer.setInverterData(&inverterData);
    webServer.setPriceData(electricityPriceResult);

    esp_log_level_set("wifi", ESP_LOG_VERBOSE);
}

void resetAllTasks()
{
    lastElectricityPriceAttempt = 0;
    lastWallboxDiscoveryAttempt = 0;
    lastEcoVolterAttempt = 0;
    lastWallboxStatusAttempt = 0;
    lastShellyAttempt = 0;
    lastShellyPairAttempt = 0;
    lastWiFiScanAttempt = 0;
    lastInverterDataAttempt = 0;
    lastIntelligenceAttempt = 0;
}

bool discoverDonglesTask()
{
    bool hasDongles = false;
    bool run = false;
    if (lastWiFiScanAttempt == 0 || millis() - lastWiFiScanAttempt > 60 * 1000)
    {
        run = true;
        lastWiFiScanAttempt = millis();
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
    static GrowattDongleAPI growattDongleAPI = GrowattDongleAPI();
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
    case CONNECTION_TYPE_GROWATT:
        d = growattDongleAPI.loadData(discoveryResult.inverterIP);
        break;
    default:
        d.status = DONGLE_STATUS_UNSUPPORTED_DONGLE;
        break;
    }
    log_d("Inverter data loaded in %d ms", millis() - millisBefore);
    return d;
}

/**
 * Check if current inverter type supports intelligence mode control
 */
bool supportsIntelligenceForCurrentInverter(WiFiDiscoveryResult_t &discoveryResult)
{
    switch (discoveryResult.type)
    {
    case CONNECTION_TYPE_SOLAX:
        return true; // Solax supports intelligence
    case CONNECTION_TYPE_GOODWE:
        return true; // Goodwe supports intelligence
    default:
        return false; // Other inverters don't support intelligence yet
    }
}

// Forward declaration - defined after syncTime()
void syncTimeFromInverter(const InverterData_t &data);

bool loadInverterDataTask()
{
    bool run = false;
    static int failures = 0;
    static int incrementalDelayTimeOnError = 0;
    if (lastInverterDataAttempt == 0 || (millis() - lastInverterDataAttempt) > (INVERTER_DATA_REFRESH_INTERVAL + incrementalDelayTimeOnError))
    {
        log_d("Loading inverter data");
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
                shellyMedianPowerSampler.addPowerSample(inverterData.pv1Power + inverterData.pv2Power + inverterData.pv3Power + inverterData.pv4Power, inverterData.soc, inverterData.batteryPower, inverterData.loadPower, inverterData.gridPowerL1 + inverterData.gridPowerL2 + inverterData.gridPowerL3);
                uiMedianPowerSampler.addPowerSample(inverterData.pv1Power + inverterData.pv2Power + inverterData.pv3Power + inverterData.pv4Power, inverterData.soc, inverterData.batteryPower, inverterData.loadPower, inverterData.gridPowerL1 + inverterData.gridPowerL2 + inverterData.gridPowerL3);
                wallboxMedianPowerSampler.addPowerSample(inverterData.pv1Power + inverterData.pv2Power + inverterData.pv3Power + inverterData.pv4Power, inverterData.soc, inverterData.batteryPower, inverterData.loadPower, inverterData.gridPowerL1 + inverterData.gridPowerL2 + inverterData.gridPowerL3);

                // Update intelligence settings with battery capacity from inverter
                // Note: Charge/discharge power is NOT loaded from inverter (user sets it manually)
                IntelligenceSettingsStorage::updateFromInverter(
                    inverterData.batteryCapacityWh);

                // Add samples for intelligence predictors
                int pvPower = inverterData.pv1Power + inverterData.pv2Power + inverterData.pv3Power + inverterData.pv4Power;
                bool consumptionQuarterChanged = consumptionPredictor.addSample(inverterData.loadPower);
                bool productionQuarterChanged = productionPredictor.addSample(pvPower);

                // Save predictors to flash when quarter changes
                if (consumptionQuarterChanged || productionQuarterChanged)
                {
                    log_d("Quarter changed, saving predictors to flash");
                    NVSGuard guard;
                    if (guard.isLocked())
                    {
                        consumptionPredictor.saveToPreferences();
                        productionPredictor.saveToPreferences();
                        solarChartDataProvider.saveToPreferences();
                    }
                }

                // Sync system time from inverter RTC if NTP failed
                syncTimeFromInverter(inverterData);

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
        lastInverterDataAttempt = millis();
    }
    return run;
}

bool pairShellyTask()
{
    bool run = false;
    if (lastShellyPairAttempt == 0 || millis() - lastShellyPairAttempt > 30000)
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
        lastShellyPairAttempt = millis();
        run = true;
    }
    return run;
}

bool reloadShellyTask()
{
    bool run = false;
    if (lastShellyAttempt == 0 || millis() - lastShellyAttempt > SHELLY_REFRESH_INTERVAL)
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

        lastShellyAttempt = millis();
        run = true;
    }
    return run;
}

bool loadElectricityPriceTask()
{
    bool run = false;
    if (lastElectricityPriceAttempt == 0 || millis() - lastElectricityPriceAttempt > ELECTRICITY_PRICE_REFRESH_INTERVAL)
    {
        log_d("Loading electricity price data");
        ElectricityPriceLoader loader;
        ElectricityPriceProvider_t provider = loader.getStoredElectricityPriceProvider();

        if (electricityPriceResult)
        {
            // Nejprve načteme dnešní data
            if (loader.loadTodayPrices(provider, electricityPriceResult))
            {
                log_d("Today electricity prices loaded");

                // Invalidate intelligence to recalculate with new prices
                lastIntelligenceAttempt = 0;

                // Zkusíme načíst zítřejší data (pokud je po 13h)
                time_t now = time(nullptr);
                struct tm timeinfoCopy;
                localtime_r(&now, &timeinfoCopy);

                if (timeinfoCopy.tm_hour >= 13)
                {
                    loader.loadTomorrowPrices(provider, electricityPriceResult);
                }
            }
        }

        lastElectricityPriceAttempt = millis();
        run = true;
    }
    return run;
}

bool runIntelligenceTask()
{
    bool run = false;
    static InverterMode_t intelligencePlan[QUARTERS_TWO_DAYS]; // Plan for today + tomorrow

    // Check if current time is aligned to 5-minute boundary (0, 5, 10, 15, 20, ...)
    time_t now_check = time(nullptr);
    struct tm *tm_check = localtime(&now_check);
    int currentMinute = tm_check->tm_min;
    bool is5MinuteAligned = (currentMinute % 5 == 0);

    // Run immediately if invalidated (lastIntelligenceAttempt == 0), otherwise only on 5-minute boundaries
    bool shouldRun = (lastIntelligenceAttempt == 0) ||
                     (is5MinuteAligned && millis() - lastIntelligenceAttempt > INTELLIGENCE_REFRESH_INTERVAL);

    if (shouldRun)
    {
        log_d("Running intelligence resolver");

        IntelligenceSettings_t settings = IntelligenceSettingsStorage::load();
        bool hasSpotPrices = electricityPriceResult && electricityPriceResult->updated > 0;
        bool canSimulate = settings.enabled && inverterData.status == DONGLE_STATUS_OK && hasSpotPrices;

        // Run simulation only if intelligence is enabled and we have all data
        if (canSimulate)
        {
            // Log input values
            time_t now_log = time(nullptr);
            struct tm *timeinfo_log = localtime(&now_log);
            int currentQuarterLog = (timeinfo_log->tm_hour * 60 + timeinfo_log->tm_min) / 15;
            float currentSpotPrice = electricityPriceResult->prices[currentQuarterLog].electricityPrice;
            float buyPrice = IntelligenceSettingsStorage::calculateBuyPrice(currentSpotPrice, settings);
            float sellPrice = IntelligenceSettingsStorage::calculateSellPrice(currentSpotPrice, settings);

            log_i("=== INTELLIGENCE SIMULATION ===");
            log_i("Time: %02d:%02d (Q%d), SOC: %d%%, Enabled: %s",
                  timeinfo_log->tm_hour, timeinfo_log->tm_min, currentQuarterLog, inverterData.soc,
                  settings.enabled ? "YES" : "NO");
            log_i("Spot: %.2f %s, Buy: %.2f, Sell: %.2f, Battery cost: %.2f",
                  currentSpotPrice, electricityPriceResult->currency, buyPrice, sellPrice, settings.batteryCostPerKwh);
            log_i("Battery: %.1f kWh capacity, min SOC: %d%%, max SOC: %d%%",
                  inverterData.batteryCapacityWh / 1000.0f, settings.minSocPercent, settings.maxSocPercent);

            // Run simulation to get all quarter decisions at once
            const auto &simResults = intelligenceResolver.runSimulation(inverterData, *electricityPriceResult, settings, true);
            const auto &summary = intelligenceResolver.getLastSummary();

            // Get current quarter result
            if (!simResults.empty())
            {
                lastIntelligenceResult.command = simResults[0].decision;
                lastIntelligenceResult.reason = simResults[0].reason;
                lastIntelligenceResult.expectedSavings = summary.totalSavingsCzk;
            }

            log_i("Recommended action: %s - %s",
                  IntelligenceResolver::commandToString(lastIntelligenceResult.command).c_str(),
                  lastIntelligenceResult.reason.c_str());

            // Generate plan for all future quarters (for chart display)
            time_t now = time(nullptr);
            struct tm *timeinfo = localtime(&now);
            int currentQuarter = (timeinfo->tm_hour * 60 + timeinfo->tm_min) / 15;

            // Determine how many quarters to plan (today only or today + tomorrow)
            int totalQuarters = electricityPriceResult->hasTomorrowData ? QUARTERS_TWO_DAYS : QUARTERS_OF_DAY;

            // Fill intelligencePlan from simulation results
            for (int q = 0; q < currentQuarter; q++)
            {
                intelligencePlan[q] = INVERTER_MODE_UNKNOWN;
            }

            for (size_t i = 0; i < simResults.size(); i++)
            {
                int q = simResults[i].quarter;
                if (q < QUARTERS_TWO_DAYS)
                {
                    intelligencePlan[q] = simResults[i].decision;
                }
            }

            // Log simulation summary
            log_i("=== SIMULATION SUMMARY ===");
            log_i("Total production: %.1f kWh, consumption: %.1f kWh",
                  summary.totalProductionKwh, summary.totalConsumptionKwh);
            log_i("Grid: bought %.1f kWh, sold %.1f kWh",
                  summary.totalFromGridKwh, summary.totalToGridKwh);
            log_i("Intelligent: cost %.1f CZK, final SOC %.0f%%",
                  summary.totalCostCzk, summary.finalBatterySoc);
            log_i("Baseline (dumb): cost %.1f CZK, final SOC %.0f%%",
                  summary.baselineCostCzk, summary.baselineFinalSoc);
            
            // Detailní info o arbitráži
            if (summary.chargedFromGridKwh > 0) {
                float avgChargeCost = summary.chargedFromGridCost / summary.chargedFromGridKwh;
                float potentialSavings = summary.chargedFromGridKwh * (summary.maxBuyPrice - avgChargeCost);
                log_i("Arbitrage: charged %.1f kWh @ avg %.1f CZK, max buy %.1f CZK, potential savings %.1f CZK",
                      summary.chargedFromGridKwh, avgChargeCost, summary.maxBuyPrice, potentialSavings);
            }
            
            log_i("Battery value adjustment: %.1f CZK (diff %.1f kWh @ %.1f CZK)",
                  summary.batteryValueAdjustment,
                  (summary.finalBatterySoc - summary.baselineFinalSoc) / 100.0f * settings.batteryCapacityKwh,
                  summary.maxBuyPrice - settings.batteryCostPerKwh);
            log_i("Savings vs dumb Self-Use: %.1f CZK", summary.totalSavingsCzk);

            // Log summary of mode changes only
            log_i("=== MODE CHANGES ===");
            int lastMode = -1;
            for (int q = currentQuarter; q < totalQuarters; q++)
            {
                if (intelligencePlan[q] != lastMode)
                {
                    int hour = (q % QUARTERS_OF_DAY) / 4;
                    int minute = ((q % QUARTERS_OF_DAY) % 4) * 15;
                    const char *modeName = IntelligenceResolver::commandToString((InverterMode_t)intelligencePlan[q]).c_str();
                    float spotPrice = (q < (electricityPriceResult->hasTomorrowData ? 192 : 96))
                                          ? electricityPriceResult->prices[q].electricityPrice
                                          : 0;
                    log_i("  Q%d (%02d:%02d): %s (spot: %.2f %s)",
                          q, hour, minute, modeName, spotPrice, electricityPriceResult->currency);
                    lastMode = intelligencePlan[q];
                }
            }

            // Fill remaining quarters with UNKNOWN if no tomorrow data
            for (int q = totalQuarters; q < QUARTERS_TWO_DAYS; q++)
            {
                intelligencePlan[q] = INVERTER_MODE_UNKNOWN;
            }

            // Use simulation summary for stats
            float remainingProduction = summary.totalProductionKwh;
            float remainingConsumption = summary.totalConsumptionKwh;
            float totalSavings = summary.totalSavingsCzk;

            // Update UI with intelligence state - split into small chunks to avoid blocking LVGL
            if (dashboardUI != nullptr)
            {
                xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
                dashboardUI->setIntelligenceState(settings.enabled, settings.enabled, hasSpotPrices);
                xSemaphoreGive(lvgl_mutex);

                xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
                dashboardUI->updateIntelligencePlanSummary(lastIntelligenceResult.command, intelligencePlan, currentQuarter, totalQuarters, totalSavings);
                xSemaphoreGive(lvgl_mutex);

                xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
                dashboardUI->updateIntelligenceUpcomingPlans(intelligencePlan, currentQuarter, totalQuarters, electricityPriceResult, &settings);
                xSemaphoreGive(lvgl_mutex);

                xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
                dashboardUI->updateIntelligenceStats(remainingProduction, remainingConsumption);
                xSemaphoreGive(lvgl_mutex);
            }

            // Send command to inverter ONLY if intelligence is enabled
            if (settings.enabled)
            {
                bool quarterChanged = (currentQuarter != lastProcessedQuarter);
                // Check if mode needs update: compare with lastSentMode AND actual inverter mode
                // This handles cases where someone changed mode manually in inverter app
                bool modeNeedsUpdate = (lastIntelligenceResult.command != INVERTER_MODE_UNKNOWN &&
                                        (lastIntelligenceResult.command != lastSentMode && lastSentMode != INVERTER_MODE_UNKNOWN ||
                                         lastIntelligenceResult.command != inverterData.inverterMode));

                if (quarterChanged || (modeNeedsUpdate && lastProcessedQuarter == -1))
                {
                    log_d("Quarter changed (%d -> %d) or first run, checking if mode update needed",
                          lastProcessedQuarter, currentQuarter);
                    lastProcessedQuarter = currentQuarter;

                    if (modeNeedsUpdate)
                    {
                        log_d("Mode update needed: command=%s, lastSent=%s, inverterMode=%s",
                              IntelligenceResolver::commandToString(lastIntelligenceResult.command).c_str(),
                              IntelligenceResolver::commandToString(lastSentMode).c_str(),
                              IntelligenceResolver::commandToString(inverterData.inverterMode).c_str());

                        bool success = false;
                        if (wifiDiscoveryResult.type == CONNECTION_TYPE_SOLAX)
                        {
                            static SolaxModbusDongleAPI solaxAPI;
                            // Použij Power Control místo Work Mode - bezpečnější s automatickým timeoutem
                            success = solaxAPI.setWorkModeViaPowerControl(
                                wifiDiscoveryResult.inverterIP, 
                                lastIntelligenceResult.command,
                                (int32_t)(settings.maxChargePowerKw * 1000),
                                (int32_t)(settings.maxDischargePowerKw * 1000),
                                1200);  // 20 minut timeout (přepočítává se každých 15 min)
                        }
                        else if (wifiDiscoveryResult.type == CONNECTION_TYPE_GOODWE)
                        {
                            static GoodweDongleAPI goodweAPI;
                            success = goodweAPI.setWorkMode(wifiDiscoveryResult.inverterIP, lastIntelligenceResult.command,
                                                            settings.minSocPercent, settings.maxSocPercent);
                        }
                        else
                        {
                            log_d("Work mode control not implemented for inverter type %d", wifiDiscoveryResult.type);
                        }
                        
                        if (success)
                        {
                            log_i("Successfully sent work mode %s to inverter",
                                  IntelligenceResolver::commandToString(lastIntelligenceResult.command).c_str());
                            lastSentMode = lastIntelligenceResult.command;
                        }
                        else if (wifiDiscoveryResult.type == CONNECTION_TYPE_SOLAX || wifiDiscoveryResult.type == CONNECTION_TYPE_GOODWE)
                        {
                            log_w("Failed to send work mode to inverter");
                        }
                    }
                    else
                    {
                        log_d("Mode unchanged (%s), no update sent",
                              IntelligenceResolver::commandToString(lastSentMode).c_str());
                    }
                }
            }
            else
            {
                // Intelligence disabled - reset tracking
                lastSentMode = INVERTER_MODE_UNKNOWN;
                lastProcessedQuarter = -1;
            }

            // Update chart predictions from simulation results (with SOC)
            solarChartDataProvider.clearPredictions(true);
            for (const auto &sim : simResults)
            {
                int q = sim.quarter;
                if (q < QUARTERS_TWO_DAYS)
                {
                    float predProductionWh = sim.productionKwh * 1000.0f;
                    float predConsumptionWh = sim.consumptionKwh * 1000.0f;
                    int predSoc = (int)sim.batterySoc;
                    solarChartDataProvider.setPrediction(q, predProductionWh, predConsumptionWh, predSoc);
                }
            }
        }
        else if (inverterData.status == DONGLE_STATUS_OK)
        {
            // Intelligence disabled or no spot prices - show only production/consumption predictions (no SOC)
            time_t now = time(nullptr);
            struct tm timeinfoCopy;
            localtime_r(&now, &timeinfoCopy);
            int currentQuarter = (timeinfoCopy.tm_hour * 60 + timeinfoCopy.tm_min) / 15;
            int currentMonth = timeinfoCopy.tm_mon;
            int currentDay = timeinfoCopy.tm_wday;
            int tomorrowDay = (currentDay + 1) % 7;

            // Calculate tomorrow's month
            time_t tomorrowTime = now + 24 * 60 * 60;
            struct tm tomorrowInfoCopy;
            localtime_r(&tomorrowTime, &tomorrowInfoCopy);
            int tomorrowMonth = tomorrowInfoCopy.tm_mon;

            solarChartDataProvider.clearPredictions(true);

            // Predictions for rest of today (SOC = -1 means don't show)
            for (int q = currentQuarter + 1; q < QUARTERS_OF_DAY; q++)
            {
                float predProductionWh = productionPredictor.predictQuarterlyProduction(currentMonth, q);
                float predConsumptionWh = consumptionPredictor.predictQuarterlyConsumption(currentDay, q);
                solarChartDataProvider.setPrediction(q, predProductionWh, predConsumptionWh, -1); // -1 = no SOC
            }

            // Predictions for tomorrow
            for (int q = 0; q < QUARTERS_OF_DAY; q++)
            {
                float predProductionWh = productionPredictor.predictQuarterlyProduction(tomorrowMonth, q);
                float predConsumptionWh = consumptionPredictor.predictQuarterlyConsumption(tomorrowDay, q);
                solarChartDataProvider.setPrediction(QUARTERS_OF_DAY + q, predProductionWh, predConsumptionWh, -1);
            }

            // Update UI state
            lastSentMode = INVERTER_MODE_UNKNOWN;
            lastProcessedQuarter = -1;
            xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
            if (dashboardUI != nullptr)
            {
                dashboardUI->setIntelligenceState(false, settings.enabled, hasSpotPrices);
            }
            xSemaphoreGive(lvgl_mutex);
        }
        else
        {
            // No inverter data - just update UI state
            lastSentMode = INVERTER_MODE_UNKNOWN;
            lastProcessedQuarter = -1;
            xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
            if (dashboardUI != nullptr)
            {
                dashboardUI->setIntelligenceState(false, settings.enabled, hasSpotPrices);
            }
            xSemaphoreGive(lvgl_mutex);
        }

        lastIntelligenceAttempt = millis();
        run = true;
    }
    return run;
}

bool loadEcoVolterTask()
{
    bool run = false;
    static int failureCounter = 0;
    int period = ecoVolterAPI.isDiscovered() ? WALLBOX_STATUS_REFRESH_INTERVAL : WALLBOX_DISCOVERY_REFRESH_INTERVAL;
    if (lastEcoVolterAttempt == 0 || millis() - lastEcoVolterAttempt > period)
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
        lastEcoVolterAttempt = millis();
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
    bool run = false;
    int period = solaxWallboxAPI.isDiscovered() ? WALLBOX_STATUS_REFRESH_INTERVAL : WALLBOX_DISCOVERY_REFRESH_INTERVAL;
    if (lastWallboxStatusAttempt == 0 || millis() - lastWallboxStatusAttempt > period)
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
        lastWallboxStatusAttempt = millis();
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

void setTimeZone()
{
    ElectricityPriceLoader loader;
    String tz = loader.getStoredTimeZone();
    log_d("Stored time zone: %s", tz.c_str());
    if (tz.length() > 0)
    {

        setenv("TZ", tz.c_str(), 1);
        tzset();

        log_d("Time zone set to %s", tz.c_str());
    }
    else
    {
        log_d("No time zone stored, using default");
    }
}

void syncTime()
{
    // use ntp arduino
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    setTimeZone();
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 5000))
    {
        log_e("Failed to obtain time from NTP");
        ntpTimeSynced = false;
        return;
    }
    ntpTimeSynced = true;
    log_d("NTP sync successful, current time: %s", asctime(&timeinfo));
}

/**
 * Set system time from inverter RTC if NTP sync failed
 * This is useful when connected directly to inverter WiFi dongle without internet access
 */
void syncTimeFromInverter(const InverterData_t &data)
{
    // Only sync from inverter if NTP sync failed and inverter has valid time
    if (ntpTimeSynced || data.inverterTime == 0)
    {
        return;
    }

    // Validate inverter time (should be after 2020)
    struct tm timeinfo;
    localtime_r(&data.inverterTime, &timeinfo);
    if (timeinfo.tm_year < 120) // Year 2020 = 120 (years since 1900)
    {
        log_w("Inverter time is invalid (year %d), not syncing", timeinfo.tm_year + 1900);
        return;
    }

    // Set system time from inverter
    struct timeval tv = {.tv_sec = data.inverterTime, .tv_usec = 0};
    settimeofday(&tv, nullptr);

    // Apply timezone
    setTimeZone();

    log_i("System time set from inverter RTC: %04d-%02d-%02d %02d:%02d:%02d",
          timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
          timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    // Load chart data now that we have valid time (if not already loaded)
    if (!chartDataLoaded)
    {
        solarChartDataProvider.loadFromPreferences();
        chartDataLoaded = true;
    }
}

void logMemory()
{
    // Don't take lvgl_mutex just for logging - it's not needed and can block LVGL
    log_d("-- Memory Info --");
    log_d("Free heap: %d", ESP.getFreeHeap());
    log_d("Min free heap: %d", ESP.getMinFreeHeap());
    log_d("Min free stack: %d", uxTaskGetStackHighWaterMark(NULL));
    log_d("-- Memory Info End --");
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
    case STATE_INTELLIGENCE_SETUP:
        xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
        intelligenceSetupUI->show();
        xSemaphoreGive(lvgl_mutex);
        break;
    case STATE_DASHBOARD:
        // Split into smaller mutex locks to avoid blocking LVGL timer
        xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
        dashboardUI->setIntelligenceSupported(supportsIntelligenceForCurrentInverter(wifiDiscoveryResult));
        dashboardUI->show();
        xSemaphoreGive(lvgl_mutex);

        if (inverterData.status == DONGLE_STATUS_OK && electricityPriceResult && previousElectricityPriceResult)
        {
            xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
            dashboardUI->update(inverterData, inverterData, uiMedianPowerSampler, shellyResult, shellyResult, wallboxData, wallboxData, solarChartDataProvider, *electricityPriceResult, *previousElectricityPriceResult, wifiSignalPercent());
            xSemaphoreGive(lvgl_mutex);

            previousShellyResult = shellyResult;
            previousInverterData = inverterData;
            previousWallboxData = wallboxData;
            *previousElectricityPriceResult = *electricityPriceResult;
            previousInverterData.millis = 0;
        }

        resetAllTasks();
        setTimeZone();
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
    case STATE_INTELLIGENCE_SETUP:
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

                // Load chart data AFTER time sync (needs correct day-of-year)
                // Only load if NTP succeeded (valid time available)
                if (ntpTimeSynced && !chartDataLoaded)
                {
                    solarChartDataProvider.loadFromPreferences();
                    chartDataLoaded = true;
                }

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
    case STATE_INTELLIGENCE_SETUP:
        if (intelligenceSetupUI->resultSaved || intelligenceSetupUI->resultCancelled)
        {
            // If settings were saved, trigger immediate intelligence recalculation
            if (intelligenceSetupUI->resultSaved)
            {
                lastIntelligenceAttempt = 0;
                lastProcessedQuarter = -1;
                log_d("Intelligence settings saved, triggering recalculation");
            }
            moveToState(STATE_DASHBOARD);
        }
        break;
    case STATE_DASHBOARD:
        if (showSettings)
        {
            showSettings = false;
            moveToState(STATE_WIFI_SETUP);
        }
        else if (showIntelligenceSettings && supportsIntelligenceForCurrentInverter(wifiDiscoveryResult))
        {
            showIntelligenceSettings = false;
            moveToState(STATE_INTELLIGENCE_SETUP);
        }
        else if (showIntelligenceSettings)
        {
            // Intelligence not supported for this inverter, ignore request
            showIntelligenceSettings = false;
        }
        else if ((millis() - previousInverterData.millis) > UI_REFRESH_INTERVAL && electricityPriceResult && previousElectricityPriceResult)
        {
            xSemaphoreTake(lvgl_mutex, portMAX_DELAY);
            dashboardUI->update(inverterData, previousInverterData.status == DONGLE_STATUS_OK ? previousInverterData : inverterData, uiMedianPowerSampler, shellyResult, previousShellyResult, wallboxData, previousWallboxData, solarChartDataProvider, *electricityPriceResult, *previousElectricityPriceResult, wifiSignalPercent());
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
            // Process pending mode change from UI (network operation moved from LVGL callback)
            if (pendingModeChangeRequest)
            {
                pendingModeChangeRequest = false;
                InverterMode_t mode = pendingModeChange;
                log_d("Processing pending mode change: %d", mode);

                bool success = false;
                if (wifiDiscoveryResult.type == CONNECTION_TYPE_SOLAX)
                {
                    static SolaxModbusDongleAPI solaxAPI;
                    // Použij Power Control místo Work Mode - bezpečnější s automatickým timeoutem
                    // Pro manuální změnu použij kratší timeout (5 min)
                    success = solaxAPI.setWorkModeViaPowerControl(
                        wifiDiscoveryResult.inverterIP, mode,
                        8000, 8000,  // Default 8kW charge/discharge power
                        300);  // 5 minut timeout pro manuální změnu
                }
                else if (wifiDiscoveryResult.type == CONNECTION_TYPE_GOODWE)
                {
                    static GoodweDongleAPI goodweAPI;
                    // Use default SOC values for manual mode changes (10% min, 100% max)
                    success = goodweAPI.setWorkMode(wifiDiscoveryResult.inverterIP, mode, 10, 100);
                }
                
                if (success)
                {
                    log_d("Successfully sent work mode %d to inverter", mode);
                    lastSentMode = mode;
                }
                else if (wifiDiscoveryResult.type == CONNECTION_TYPE_SOLAX || wifiDiscoveryResult.type == CONNECTION_TYPE_GOODWE)
                {
                    log_w("Failed to send work mode %d to inverter", mode);
                }
                break;
            }

            // only one task per state update
            if (loadInverterDataTask())
            {
                break;
            }
            if (loadElectricityPriceTask())
            {
                break;
            }
            if (runIntelligenceTask())
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

    vTaskDelay(pdMS_TO_TICKS(1));
}