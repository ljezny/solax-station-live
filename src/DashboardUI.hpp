#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "ui/ui.h"
#include "utils/UIBallAnimator.hpp"
#include "Inverters/InverterResult.hpp"
#include "Shelly/Shelly.hpp"
#include "utils/UITextChangeAnimator.hpp"
#include "utils/UIBackgroundAnimatior.hpp"

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

class DashboardUI
{
public:
    const int UI_REFRESH_PERIOD_MS = 5000;
    void show()
    {
        lv_scr_load_anim(ui_Dashboard, LV_SCR_LOAD_ANIM_FADE_IN, 500, 0, true);
        lv_obj_add_event_cb(ui_Chart1, draw_event_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);
    }
    
    int getSelfUsePowerPercent(InverterData_t& inverterData)
    {
        return constrain(inverterData.loadPower > 0 ? (100 * (inverterData.loadPower + inverterData.feedInPower)) / inverterData.loadPower : 0, 0, 100);
    }

    void update(InverterData_t& inverterData, InverterData_t& previousInverterData, ShellyResult_t& shellyResult, ShellyResult_t& previousShellyResult, SolarChartDataProvider& solarChartDataProvider, int wifiSignalPercent)
    {
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
        lv_color_t white = lv_color_make(255, 255, 255);
        lv_color_t red = lv_color_hex(0xAB2328);
        lv_color_t orange = lv_color_hex(0xFFD400);
        lv_color_t green = lv_color_hex(0x03AD36);
        

        //lv_label_set_text(ui_pvLabel, format(POWER, inverterData.pv1Power + inverterData.pv2Power).value.c_str());
        pvPowerTextAnimator.animate(ui_pvLabel, previousInverterData.pv1Power + previousInverterData.pv2Power, inverterData.pv1Power + inverterData.pv2Power);
        lv_label_set_text(ui_pvUnitLabel, format(POWER, inverterData.pv1Power + inverterData.pv2Power).unit.c_str());
        //lv_label_set_text(ui_pv1Label, format(POWER, inverterData.pv1Power, 1.0f, true).value.c_str());
        pv1PowerTextAnimator.animate(ui_pv1Label, previousInverterData.pv1Power, inverterData.pv1Power);
        lv_label_set_text(ui_pv1UnitLabel, format(POWER, inverterData.pv1Power, 1.0f, true).unit.c_str());
        //lv_label_set_text(ui_pv2Label, format(POWER, inverterData.pv2Power, 1.0f, true).value.c_str());
        pv2PowerTextAnimator.animate(ui_pv2Label, previousInverterData.pv2Power, inverterData.pv2Power);
        lv_label_set_text(ui_pv2UnitLabel, format(POWER, inverterData.pv2Power, 1.0f, true).unit.c_str());
        if (inverterData.pv1Power == 0 || inverterData.pv2Power == 0)
        { // hide
            lv_obj_add_flag(ui_pvStringsContainer, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_clear_flag(ui_pvStringsContainer, LV_OBJ_FLAG_HIDDEN);
        }   

        lv_label_set_text_fmt(ui_inverterTemperatureLabel, "%d", inverterData.inverterTemperature);
        if(inverterData.inverterTemperature > 50)
        {
            lv_obj_set_style_bg_color(ui_inverterTemperatureContainer, red, 0);
            lv_obj_set_style_text_color(ui_inverterTemperatureLabel, white, 0);
            lv_obj_set_style_text_color(ui_inverterTemperatureUnitLabel, white, 0);
        } else if (inverterData.inverterTemperature > 40)
        {
            lv_obj_set_style_bg_color(ui_inverterTemperatureContainer, orange, 0);
            lv_obj_set_style_text_color(ui_inverterTemperatureLabel, black, 0);
            lv_obj_set_style_text_color(ui_inverterTemperatureUnitLabel, black, 0);
        } else
        {
            lv_obj_set_style_bg_color(ui_inverterTemperatureContainer, green, 0);
            lv_obj_set_style_text_color(ui_inverterTemperatureLabel, white, 0);
            lv_obj_set_style_text_color(ui_inverterTemperatureUnitLabel, white, 0);
        }

        //lv_label_set_text(ui_inverterPowerLabel, format(POWER, inverterData.inverterPower).value.c_str());
        inverterPowerTextAnimator.animate(ui_inverterPowerLabel, previousInverterData.inverterPower, inverterData.inverterPower);
        //lv_obj_set_style_bg_color(ui_pvContainer, (inverterData.pv1Power + inverterData.pv2Power) > 0 ? lv_color_hex(_ui_theme_color_pvColor[0]) :  lv_color_white(), 0);
        pvBackgroundAnimator.animate(ui_pvContainer, (previousInverterData.pv1Power + previousInverterData.pv2Power) > 0, (inverterData.pv1Power + inverterData.pv2Power) > 0);
        lv_label_set_text(ui_inverterPowerUnitLabel, format(POWER, inverterData.inverterPower).unit.c_str());
        //lv_label_set_text(ui_inverterPowerL1Label, format(POWER, inverterData.L1Power).value.c_str());
        inverterPowerL1TextAnimator.animate(ui_inverterPowerL1Label, previousInverterData.L1Power, inverterData.L1Power);
        lv_label_set_text(ui_inverterPowerL1UnitLabel, format(POWER, inverterData.L1Power).unit.c_str());
        lv_obj_set_style_text_color(ui_inverterPowerL1Label, l1PercentUsage > 50 ? red : black, 0);
        lv_obj_set_style_text_color(ui_inverterPowerL1UnitLabel, l1PercentUsage > 50 ? red : black, 0);
        //lv_label_set_text(ui_inverterPowerL2Label, format(POWER, inverterData.L2Power).value.c_str());
        inverterPowerL2TextAnimator.animate(ui_inverterPowerL2Label, previousInverterData.L2Power, inverterData.L2Power);
        lv_label_set_text(ui_inverterPowerL2UnitLabel, format(POWER, inverterData.L2Power).unit.c_str());        
        lv_obj_set_style_text_color(ui_inverterPowerL2Label, l2PercentUsage > 50 ? red : black, 0);
        lv_obj_set_style_text_color(ui_inverterPowerL2UnitLabel, l2PercentUsage > 50 ? red : black, 0);
        //lv_label_set_text(ui_inverterPowerL3Label, format(POWER, inverterData.L3Power).value.c_str());
        inverterPowerL3TextAnimator.animate(ui_inverterPowerL3Label, previousInverterData.L3Power, inverterData.L3Power);
        lv_label_set_text(ui_inverterPowerL3UnitLabel, format(POWER, inverterData.L3Power).unit.c_str());
        lv_obj_set_style_text_color(ui_inverterPowerL3Label, l3PercentUsage > 50 ? red : black, 0);
        lv_obj_set_style_text_color(ui_inverterPowerL3UnitLabel, l3PercentUsage > 50 ? red : black, 0);
        loadPowerTextAnimator.animate(ui_loadPowerLabel, previousInverterData.loadPower, inverterData.loadPower);
        lv_label_set_text(ui_loadPowerUnitLabel, format(POWER, inverterData.loadPower).unit.c_str());
        feedInPowerTextAnimator.animate(ui_feedInPowerLabel, abs(previousInverterData.feedInPower), abs(inverterData.feedInPower));
        lv_label_set_text(ui_feedInPowerUnitLabel, format(POWER, abs(inverterData.feedInPower)).unit.c_str());
        //lv_obj_set_style_bg_color(ui_gridContainer, (inverterData.feedInPower) < 0 ? lv_color_hex(_ui_theme_color_gridColor[0]) :  lv_color_white(), 0);
        gridBackgroundAnimator.animate(ui_gridContainer, (previousInverterData.feedInPower) < 0, (inverterData.feedInPower) < 0);
        lv_obj_set_style_text_color(ui_feedInPowerLabel, inverterData.feedInPower < 0 ? white : black, 0);
        lv_obj_set_style_text_color(ui_feedInPowerUnitLabel, inverterData.feedInPower < 0 ? white : black, 0);
        //lv_label_set_text_fmt(ui_socLabel, "%d", inverterData.soc);
        batteryPercentTextAnimator.animate(ui_socLabel, previousInverterData.soc, inverterData.soc);
        //lv_label_set_text(ui_batteryPowerLabel, format(POWER, abs(inverterData.batteryPower)).value.c_str());
        batteryPowerTextAnimator.animate(ui_batteryPowerLabel, abs(previousInverterData.batteryPower), abs(inverterData.batteryPower));
        batteryBackgroundAnimator.animate(ui_batteryContainer, (previousInverterData.batteryPower) < 0, (inverterData.batteryPower) < 0);
        //lv_obj_set_style_bg_color(ui_batteryContainer, (inverterData.batteryPower) < 0 ? lv_color_hex(_ui_theme_color_batteryColor[0]) :  lv_color_white(), 0);

        lv_label_set_text(ui_batteryPowerUnitLabel, format(POWER, abs(inverterData.batteryPower)).unit.c_str());
        //lv_obj_set_style_text_color(ui_batteryPowerLabel, inverterData.batteryPower < 0 ? red : black, 0);
        //lv_obj_set_style_text_color(ui_batteryPowerUnitLabel, inverterData.batteryPower < 0 ? red : black, 0);
        lv_label_set_text_fmt(ui_batteryTemperatureLabel, "%d", inverterData.batteryTemperature);
        
        if(inverterData.batteryTemperature > 40)
        {
            lv_obj_set_style_bg_color(ui_batteryTemperatureContainer, red, 0);
            lv_obj_set_style_text_color(ui_batteryTemperatureLabel, white, 0);
            lv_obj_set_style_text_color(ui_batteryTemperatureUnitLabel, white, 0);
        } else if (inverterData.batteryTemperature > 30)
        {
            lv_obj_set_style_bg_color(ui_batteryTemperatureContainer, orange, 0);
            lv_obj_set_style_text_color(ui_batteryTemperatureLabel, black, 0);
            lv_obj_set_style_text_color(ui_batteryTemperatureUnitLabel, black, 0);
        } else
        {
            lv_obj_set_style_bg_color(ui_batteryTemperatureContainer, green, 0);
            lv_obj_set_style_text_color(ui_batteryTemperatureLabel, white, 0);
            lv_obj_set_style_text_color(ui_batteryTemperatureUnitLabel, white, 0);
        }
        
        //lv_label_set_text(ui_selfUsePercentLabel, String(selfUsePowerPercent).c_str());
        selfUsePercentTextAnimator.animate(ui_selfUsePercentLabel, getSelfUsePowerPercent(previousInverterData), getSelfUsePowerPercent(inverterData));
        if (getSelfUsePowerPercent(inverterData) > 50)
        {
            lv_obj_set_style_text_color(ui_selfUsePercentLabel, green, 0);
            lv_obj_set_style_text_color(ui_selfUsePercentUnitLabel, green, 0);
        }
        else if (getSelfUsePowerPercent(inverterData) > 30)
        {
            lv_obj_set_style_text_color(ui_selfUsePercentLabel, orange, 0);
            lv_obj_set_style_text_color(ui_selfUsePercentUnitLabel, orange, 0);
        }
        else
        {
            lv_obj_set_style_text_color(ui_selfUsePercentLabel, red, 0);
            lv_obj_set_style_text_color(ui_selfUsePercentUnitLabel, red, 0);
        }
        lv_label_set_text(ui_yieldTodayLabel, format(ENERGY, inverterData.pvToday * 1000.0, 1).value.c_str());
        lv_label_set_text(ui_yieldTodayUnitLabel, format(ENERGY, inverterData.pvToday * 1000.0, 1).unit.c_str());
        lv_label_set_text(ui_yieldTotalLabel, format(ENERGY, inverterData.pvTotal * 1000.0, 1, true).value.c_str());
        lv_label_set_text(ui_yieldTotalUnitLabel, format(ENERGY, inverterData.pvTotal * 1000.0, 1, true).unit.c_str());
        lv_label_set_text(ui_gridSellTodayLabel, (format(ENERGY, inverterData.gridSellToday * 1000.0, 1).value).c_str());
        lv_label_set_text(ui_gridSellTodayUnitLabel, format(ENERGY, inverterData.gridSellToday * 1000.0, 1).unit.c_str());
        lv_label_set_text(ui_gridBuyTodayLabel, (format(ENERGY, inverterData.gridBuyToday * 1000.0, 1).value).c_str());
        lv_obj_set_style_text_color(ui_gridBuyTodayLabel, red, 0);
        lv_obj_set_style_text_color(ui_gridBuyTodayUnitLabel, red, 0);
        lv_label_set_text(ui_gridBuyTodayUnitLabel, format(ENERGY, inverterData.gridBuyToday * 1000.0, 1).unit.c_str());
        lv_label_set_text(ui_batteryChargedTodayLabel, (format(ENERGY, inverterData.batteryChargedToday * 1000.0, 1).value).c_str());
        lv_label_set_text(ui_batteryChargedTodayUnitLabel, (format(ENERGY, inverterData.batteryChargedToday * 1000.0, 1).unit).c_str());
        lv_label_set_text(ui_batteryDischargedTodayLabel, (format(ENERGY, inverterData.batteryDischargedToday * 1000.0, 1).value).c_str());
        lv_obj_set_style_text_color(ui_batteryDischargedTodayLabel, red, 0);
        lv_obj_set_style_text_color(ui_batteryDischargedTodayUnitLabel, red, 0);
        lv_label_set_text(ui_batteryDischargedTodayUnitLabel, (format(ENERGY, inverterData.batteryDischargedToday * 1000.0, 1).unit).c_str());
        lv_label_set_text(ui_loadTodayLabel, format(ENERGY, inverterData.loadToday * 1000.0, 1).value.c_str());
        lv_label_set_text(ui_loadTodayUnitLabel, format(ENERGY, inverterData.loadToday * 1000.0, 1).unit.c_str());

        lv_label_set_text_fmt(ui_selfUseTodayLabel, "%d", selfUseEnergyTodayPercent);
        if (selfUseEnergyTodayPercent > 50)
        {
            lv_obj_set_style_text_color(ui_selfUseTodayLabel, green, 0);
            lv_obj_set_style_text_color(ui_selfUseTodayUnitLabel, green, 0);
        }
        else if (selfUseEnergyTodayPercent > 30)
        {
            lv_obj_set_style_text_color(ui_selfUseTodayLabel, orange, 0);
            lv_obj_set_style_text_color(ui_selfUseTodayUnitLabel, orange, 0);
        }
        else
        {
            lv_obj_set_style_text_color(ui_selfUseTodayLabel, red, 0);
            lv_obj_set_style_text_color(ui_selfUseTodayUnitLabel, red, 0);
        }

        if (shellyResult.pairedCount > 0)
        {
            lv_obj_clear_flag(ui_shellyContainer, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(ui_shellyContainer, LV_OBJ_FLAG_HIDDEN);
        }
        //lv_label_set_text(ui_shellyPowerLabel, format(POWER, shellyResult.totalPower).value.c_str());
        shellyPowerTextAnimator.animate(ui_shellyPowerLabel, previousShellyResult.totalPower, shellyResult.totalPower);
        lv_label_set_text(ui_shellyPowerUnitLabel, format(POWER, shellyResult.totalPower).unit.c_str());
        lv_label_set_text_fmt(ui_shellyCountLabel, "%d / %d", shellyResult.activeCount, shellyResult.pairedCount);

        updateChart(solarChartDataProvider);

        lv_obj_set_style_text_color(ui_statusLabel, lv_palette_main(LV_PALETTE_DEEP_ORANGE), 0);
        
        switch(inverterData.status)
        {
            case DONGLE_STATUS_OK:
                lv_obj_set_style_text_color(ui_statusLabel, lv_palette_main(LV_PALETTE_GREY), 0);
                lv_label_set_text_fmt(ui_statusLabel, "%s %d%%", inverterData.sn.c_str(), wifiSignalPercent);
                break;
            case DONGLE_STATUS_CONNECTION_ERROR:
                lv_label_set_text(ui_statusLabel, "Connection error");
                break;
            case DONGLE_STATUS_HTTP_ERROR:
                lv_label_set_text(ui_statusLabel, "HTTP error");
                break;
            case DONGLE_STATUS_JSON_ERROR:
                lv_label_set_text(ui_statusLabel, "JSON error");
                break;
            case DONGLE_STATUS_WIFI_DISCONNECTED:
                lv_label_set_text(ui_statusLabel, "WiFi disconnected");
                break;
            default:
                lv_label_set_text(ui_statusLabel, "Unknown error");
                break;
        }
        
        updateFlowAnimations(inverterData, shellyResult);
    }
private:
    int const UI_TEXT_CHANGE_ANIMATION_DURATION = UI_REFRESH_PERIOD_MS;
    int const UI_BACKGROUND_ANIMATION_DURATION = UI_REFRESH_PERIOD_MS / 3;
    UITextChangeAnimator loadPowerTextAnimator = UITextChangeAnimator(POWER, UI_TEXT_CHANGE_ANIMATION_DURATION);
    UITextChangeAnimator feedInPowerTextAnimator = UITextChangeAnimator(POWER, UI_TEXT_CHANGE_ANIMATION_DURATION);
    UITextChangeAnimator batteryPowerTextAnimator = UITextChangeAnimator(POWER, UI_TEXT_CHANGE_ANIMATION_DURATION);
    UITextChangeAnimator inverterPowerTextAnimator = UITextChangeAnimator(POWER, UI_TEXT_CHANGE_ANIMATION_DURATION);
    UITextChangeAnimator inverterPowerL1TextAnimator = UITextChangeAnimator(POWER, UI_TEXT_CHANGE_ANIMATION_DURATION);
    UITextChangeAnimator inverterPowerL2TextAnimator = UITextChangeAnimator(POWER, UI_TEXT_CHANGE_ANIMATION_DURATION);
    UITextChangeAnimator inverterPowerL3TextAnimator = UITextChangeAnimator(POWER, UI_TEXT_CHANGE_ANIMATION_DURATION);
    UITextChangeAnimator shellyPowerTextAnimator = UITextChangeAnimator(POWER, UI_TEXT_CHANGE_ANIMATION_DURATION);
    UITextChangeAnimator pvPowerTextAnimator = UITextChangeAnimator(POWER, UI_TEXT_CHANGE_ANIMATION_DURATION);
    UITextChangeAnimator pv1PowerTextAnimator = UITextChangeAnimator(POWER, UI_TEXT_CHANGE_ANIMATION_DURATION);
    UITextChangeAnimator pv2PowerTextAnimator = UITextChangeAnimator(POWER, UI_TEXT_CHANGE_ANIMATION_DURATION);
    UITextChangeAnimator batteryPercentTextAnimator = UITextChangeAnimator(PERCENT, UI_TEXT_CHANGE_ANIMATION_DURATION);
    UITextChangeAnimator selfUsePercentTextAnimator = UITextChangeAnimator(PERCENT, UI_TEXT_CHANGE_ANIMATION_DURATION);
    UIBackgroundAnimator pvBackgroundAnimator = UIBackgroundAnimator(UI_BACKGROUND_ANIMATION_DURATION, lv_color_hex(_ui_theme_color_pvColor[0]));
    UIBackgroundAnimator batteryBackgroundAnimator = UIBackgroundAnimator(UI_BACKGROUND_ANIMATION_DURATION, lv_color_hex(_ui_theme_color_batteryColor[0]));
    UIBackgroundAnimator gridBackgroundAnimator = UIBackgroundAnimator(UI_BACKGROUND_ANIMATION_DURATION, lv_color_hex(_ui_theme_color_gridColor[0]));

    void updateChart(SolarChartDataProvider& solarChartDataProvider)
    {
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
            SolarChartDataItem_t item = solarChartDataProvider.getData()[CHART_SAMPLES_PER_DAY - i - 1];

            lv_chart_set_next_value(ui_Chart1, pvPowerSeries, item.pvPower);
            lv_chart_set_next_value(ui_Chart1, acPowerSeries, item.loadPower);
            lv_chart_set_next_value(ui_Chart1, socSeries, item.soc);

            maxPower = max(maxPower, max(item.pvPower, item.loadPower));
        }
        lv_chart_set_range(ui_Chart1, LV_CHART_AXIS_SECONDARY_Y, 0, (lv_coord_t)maxPower);
    }

    void updateFlowAnimations(InverterData_t inverterData, ShellyResult_t shellyResult)
    {
        static UIBallAnimator *pvAnimator = NULL;
        static UIBallAnimator *batteryAnimator = NULL;
        static UIBallAnimator *gridAnimator = NULL;
        static UIBallAnimator *loadAnimator = NULL;
        static UIBallAnimator *shellyAnimator = NULL;

        int duration = UI_REFRESH_PERIOD_MS / 3;
        int offsetY = 15;
        int offsetX = 30;
        if (pvAnimator != NULL)
        {
            delete pvAnimator;
            pvAnimator = NULL;
        }
        if ((inverterData.pv1Power + inverterData.pv2Power) > 0)
        {
            pvAnimator = new UIBallAnimator(ui_LeftContainer, _ui_theme_color_pvColor, ((inverterData.pv1Power + inverterData.pv2Power) / 1000) + 1);
            pvAnimator->run(ui_pvContainer, ui_inverterContainer, duration, 0, 0, -offsetX, -offsetY);
        }

        if (batteryAnimator != NULL)
        {
            delete batteryAnimator;
            batteryAnimator = NULL;
        }
        if (inverterData.batteryPower > 0)
        {
            batteryAnimator = new UIBallAnimator(ui_LeftContainer, _ui_theme_color_batteryColor, (inverterData.batteryPower / 1000) + 1);
            batteryAnimator->run(ui_inverterContainer, ui_batteryContainer, duration, duration, 1, offsetX, -offsetY);
        }
        else if (inverterData.batteryPower < 0)
        {
            batteryAnimator = new UIBallAnimator(ui_LeftContainer, _ui_theme_color_batteryColor, (abs(inverterData.batteryPower) / 1000) + 1);
            batteryAnimator->run(ui_batteryContainer, ui_inverterContainer, duration, 0, 0, offsetX, -offsetY);
        }

        if (gridAnimator != NULL)
        {
            delete gridAnimator;
            gridAnimator = NULL;
        }

        if (inverterData.feedInPower > 0)
        {
            gridAnimator = new UIBallAnimator(ui_LeftContainer, _ui_theme_color_gridColor, (inverterData.feedInPower / 1000) + 1);
            gridAnimator->run(ui_inverterContainer, ui_gridContainer, duration, duration, 1, offsetX, offsetY);
        }
        else if (inverterData.feedInPower < 0)
        {
            gridAnimator = new UIBallAnimator(ui_LeftContainer, _ui_theme_color_gridColor, (abs(inverterData.feedInPower) / 1000) + 1);
            gridAnimator->run(ui_gridContainer, ui_inverterContainer, duration, 0, 0, offsetX, offsetY);
        }

        if (loadAnimator != NULL)
        {
            delete loadAnimator;
            loadAnimator = NULL;
        }
        if (inverterData.loadPower > 0)
        {
            loadAnimator = new UIBallAnimator(ui_LeftContainer, _ui_theme_color_loadColor, (inverterData.loadPower / 1000) + 1);
            loadAnimator->run(ui_inverterContainer, ui_loadContainer, duration, duration, 1, -offsetX, offsetY);
        }
        // if (shellyAnimator != NULL)
        // {
        //     delete shellyAnimator;
        //     shellyAnimator = NULL;
        // }
        // if (shellyResult.totalPower > 0)
        // { // TODO: check if shelly is on
        //     shellyAnimator = new UIBallAnimator(ui_LeftContainer, _ui_theme_color_pvColor, (shellyResult.totalPower / 1000) + 1);
        //     shellyAnimator->run(ui_loadContainer, ui_shellyContainer, duration, duration, 1, 0, 0);
        // }
    }
};