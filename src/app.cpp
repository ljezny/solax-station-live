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
#include "utils/MedianPowerSampler.hpp"
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <nvs_flash.h>
#define UI_REFRESH_INTERVAL 5000 // Define the UI refresh interval in milliseconds
#define INVERTER_DATA_REFRESH_INTERVAL 2000
#define SHELLY_REFRESH_INTERVAL 2000

#include "Touch/Touch.hpp"
#include <mutex>
#include <Wire.h>

SemaphoreHandle_t lvgl_mutex = xSemaphoreCreateMutex();

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
MedianPowerSampler shellyMedianPowerSampler;
MedianPowerSampler uiMedianPowerSampler;
ShellyRuleResolver shellyRuleResolver(shellyMedianPowerSampler);

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

SemaphoreHandle_t sem_vsync_end;
SemaphoreHandle_t sem_gui_ready;

bool IRAM_ATTR example_on_vsync_event(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t *event_data, void *user_data)
{
    BaseType_t high_task_awoken = pdFALSE;

    if (xSemaphoreTakeFromISR(sem_gui_ready, &high_task_awoken) == pdTRUE)
    {
        xSemaphoreGiveFromISR(sem_vsync_end, &high_task_awoken);
    }

    return high_task_awoken == pdTRUE;
}

void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;

    xSemaphoreGive(sem_gui_ready);
    xSemaphoreTake(sem_vsync_end, portMAX_DELAY);
    
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, px_map);
    lv_display_flush_ready(disp);
    
}

void my_touchpad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    if (true || !touch.hasTouch())
    {
        data->state = LV_INDEV_STATE_RELEASED;
    }
    else
    {
        data->state = LV_INDEV_STATE_PRESSED;
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
        uint32_t delay = lv_timer_handler();
        xSemaphoreGive(lvgl_mutex);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void lvglIncTask(void *param)
{
    for (;;)
    {
        lv_tick_inc(2);
        vTaskDelay(pdMS_TO_TICKS(2));
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

    backlightResolver.setup();

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_rgb_panel_config_t panel_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = (18 * 1000 * 1000),
            .h_res = 800,
            .v_res = 480,
            .hsync_pulse_width = 4,
            .hsync_back_porch = 8,
            .hsync_front_porch = 8,
            .vsync_pulse_width = 4,
            .vsync_back_porch = 8,
            .vsync_front_porch = 8,
            .flags = {
                .pclk_active_neg = true,
            },
        },
        .data_width = 16,
        .bits_per_pixel = 0,
        .num_fbs = 2,
        .bounce_buffer_size_px = 20 * 800,
        .dma_burst_size = 64,
        .hsync_gpio_num = GPIO_NUM_40,
        .vsync_gpio_num = GPIO_NUM_41,
        .de_gpio_num = GPIO_NUM_42,
        .pclk_gpio_num = GPIO_NUM_39,
        .disp_gpio_num = -1,
        .data_gpio_nums = {
            GPIO_NUM_21,
            GPIO_NUM_47,
            GPIO_NUM_48,
            GPIO_NUM_45,
            GPIO_NUM_38,
            GPIO_NUM_9,
            GPIO_NUM_10,
            GPIO_NUM_11,
            GPIO_NUM_12,
            GPIO_NUM_13,
            GPIO_NUM_14,
            GPIO_NUM_7,
            GPIO_NUM_17,
            GPIO_NUM_18,
            GPIO_NUM_3,
            GPIO_NUM_46,
        },
        .flags = {
            .fb_in_psram = 1, // allocate frame buffer in PSRAM
        }};
    esp_lcd_new_rgb_panel(&panel_config, &panel_handle);
    esp_lcd_panel_reset(panel_handle);
    esp_lcd_panel_init(panel_handle);

    sem_vsync_end = xSemaphoreCreateBinary();
    sem_gui_ready = xSemaphoreCreateBinary();
    
    // esp_lcd_panel_mirror(panel_handle, true, true);

    delay(200);

    lv_init();
    lv_display_t *display = lv_display_create(800, 480);
    lv_display_set_user_data(display, panel_handle);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    // lv_display_set_rotation(display, LV_DISPLAY_ROTATION_180);
    //  touch setup
    touch.init();

    void *buf1 = NULL;
    void *buf2 = NULL;
    // size_t draw_buffer_sz = 800 * 50 * 2;
    // buf1 = heap_caps_malloc(draw_buffer_sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    // lv_display_set_buffers(display, buf1, NULL, draw_buffer_sz, LV_DISPLAY_RENDER_MODE_PARTIAL);

    esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 2, &buf1, &buf2);
    lv_display_set_buffers(display, buf1, buf2, 800 * 480 * 2, LV_DISPLAY_RENDER_MODE_FULL);

    lv_display_set_flush_cb(display, my_disp_flush);
    esp_lcd_rgb_panel_event_callbacks_t cbs = {
        .on_vsync = example_on_vsync_event,
    };
    esp_lcd_rgb_panel_register_event_callbacks(panel_handle, &cbs, display);

    lv_indev_t *indev = lv_indev_create();           /* Create input device connected to Default Display. */
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER); /* Touch pad is a pointer-like device. */
    lv_indev_set_read_cb(indev, my_touchpad_read);   /* Set driver function. */

    ui_init();

    xTaskCreatePinnedToCore(lvglTimerTask, "lvglTimerTask", 12 * 1024, NULL, 10, NULL, 1);
    xTaskCreatePinnedToCore(lvglIncTask, "lvglIncTask", 1 * 1024, NULL, 10, NULL, 1);
}

void setup()
{
    Serial.begin(115200);
    setupWiFi();
    setupLVGL();
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
        return run;
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
                shellyMedianPowerSampler.addPowerSample(inverterData.pv1Power + inverterData.pv2Power + inverterData.pv3Power + inverterData.pv4Power, inverterData.soc, inverterData.batteryPower, inverterData.loadPower, inverterData.feedInPower);
                uiMedianPowerSampler.addPowerSample(inverterData.pv1Power + inverterData.pv2Power + inverterData.pv3Power + inverterData.pv4Power, inverterData.soc, inverterData.batteryPower, inverterData.loadPower, inverterData.feedInPower);
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
                    WiFi.disconnect();
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
        if (state != SHELLY_UNKNOWN)
        {
            delay(1000);
            shellyAPI.updateState(state, 5 * 60);

            // state should change
            if ((shellyResult.activeCount == 0 && state > SHELLY_FULL_OFF) || (shellyResult.activeCount > 0 && state < SHELLY_KEEP_CURRENT_STATE))
            {
                shellyResult = shellyAPI.getState(); // reload state after update
            }
        }

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
            dashboardUI.update(inverterData, inverterData, uiMedianPowerSampler, shellyResult, shellyResult, solarChartDataProvider, wifiSignalPercent());
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
            dashboardUI.update(inverterData, previousInverterData.status == DONGLE_STATUS_OK ? previousInverterData : inverterData, uiMedianPowerSampler, shellyResult, previousShellyResult, solarChartDataProvider, wifiSignalPercent());
            xSemaphoreGive(lvgl_mutex);

            previousShellyResult = shellyResult;
            previousInverterData = inverterData;
            previousInverterData.millis = millis(); // this ensures that if we dont have new data, we will use the old data
            backlightResolver.resolve(inverterData);
        }
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
        if (resetWifiTask())
        {
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