#include <Arduino.h>
#include <ESP_Panel_Library.h>
#include <lvgl.h>
#include "lvgl_port_v8.h"
#include "ui/ui.h"
#include "Solax/SolaxDongleDiscovery.hpp"
#include "Solax/SolaxDongleAPI.hpp"
#include "utils/UnitFormatter.hpp"
#include "utils/SolarChartDataProvider.hpp"
#include "utils/UIBallAnimator.hpp"
#include "utils/BacklightResolver.hpp"
#include <mat.h>
#include "Shelly/Shelly.hpp"

SET_LOOP_TASK_STACK_SIZE(48 * 1024);

#define SOFT_AP_SSID_PREFIX String("SolarStationLive-")

SolaxDongleAPI dongleAPI;
SolaxDongleDiscovery dongleDiscovery;
ShellyAPI shellyAPI;
BacklightResolver backlightResolver;

SolaxDongleInverterData_t inverterData;
SolaxDongleDiscoveryResult_t discoveryResult;
ShellyResult_t shellyResult;

ESP_Panel *panel = new ESP_Panel();
SolarChartDataProvider *solarChartDataProvider = new SolarChartDataProvider();

String getESPIdHex()
{
  char idHex[23];
  snprintf(idHex, 23, "%llX", ESP.getEfuseMac());

  return idHex;
}

SolaxDongleInverterData_t createRandomMockData()
{
    SolaxDongleInverterData_t inverterData;
    inverterData.status = SOLAX_DONGLE_STATUS_OK;
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

static void draw_event_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    /*Add the faded area before the lines are drawn*/
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    if (dsc->part == LV_PART_ITEMS)
    {
        if (!dsc->p1 || !dsc->p2)
            return;
        const lv_chart_series_t *ser = (const lv_chart_series_t *)dsc->sub_part_ptr;
        if (ser->y_axis_sec == 0)
        {
            return;
        }

        /*Add a line mask that keeps the area below the line*/
        lv_draw_mask_line_param_t line_mask_param;
        lv_draw_mask_line_points_init(&line_mask_param, dsc->p1->x, dsc->p1->y, dsc->p2->x, dsc->p2->y,
                                      LV_DRAW_MASK_LINE_SIDE_BOTTOM);
        int16_t line_mask_id = lv_draw_mask_add(&line_mask_param, NULL);

        /*Add a fade effect: transparent bottom covering top*/
        lv_coord_t h = lv_obj_get_height(obj);
        lv_draw_mask_fade_param_t fade_mask_param;
        lv_draw_mask_fade_init(&fade_mask_param, &obj->coords, LV_OPA_COVER, obj->coords.y1 + h / 8, LV_OPA_TRANSP,
                               obj->coords.y2);
        int16_t fade_mask_id = lv_draw_mask_add(&fade_mask_param, NULL);

        /*Draw a rectangle that will be affected by the mask*/
        lv_draw_rect_dsc_t draw_rect_dsc;
        lv_draw_rect_dsc_init(&draw_rect_dsc);
        draw_rect_dsc.bg_opa = LV_OPA_20;
        draw_rect_dsc.bg_color = dsc->line_dsc->color;

        lv_area_t a;
        a.x1 = dsc->p1->x;
        a.x2 = dsc->p2->x - 1;
        a.y1 = LV_MIN(dsc->p1->y, dsc->p2->y);
        a.y2 = obj->coords.y2;
        lv_draw_rect(dsc->draw_ctx, &draw_rect_dsc, &a);

        /*Remove the masks*/
        lv_draw_mask_free_param(&line_mask_param);
        lv_draw_mask_free_param(&fade_mask_param);
        lv_draw_mask_remove_id(line_mask_id);
        lv_draw_mask_remove_id(fade_mask_id);
    }
    else if (dsc->part == LV_PART_TICKS)
    {
        if (dsc->id == LV_CHART_AXIS_PRIMARY_Y)
        {
            lv_snprintf(dsc->text, dsc->text_length, "%d%%", dsc->value);
        }
        else if (dsc->id == LV_CHART_AXIS_SECONDARY_Y)
        {
            lv_snprintf(dsc->text, dsc->text_length, "%d\nkW", dsc->value / 1000);
        }
        else if (dsc->id == LV_CHART_AXIS_PRIMARY_X)
        {
            lv_snprintf(dsc->text, dsc->text_length, "%dh", -24 + 6 * dsc->value);
        }
    }
}

bool setupChart = false;
void updateChart()
{
    if (!setupChart)
    {
        setupChart = true;
        lv_obj_add_event_cb(ui_Chart1, draw_event_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);
    }

    while (lv_chart_get_series_next(ui_Chart1, NULL))
    {
        lv_chart_remove_series(ui_Chart1, lv_chart_get_series_next(ui_Chart1, NULL));
    }

    lv_chart_series_t *pvPowerSeries = lv_chart_add_series(ui_Chart1, lv_color_hex(_ui_theme_color_pvColor[0]), LV_CHART_AXIS_SECONDARY_Y);
    lv_chart_series_t *acPowerSeries = lv_chart_add_series(ui_Chart1, lv_color_hex(_ui_theme_color_loadColor[0]), LV_CHART_AXIS_SECONDARY_Y);
    lv_chart_series_t *socSeries = lv_chart_add_series(ui_Chart1, lv_color_hex(_ui_theme_color_batteryColor[0]), LV_CHART_AXIS_PRIMARY_Y);

    uint32_t i;

    float maxPower = 10000.0f;
    for (i = 0; i < CHART_SAMPLES_PER_DAY; i++)
    {
        SolarChartDataItem_t item = solarChartDataProvider->getData()[CHART_SAMPLES_PER_DAY - i - 1];

        lv_chart_set_next_value(ui_Chart1, pvPowerSeries, item.pvPower);
        lv_chart_set_next_value(ui_Chart1, acPowerSeries, item.loadPower);
        lv_chart_set_next_value(ui_Chart1, socSeries, item.soc);

        maxPower = max(maxPower, max(item.pvPower, item.loadPower));
    }
    lv_chart_set_range(ui_Chart1, LV_CHART_AXIS_SECONDARY_Y, 0, (lv_coord_t)maxPower);
}



void updateFlowAnimations()
{
    static UIBallAnimator *pvAnimator = NULL;
    static UIBallAnimator *batteryAnimator = NULL;
    static UIBallAnimator *gridAnimator = NULL;
    static UIBallAnimator *loadAnimator = NULL;
    static UIBallAnimator *shellyAnimator = NULL;

    int duration = 1400;
    int offsetY = 15;
    if (pvAnimator != NULL)
    {
        delete pvAnimator;
        pvAnimator = NULL;
    }
    if ((inverterData.pv1Power + inverterData.pv2Power) > 0)
    {
        pvAnimator = new UIBallAnimator(ui_LeftContainer, _ui_theme_color_pvColor);
        pvAnimator->run(ui_pvContainer, ui_inverterContainer, duration, 0, 0, -offsetY);
    }

    if (batteryAnimator != NULL)
    {
        delete batteryAnimator;
        batteryAnimator = NULL;
    }
    if (inverterData.batteryPower > 0)
    {
        batteryAnimator = new UIBallAnimator(ui_LeftContainer, _ui_theme_color_batteryColor);
        batteryAnimator->run(ui_inverterContainer, ui_batteryContainer, duration, duration, 1, -offsetY);
    }
    else if (inverterData.batteryPower < 0)
    {
        batteryAnimator = new UIBallAnimator(ui_LeftContainer, _ui_theme_color_batteryColor);
        batteryAnimator->run(ui_batteryContainer, ui_inverterContainer, duration, 0, 0, -offsetY);
    }

    if (gridAnimator != NULL)
    {
        delete gridAnimator;
        gridAnimator = NULL;
    }

    if (inverterData.feedInPower > 0)
    {
        gridAnimator = new UIBallAnimator(ui_LeftContainer, _ui_theme_color_gridColor);
        gridAnimator->run(ui_inverterContainer, ui_gridContainer, duration, duration, 1, offsetY);
    }
    else if (inverterData.feedInPower < 0)
    {
        gridAnimator = new UIBallAnimator(ui_LeftContainer, _ui_theme_color_gridColor);
        gridAnimator->run(ui_gridContainer, ui_inverterContainer, duration, 0, 0, offsetY);
    }

    if (loadAnimator != NULL)
    {
        delete loadAnimator;
        loadAnimator = NULL;
    }
    if (inverterData.loadPower > 0)
    {
        loadAnimator = new UIBallAnimator(ui_LeftContainer, _ui_theme_color_loadColor);
        loadAnimator->run(ui_inverterContainer, ui_loadContainer, duration, duration, 1, 20);
    }
    if(shellyAnimator != NULL) {
        delete shellyAnimator;
        shellyAnimator = NULL;
    }
    if(shellyResult.totalPower > 0) { //TODO: check if shelly is on
        shellyAnimator = new UIBallAnimator(ui_LeftContainer, _ui_theme_color_pvColor);
        shellyAnimator->run(ui_loadContainer, ui_shellyContainer, duration, duration, 1, 20);
    }
}

void updateDashboardUI()
{
    int selfUsePowerPercent = inverterData.loadPower > 0 ? (100 * (inverterData.loadPower + inverterData.feedInPower)) / inverterData.loadPower : 0;
    selfUsePowerPercent = constrain(selfUsePowerPercent, 0, 100);

    int selfUseEnergyTodayPercent = inverterData.loadToday > 0 ? ((inverterData.loadToday - inverterData.gridBuyToday) / inverterData.loadToday) * 100 : 0;
    selfUseEnergyTodayPercent = constrain(selfUseEnergyTodayPercent, 0, 100);

    int inPower = inverterData.pv1Power + inverterData.pv2Power;
    if (inverterData.batteryPower < 0)
    {
        inPower += abs(inverterData.batteryPower);
    }
    if (inverterData.feedInPower < 0)
    {
        inPower += abs(inverterData.feedInPower);
    }

    int outPower = inverterData.loadPower;
    if (inverterData.batteryPower > 0)
    {
        outPower += inverterData.batteryPower;
    }
    if (inverterData.feedInPower > 0)
    {
        outPower += inverterData.feedInPower;
    }
    int totalPhasePower = inverterData.L1Power + inverterData.L2Power + inverterData.L3Power;
    int l1PercentUsage = inverterData.L1Power > 0 ? (100 * inverterData.L1Power) / totalPhasePower : 0;
    int l2PercentUsage = inverterData.L2Power > 0 ? (100 * inverterData.L2Power) / totalPhasePower : 0;
    int l3PercentUsage = inverterData.L3Power > 0 ? (100 * inverterData.L3Power) / totalPhasePower : 0;

    lv_color_t black = lv_color_make(0, 0, 0);
    lv_color_t red = lv_color_make(192, 0, 0);
    lv_color_t orange = lv_color_make(192, 96, 0);
    lv_color_t green = lv_color_make(0, 128, 0);

    lv_label_set_text(ui_pvLabel, format(POWER, inverterData.pv1Power + inverterData.pv2Power).formatted.c_str());
    lv_label_set_text(ui_pv1Label, format(POWER, inverterData.pv1Power, 1.0f, true).formatted.c_str());
    lv_label_set_text(ui_pv2Label, format(POWER, inverterData.pv2Power, 1.0f, true).formatted.c_str());
    if (inverterData.pv1Power == 0 || inverterData.pv2Power == 0)
    { // hide
        lv_obj_add_flag(ui_pv1Label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_pv2Label, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_clear_flag(ui_pv1Label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ui_pv2Label, LV_OBJ_FLAG_HIDDEN);
    }

    lv_label_set_text_fmt(ui_inverterTemperatureLabel, "%d°C", inverterData.inverterTemperature);
    lv_label_set_text(ui_inverterPowerLabel, format(POWER, inverterData.inverterPower).formatted.c_str());
    lv_label_set_text(ui_inverterPowerL1Label, format(POWER, inverterData.L1Power).formatted.c_str());
    lv_obj_set_style_text_color(ui_inverterPowerL1Label, l1PercentUsage > 50 ? red : black, 0);
    lv_label_set_text(ui_inverterPowerL2Label, format(POWER, inverterData.L2Power).formatted.c_str());
    lv_obj_set_style_text_color(ui_inverterPowerL2Label, l2PercentUsage > 50 ? red : black, 0);
    lv_label_set_text(ui_inverterPowerL3Label, format(POWER, inverterData.L3Power).formatted.c_str());
    lv_obj_set_style_text_color(ui_inverterPowerL3Label, l3PercentUsage > 50 ? red : black, 0);
    lv_label_set_text(ui_loadPowerLabel, format(POWER, inverterData.loadPower).formatted.c_str());
    lv_label_set_text(ui_feedInPowerLabel, format(POWER, abs(inverterData.feedInPower)).formatted.c_str());
    lv_obj_set_style_text_color(ui_feedInPowerLabel, inverterData.feedInPower < 0 ? red : black, 0);
    lv_label_set_text_fmt(ui_socLabel, "%d%%", inverterData.soc);
    lv_label_set_text(ui_batteryPowerLabel, format(POWER, abs(inverterData.batteryPower)).formatted.c_str());
    lv_obj_set_style_text_color(ui_batteryPowerLabel, inverterData.batteryPower < 0 ? red : black, 0);
    lv_label_set_text_fmt(ui_batteryTemperatureLabel, "%d°C", inverterData.batteryTemperature);
    lv_label_set_text_fmt(ui_selfUsePercentLabel, "%d%%", selfUsePowerPercent);
    if (selfUsePowerPercent > 50)
    {
        lv_obj_set_style_text_color(ui_selfUsePercentLabel, green, 0);
    }
    else if (selfUsePowerPercent > 30)
    {
        lv_obj_set_style_text_color(ui_selfUsePercentLabel, orange, 0);
    }
    else
    {
        lv_obj_set_style_text_color(ui_selfUsePercentLabel, red, 0);
    }
    lv_label_set_text(ui_yieldTodayLabel, format(ENERGY, inverterData.pvToday * 1000.0, 1).value.c_str());
    lv_label_set_text(ui_yieldTodayUnitLabel, format(ENERGY, inverterData.pvToday * 1000.0, 1).unit.c_str());
    lv_label_set_text(ui_yieldTotalLabel, format(ENERGY, inverterData.pvTotal * 1000.0, 1, true).formatted.c_str());
    lv_label_set_text(ui_gridSellTodayLabel, (format(ENERGY, inverterData.gridSellToday * 1000.0, 1).value).c_str());
    lv_label_set_text(ui_gridSellTodayUnitLabel, format(ENERGY, inverterData.gridSellToday * 1000.0, 1).unit.c_str());
    lv_label_set_text(ui_gridBuyTodayLabel, (format(ENERGY, inverterData.gridBuyToday * 1000.0, 1).value).c_str());
    lv_obj_set_style_text_color(ui_gridBuyTodayLabel, red, 0);
    lv_label_set_text(ui_gridBuyTodayUnitLabel, format(ENERGY, inverterData.gridBuyToday * 1000.0, 1).unit.c_str());
    lv_label_set_text(ui_batteryChargedTodayLabel, (format(ENERGY, inverterData.batteryChargedToday * 1000.0, 1).value).c_str());
    lv_label_set_text(ui_batteryChargedTodayUnitLabel, (format(ENERGY, inverterData.batteryChargedToday * 1000.0, 1).unit).c_str());
    lv_label_set_text(ui_batteryDischargedTodayLabel, (format(ENERGY, inverterData.batteryDischargedToday * 1000.0, 1).value).c_str());
    lv_obj_set_style_text_color(ui_batteryDischargedTodayLabel, red, 0);
    lv_label_set_text(ui_batteryDischargedTodayUnitLabel, (format(ENERGY, inverterData.batteryDischargedToday * 1000.0, 1).unit).c_str());
    lv_label_set_text(ui_loadTodayLabel, format(ENERGY, inverterData.loadToday * 1000.0, 1).value.c_str());
    lv_label_set_text(ui_loadTodayUnitLabel, format(ENERGY, inverterData.loadToday * 1000.0, 1).unit.c_str());

    lv_label_set_text_fmt(ui_selfUseTodayLabel, "%d%%", selfUseEnergyTodayPercent);
    if (selfUseEnergyTodayPercent > 50)
    {
        lv_obj_set_style_text_color(ui_selfUseTodayLabel, green, 0);
    }
    else if (selfUseEnergyTodayPercent > 30)
    {
        lv_obj_set_style_text_color(ui_selfUseTodayLabel, orange, 0);
    }
    else
    {
        lv_obj_set_style_text_color(ui_selfUseTodayLabel, red, 0);
    }

    if(shellyResult.pairedCount > 0) {
        lv_obj_clear_flag(ui_shellyContainer, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui_shellyContainer, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(ui_shellyPowerLabel1, format(POWER, shellyResult.totalPower).formatted.c_str());
    lv_label_set_text_fmt(ui_shellyCountLabel, "%d / %d", shellyResult.activeCount, shellyResult.pairedCount);

    updateChart();

    lv_obj_set_style_text_color(ui_statusLabel, lv_palette_main(LV_PALETTE_DEEP_ORANGE), 0);
    if (discoveryResult.result)
    {
        if (inverterData.status != SOLAX_DONGLE_STATUS_OK)
        {
            lv_label_set_text_fmt(ui_statusLabel, dongleAPI.getStatusText(inverterData.status).c_str());
        }
        else
        {
            lv_obj_set_style_text_color(ui_statusLabel, lv_palette_main(LV_PALETTE_GREY), 0);
            lv_label_set_text(ui_statusLabel, discoveryResult.sn.c_str());
        }
    }
    else
    {
        lv_label_set_text(ui_statusLabel, "Disconnected");
    }

    updateFlowAnimations();

    backlightResolver.resolve(inverterData);
}

bool dashboardShown = false;

void timerCB(struct _lv_timer_t *timer)
{
    if (!dashboardShown && inverterData.status == SOLAX_DONGLE_STATUS_OK)
    {
        lv_scr_load_anim(ui_Dashboard, LV_SCR_LOAD_ANIM_FADE_IN, 500, 0, true);
        dashboardShown = true;
    }

    if (dashboardShown)
    {
        updateDashboardUI();
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

    lvgl_port_lock(-1);
    lvgl_port_init(panel->getLcd(), panel->getTouch());
    ui_init();
    lv_scr_load_anim(ui_Splash, LV_SCR_LOAD_ANIM_FADE_IN, 500, 0, true);
    lvgl_port_unlock();

    lv_timer_t *timer = lv_timer_create(timerCB, 3000, NULL);
    lv_log_register_print_cb([](const char *txt)
                             { log_i("%s\n", txt); });
}

bool canActivateShelly() {
    int powerTreshold = 500;

    if(inverterData.status != SOLAX_DONGLE_STATUS_OK) {
        return false;
    }
    return true;
    if(inverterData.soc >= 99) {
        log_d("Battery full");
        return true;
    }

    if(inverterData.soc > 90 && inverterData.batteryPower > powerTreshold) {
        log_d("Battery almost full and charging");
        return true;
    }

    if(inverterData.feedInPower > powerTreshold) {
        log_d("Feeding in power");
        return true;
    }

    return false;
}

void discoverDongle()
{
    static long lastAttempt = 0;
    
    if (lastAttempt == 0 ||millis() - lastAttempt > 20000)
    {
        discoveryResult = dongleDiscovery.discoverDongle();
        lastAttempt = millis();
    }
}

void reloadData()
{
    static long lastAttempt = 0;
    if (lastAttempt == 0 ||millis() - lastAttempt > 3000)
    {
        log_d("Reloading data");
        if (discoveryResult.result)
        {
            //inverterData = createRandomMockData();

            int MAX_RETRIES = 5;
            for(int i = 0; i < MAX_RETRIES; i++) {
                inverterData = dongleAPI.loadData(discoveryResult.sn);
                if(inverterData.status == SOLAX_DONGLE_STATUS_OK) {
                    break;
                }
                delay(100);
            }
            if (inverterData.status == SOLAX_DONGLE_STATUS_OK)
            {
                solarChartDataProvider->addSample(millis(), inverterData.pv1Power + inverterData.pv2Power, inverterData.loadPower, inverterData.soc);
            }
        }
        lastAttempt = millis();
    }
}

void checkNewShellyPairings()
{
    static long lastAttempt = 0;
    if (lastAttempt == 0 ||millis() - lastAttempt > 30000)
    {
        log_d("Checking for new Shelly pairings");
        String shellyAPSSID = shellyAPI.findShellyAP();
        if (shellyAPSSID.length() > 0)
        {
            shellyAPI.pairShelly(shellyAPSSID, SOFT_AP_SSID_PREFIX + getESPIdHex(), getESPIdHex());
        }
        lastAttempt = millis();
    }
}

void discoverShellyPairings() {
    static long lastAttempt = 0;
    if (lastAttempt == 0 ||millis() - lastAttempt > 1000)
    {
        log_d("Discovering Shelly on local network");
        shellyAPI.discoverParings();
        lastAttempt = millis();
    }
}

void checkSoftAP() {
    static long lastAttempt = 0;
    if (lastAttempt == 0 ||millis() - lastAttempt > 10000)
    {
        if (WiFi.softAPSSID().isEmpty())
        {
            log_d("Starting SoftAP");
            WiFi.softAP(SOFT_AP_SSID_PREFIX + getESPIdHex(), getESPIdHex(), 1, 1, MAX_SHELLY_PAIRS);
            shellyAPI.initMDNS(SOFT_AP_SSID_PREFIX + getESPIdHex());
        }
        lastAttempt = millis();
    }
}

bool reloadShellyState() {
    bool result = false;
    static long lastAttempt = 0;
    if (lastAttempt == 0 ||millis() - lastAttempt > 3000)
    {
        log_d("Reloading Shelly state");
        shellyResult = shellyAPI.getState();
        lastAttempt = millis();
        result = true;
    }
    return result;
}

void checkShellyActivation() {
    static long lastAttempt = 0;
    if (lastAttempt == 0 || millis() - lastAttempt > 10000)
    {
        if(canActivateShelly()) {
            log_d("Activating Shelly");
            shellyAPI.activateOneShelly(20); //(5 * 60);

        }
        lastAttempt = millis();
    }
}

void loop()
{
    discoverDongle();
    checkNewShellyPairings();
    discoverShellyPairings();
    checkSoftAP();
    if(reloadShellyState()) {
        checkShellyActivation();
    }
    
    reloadData();
    delay(100); //yield other tasks
}
