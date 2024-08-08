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
#include <mat.h>

#define WAIT 2000
SET_LOOP_TASK_STACK_SIZE(32 * 1024);

SolaxDongleAPI dongleAPI;
SolaxDongleDiscovery dongleDiscovery;

SolaxDongleInverterData_t inverterData;
SolaxDongleDiscoveryResult_t discoveryResult;
ESP_Panel *panel = new ESP_Panel();
SolarChartDataProvider *solarChartDataProvider = new SolarChartDataProvider();

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
    inverterData.yieldToday = random(0, 1000);
    inverterData.yieldTotal = random(0, 1000);
    inverterData.batteryChargedToday = random(0, 1000);
    inverterData.batteryDischargedToday = random(0, 1000);
    inverterData.gridBuyToday = random(0, 1000);
    inverterData.gridSellToday = random(0, 1000);
    inverterData.gridBuyTotal = random(0, 1000);
    inverterData.gridSellTotal = random(0, 1000);
    inverterData.batteryChargedTotal = random(0, 1000);
    inverterData.batteryDischargedTotal = random(0, 1000);
    return inverterData;
}

static void draw_event_cb(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target(e);
    /*Add the faded area before the lines are drawn*/
    lv_obj_draw_part_dsc_t * dsc = lv_event_get_draw_part_dsc(e);
    if(dsc->part == LV_PART_ITEMS) {
        if(!dsc->p1 || !dsc->p2) return;
        
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
        draw_rect_dsc.bg_opa = LV_OPA_10;
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
    } else if (dsc->part == LV_PART_TICKS) {
        if(dsc->id == LV_CHART_AXIS_PRIMARY_Y) {
            lv_snprintf (dsc->text, dsc->text_length, "%d%%", dsc->value);
        } else if(dsc->id == LV_CHART_AXIS_SECONDARY_Y) {
            lv_snprintf (dsc->text, dsc->text_length, "%d\nkW", dsc->value / 1000);
        } else if (dsc->id == LV_CHART_AXIS_PRIMARY_X) {
            lv_snprintf (dsc->text, dsc->text_length, "%dh", -24 + 6 * dsc->value);
        }
    }
}

bool setupChart = false;
void updateChart()
{
    if(!setupChart) {
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
    for(i = 0; i < CHART_SAMPLES_PER_DAY; i++) {
        SolarChartDataItem_t item = solarChartDataProvider->getData()[CHART_SAMPLES_PER_DAY - i - 1];
        
        lv_chart_set_next_value(ui_Chart1, pvPowerSeries, item.pvPower);
        lv_chart_set_next_value(ui_Chart1, acPowerSeries, item.loadPower);
        lv_chart_set_next_value(ui_Chart1, socSeries, item.soc);
        maxPower = max(maxPower, max(item.pvPower, item.loadPower));
    }
    lv_chart_set_range( ui_Chart1, LV_CHART_AXIS_SECONDARY_Y, 0, (lv_coord_t) maxPower);
}

UIBallAnimator *pvAnimator = NULL;
UIBallAnimator *batteryAnimator = NULL;
UIBallAnimator *gridAnimator = NULL;
UIBallAnimator *loadAnimator = NULL;

void updateFlowAnimations() {
    int duration = 1400;
    int offsetY = 15;
    if(pvAnimator != NULL) {
        delete pvAnimator;
        pvAnimator = NULL;
    }
    if ((inverterData.pv1Power + inverterData.pv2Power) > 0)
    {
        pvAnimator = new UIBallAnimator(ui_LeftContainer,  _ui_theme_color_pvColor);
        pvAnimator->run(ui_pvContainer, ui_inverterContainer, duration, 0, 0, -offsetY);
    }

    if(batteryAnimator != NULL) {
        delete batteryAnimator;
        batteryAnimator = NULL;
    }
    if (inverterData.batteryPower > 0)
    {
        batteryAnimator = new UIBallAnimator(ui_LeftContainer, _ui_theme_color_batteryColor);
        batteryAnimator->run(ui_inverterContainer, ui_batteryContainer, duration, duration, 1, -offsetY);
    } else if (inverterData.batteryPower < 0){
        batteryAnimator = new UIBallAnimator(ui_LeftContainer, _ui_theme_color_batteryColor);
        batteryAnimator->run(ui_batteryContainer, ui_inverterContainer, duration, 0, 0, -offsetY);
    }

    if(gridAnimator != NULL) {
        delete gridAnimator;
        gridAnimator = NULL;
    }

    if (inverterData.feedInPower > 0)
    {
        gridAnimator = new UIBallAnimator(ui_LeftContainer, _ui_theme_color_gridColor);
        gridAnimator->run(ui_inverterContainer, ui_gridContainer, duration, duration, 1, offsetY);
    } else if (inverterData.feedInPower < 0){
        gridAnimator = new UIBallAnimator(ui_LeftContainer, _ui_theme_color_gridColor);
        gridAnimator->run(ui_gridContainer, ui_inverterContainer, duration, 0, 0, offsetY);
    }
    
    if(loadAnimator != NULL) {
        delete loadAnimator;
        loadAnimator = NULL;
    }
    if (inverterData.loadPower > 0)
    {
        loadAnimator = new UIBallAnimator(ui_LeftContainer, _ui_theme_color_loadColor);
        loadAnimator->run(ui_inverterContainer, ui_loadContainer, duration, duration, 1, 20);
    }
}

void updateDashboardUI()
{
    int selfUsePercent = inverterData.loadPower > 0 ? (100 * (inverterData.loadPower + inverterData.feedInPower)) / inverterData.loadPower : 0;
    selfUsePercent = constrain(selfUsePercent, 0, 100);
    
    int inPower = inverterData.pv1Power + inverterData.pv2Power;
    if(inverterData.batteryPower < 0) {
        inPower += abs(inverterData.batteryPower);
    }
    if(inverterData.feedInPower < 0) {
        inPower += abs(inverterData.feedInPower);
    }

    int outPower = inverterData.loadPower;
    if(inverterData.batteryPower > 0) {
        outPower += inverterData.batteryPower;
    }
    if(inverterData.feedInPower > 0) {
        outPower += inverterData.feedInPower;
    }
    int totalPhasePower = inverterData.L1Power + inverterData.L2Power + inverterData.L3Power;
    int l1PercentUsage = inverterData.L1Power > 0 ? (100 * inverterData.L1Power) / totalPhasePower : 0;
    int l2PercentUsage = inverterData.L2Power > 0 ? (100 * inverterData.L2Power) / totalPhasePower : 0;
    int l3PercentUsage = inverterData.L3Power > 0 ? (100 * inverterData.L3Power) / totalPhasePower : 0;

    lv_label_set_text(ui_pvLabel, format(POWER, inverterData.pv1Power + inverterData.pv2Power).formatted.c_str());
    lv_label_set_text(ui_pv1Label, format(POWER, inverterData.pv1Power, 1.0f, true).formatted.c_str());
    lv_label_set_text(ui_pv2Label, format(POWER, inverterData.pv2Power, 1.0f, true).formatted.c_str());
    lv_label_set_text_fmt(ui_inverterTemperatureLabel, "%d°C", inverterData.inverterTemperature);
    lv_label_set_text(ui_inverterPowerLabel, format(POWER, inverterData.inverterPower).formatted.c_str());
    lv_label_set_text(ui_inverterPowerL1Label, format(POWER, inverterData.L1Power).formatted.c_str());
    lv_obj_set_style_text_color(ui_inverterPowerL1Label, lv_palette_main(l1PercentUsage > 50 ? LV_PALETTE_DEEP_ORANGE : LV_PALETTE_GREY), 0);
    lv_label_set_text(ui_inverterPowerL2Label, format(POWER, inverterData.L2Power).formatted.c_str());
    lv_obj_set_style_text_color(ui_inverterPowerL2Label, lv_palette_main(l2PercentUsage > 50 ? LV_PALETTE_DEEP_ORANGE : LV_PALETTE_GREY), 0);
    lv_label_set_text(ui_inverterPowerL3Label, format(POWER, inverterData.L3Power).formatted.c_str());
    lv_obj_set_style_text_color(ui_inverterPowerL3Label, lv_palette_main(l3PercentUsage > 50 ? LV_PALETTE_DEEP_ORANGE : LV_PALETTE_GREY), 0);
    lv_label_set_text(ui_loadPowerLabel, format(POWER, inverterData.loadPower).formatted.c_str());
    lv_label_set_text(ui_feedInPowerLabel, format(POWER, abs(inverterData.feedInPower)).formatted.c_str());
    lv_label_set_text_fmt(ui_socLabel, "%d%%", inverterData.soc);
    lv_label_set_text(ui_batteryPowerLabel, format(POWER, abs(inverterData.batteryPower)).formatted.c_str());
    lv_label_set_text_fmt(ui_batteryTemperatureLabel, "%d°C", inverterData.batteryTemperature);
    lv_label_set_text_fmt(ui_selfUsePercentLabel, "%d%%", selfUsePercent);
    lv_label_set_text(ui_yieldTodayLabel, format(ENERGY, inverterData.yieldToday * 1000.0, 1).formatted.c_str());
    lv_label_set_text(ui_loadTotalLabel, format(ENERGY, inverterData.loadTotal * 1000.0, 1).formatted.c_str());
    lv_label_set_text(ui_gridSellTodayLabel, ("+ " + format(ENERGY, inverterData.gridSellToday * 1000.0, 1).formatted).c_str());
    lv_label_set_text(ui_gridBuyTodayLabel, ("-" + format(ENERGY, inverterData.gridBuyToday * 1000.0, 1).formatted).c_str());
    lv_label_set_text(ui_batteryChargedTodayLabel, ("+" + format(ENERGY, inverterData.batteryChargedToday * 1000.0, 1).formatted).c_str());
    lv_label_set_text(ui_batteryDischargedTodayLabel, ("-" + format(ENERGY, inverterData.batteryDischargedToday * 1000.0, 1).formatted).c_str());
    lv_label_set_text(ui_yieldTotalLabel, format(ENERGY, inverterData.yieldTotal * 1000.0, 1).formatted.c_str());
    lv_label_set_text(ui_loadTotalLabel, format(ENERGY, inverterData.loadToday * 1000.0, 1).formatted.c_str());
    lv_label_set_text(ui_gridSellTotalLabel, ("+ " + format(ENERGY, inverterData.gridSellTotal * 1000.0, 1).formatted).c_str());
    lv_label_set_text(ui_gridBuyTotalLabel, ("- " + format(ENERGY, inverterData.gridBuyTotal * 1000.0, 1).formatted).c_str());
    lv_label_set_text(ui_batteryChargedTotalLabel, ("+ " + format(ENERGY, inverterData.batteryChargedTotal * 1000.0, 1).formatted).c_str());
    lv_label_set_text(ui_batteryDischargedTotalLabel, ("- " + format(ENERGY, inverterData.batteryDischargedTotal * 1000.0, 1).formatted).c_str());

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
}

bool uiInitialized = false;
bool dashboardShown = false;

void timerCB(struct _lv_timer_t *timer)
{
    if (!uiInitialized)
    {
        uiInitialized = true;
        ui_init();
    }
    if (!dashboardShown && inverterData.status == SOLAX_DONGLE_STATUS_OK)
    {
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

    lv_timer_t *timer = lv_timer_create(timerCB, 3000, NULL);
    lv_log_register_print_cb([](const char * txt) {
        log_i("%s\n", txt);
    });
}

void loop()
{
    //discoveryResult = dongleDiscovery.discoverDongle();
    //if (discoveryResult.result)
    { 
        inverterData = createRandomMockData();  
        //inverterData = dongleAPI.loadData(discoveryResult.sn);
        if (inverterData.status == SOLAX_DONGLE_STATUS_OK)
        {
           solarChartDataProvider->addSample(millis(), inverterData.pv1Power + inverterData.pv2Power, inverterData.loadPower, inverterData.soc);
        }
    }
    delay(WAIT);
}
