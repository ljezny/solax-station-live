#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "ui/ui.h"
#include "utils/UIBallAnimator.hpp"
#include "Inverters/InverterResult.hpp"
#include "Shelly/Shelly.hpp"
#include "utils/UITextChangeAnimator.hpp"
#include "utils/UIBackgroundAnimatior.hpp"
#include "utils/MedianPowerSampler.hpp"
#include "Spot/ElectricityPriceLoader.hpp"

lv_color_t red = lv_color_hex(0xAB2328);
lv_color_t orange = lv_color_hex(0xFFAA00);
lv_color_t green = lv_color_hex(0x03AD36);

static bool isDarkMode = false;

static lv_color_t getPriceLevelColor(PriceLevel_t level)
{
    switch (level)
    {
    case PRICE_LEVEL_CHEAP:
        return green;
    case PRICE_LEVEL_MEDIUM:
        return orange;
    case PRICE_LEVEL_EXPENSIVE:
        return red;
    case PRICE_LEVEL_NEGATIVE:
        return lv_color_hex(0x007BFF); // frozen blue
    default:
        return lv_color_hex(0x808080); // gray for unknown
    }
}

static void electricity_price_draw_event_cb(lv_event_t *e)
{
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    lv_obj_t *obj = lv_event_get_target(e);
    ElectricityPriceResult_t *electricityPriceResult = (ElectricityPriceResult_t *)lv_obj_get_user_data(obj);

    if (dsc->part == LV_PART_MAIN)
    {
        lv_coord_t pad_left = lv_obj_get_style_pad_left(obj, LV_PART_MAIN);
        lv_coord_t pad_right = lv_obj_get_style_pad_right(obj, LV_PART_MAIN);
        lv_coord_t pad_top = lv_obj_get_style_pad_top(obj, LV_PART_MAIN);
        lv_coord_t pad_bottom = lv_obj_get_style_pad_bottom(obj, LV_PART_MAIN);
        lv_coord_t w = (int32_t)lv_obj_get_content_width(obj);
        lv_coord_t h = (int32_t)lv_obj_get_content_height(obj);
        uint32_t segmentCount = QUARTERS_OF_DAY;
        uint32_t segmentGap = 0;
        int32_t segmentWidth = 4;
        int32_t offset_x = w - (segmentCount * segmentWidth + (segmentCount - 1) * segmentGap);

        float minPrice = electricityPriceResult->prices[0].electricityPrice;
        float maxPrice = electricityPriceResult->prices[0].electricityPrice;
        for (uint32_t i = 0; i < segmentCount; i++)
        {
            float price = electricityPriceResult->prices[i].electricityPrice;
            if (price < minPrice)
            {
                minPrice = price;
            }
            if (price > maxPrice)
            {
                maxPrice = price;
            }
        }
        minPrice = min(0.0f, minPrice);
        maxPrice = max(maxPrice, electricityPriceResult->scaleMaxValue);
        float priceRange = maxPrice - minPrice;

        // draw vertical line for current quarter
        time_t now = time(nullptr);
        struct tm *timeinfo = localtime(&now);
        int currentQuarter = (timeinfo->tm_hour * 60 + timeinfo->tm_min) / 15;
        lv_draw_rect_dsc_t current_quarter_dsc;
        lv_draw_rect_dsc_init(&current_quarter_dsc);
        current_quarter_dsc.bg_opa = LV_OPA_50;
        current_quarter_dsc.bg_color = isDarkMode ? lv_color_hex(0x555555) : lv_color_hex(0xAAAAAA);
        lv_area_t cq_a;
        cq_a.x1 = obj->coords.x1 + offset_x + currentQuarter * (segmentWidth + segmentGap) - segmentGap / 2;
        cq_a.x2 = cq_a.x1 + segmentWidth + segmentGap - 1;
        cq_a.y1 = obj->coords.y1 + pad_top;
        cq_a.y2 = obj->coords.y2 - pad_bottom;
        lv_draw_rect(dsc->draw_ctx, &current_quarter_dsc, &cq_a);

        // draw price segments
        for (uint32_t i = 0; i < segmentCount; i++)
        {
            float price = electricityPriceResult->prices[i].electricityPrice;
            lv_color_t color = getPriceLevelColor(electricityPriceResult->prices[i].priceLevel);

            // time_t now = time(nullptr);
            // struct tm *timeinfo = localtime(&now);
            // int currentQuarter = (timeinfo->tm_hour * 60 + timeinfo->tm_min) / 15;
            // if (i == currentQuarter)
            // {
            //     color = isDarkMode ? lv_color_white() : lv_color_black();
            // }

            lv_draw_rect_dsc_t draw_rect_dsc;
            lv_draw_rect_dsc_init(&draw_rect_dsc);
            draw_rect_dsc.bg_opa = LV_OPA_30;
            draw_rect_dsc.bg_color = color;
            draw_rect_dsc.border_opa = LV_OPA_80;
            draw_rect_dsc.border_color = color;
            draw_rect_dsc.border_width = 1;

            lv_area_t a;
            a.x1 = obj->coords.x1 + offset_x + i * (segmentWidth + segmentGap);
            a.x2 = a.x1 + segmentWidth - 1;
            a.y1 = obj->coords.y1 + pad_top + (priceRange - price + minPrice) * h / priceRange;
            a.y2 = obj->coords.y1 + pad_top + (priceRange + minPrice) * h / priceRange - 1;
            if (a.y1 > a.y2) // swap
            {
                lv_coord_t temp = a.y1;
                a.y1 = a.y2;
                a.y2 = temp;
            }
            lv_draw_rect(dsc->draw_ctx, &draw_rect_dsc, &a);
        }

        // draw x-axis line
        //  lv_draw_rect_dsc_t line_dsc;
        //  lv_draw_rect_dsc_init(&line_dsc);
        //  line_dsc.bg_opa = LV_OPA_COVER;
        //  line_dsc.bg_color = isDarkMode ? lv_color_hex(0x555555) : lv_color_hex(0xAAAAAA);
        //  lv_area_t a;
        //  a.x1 = obj->coords.x1 + pad_left;
        //  a.x2 = obj->coords.x2 - pad_right;
        //  a.y1 = obj->coords.y1 + pad_top + (priceRange + minPrice) * h / priceRange - 1;
        //  a.y2 = a.y1;
        // lv_draw_rect(dsc->draw_ctx, &line_dsc, &a);

        // show times for 6, 12, 18 hours
        lv_draw_label_dsc_t label_dsc;
        // use OpenSans small font
        label_dsc.font = &ui_font_OpenSansExtraSmall;
        lv_draw_label_dsc_init(&label_dsc);
        lv_obj_init_draw_label_dsc(obj, LV_PART_MAIN, &label_dsc);
        lv_area_t a;
        a.y1 = obj->coords.y2 - pad_bottom + 2;
        a.y2 = a.y1 + lv_font_get_line_height(label_dsc.font);
        for (int hour = 6; hour <= 18; hour += 6)
        {
            int quarter = hour * 4;
            // center label
            String text = (hour < 10 ? "0" : "") + String(hour) + ":00";
            lv_point_t size;
            lv_txt_get_size(&size, text.c_str(), label_dsc.font, label_dsc.letter_space, label_dsc.line_space, LV_COORD_MAX, label_dsc.flag);
            a.x1 = obj->coords.x1 + offset_x + quarter * (segmentWidth + segmentGap) + (segmentWidth - size.x) / 2;
            a.x2 = a.x1 + size.x;
            -1;
            lv_draw_label(dsc->draw_ctx, &label_dsc, &a, text.c_str(), NULL);
        }
    }
}

static void solar_chart_draw_event_cb(lv_event_t *e)
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
        draw_rect_dsc.bg_opa = LV_OPA_50;
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
            lv_snprintf(dsc->text, dsc->text_length, "%d%%", (int)dsc->value);
        }
        else if (dsc->id == LV_CHART_AXIS_SECONDARY_Y)
        {
            lv_snprintf(dsc->text, dsc->text_length, "%d\nkW", (int)round(dsc->value / 1000.0f));
        }
        else if (dsc->id == LV_CHART_AXIS_PRIMARY_X)
        {
            int linesCount = 5; // max(2, min(5, (int) lv_chart_get_point_count(obj)));
            int segmentsCount = linesCount - 1;
            int pointCountRoundedUp = ((lv_chart_get_point_count(obj) / segmentsCount)) * (segmentsCount + 1);
            int maxMinutes = pointCountRoundedUp * (CHART_SAMPLE_INTERVAL_MS / 60000);
            int minutesPerLine = maxMinutes / linesCount;
            int totalMinutes = (linesCount - (dsc->value + 1)) * minutesPerLine;
            int hours = totalMinutes / 60;
            int minutes = totalMinutes % 60;
            if (totalMinutes == 0)
            {
                memset(dsc->text, 0, dsc->text_length);
                // lv_snprintf(dsc->text, dsc->text_length, "");
            }
            else
            {
                String format = "-";
                if (hours > 0)
                {
                    format += String(hours) + "h";
                }
                if (minutes > 0)
                {
                    format += String(minutes) + "min";
                }
                lv_snprintf(dsc->text, dsc->text_length, format.c_str());
            }
            // lv_snprintf(dsc->text, dsc->text_length, "%d", dsc->value);
        }
    }
}

class DashboardUI
{
private:
    long shownMillis = 0;

public:
    const int UI_REFRESH_PERIOD_MS = 5000;

    DashboardUI(void (*onSettingsShow)(lv_event_t *))
    {
        lv_obj_add_event_cb(ui_Chart1, solar_chart_draw_event_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);
        lv_obj_add_event_cb(ui_spotPriceContainer, electricity_price_draw_event_cb, LV_EVENT_DRAW_PART_END, NULL);
        lv_obj_add_event_cb(ui_settingsButton, onSettingsShow, LV_EVENT_RELEASED, NULL);

        pvAnimator.setup(ui_LeftContainer, _ui_theme_color_pvColor);
        batteryAnimator.setup(ui_LeftContainer, _ui_theme_color_batteryColor);
        gridAnimator.setup(ui_LeftContainer, _ui_theme_color_gridColor);
        loadAnimator.setup(ui_LeftContainer, _ui_theme_color_loadColor);

        // remove demo chart series from designer
        while (lv_chart_get_series_next(ui_Chart1, NULL))
        {
            lv_chart_remove_series(ui_Chart1, lv_chart_get_series_next(ui_Chart1, NULL));
        }

        pvPowerSeries = lv_chart_add_series(ui_Chart1, lv_color_hex(_ui_theme_color_pvColor[0]), LV_CHART_AXIS_SECONDARY_Y);
        acPowerSeries = lv_chart_add_series(ui_Chart1, lv_color_hex(_ui_theme_color_loadColor[0]), LV_CHART_AXIS_SECONDARY_Y);
        socSeries = lv_chart_add_series(ui_Chart1, lv_color_hex(_ui_theme_color_batteryColor[0]), LV_CHART_AXIS_PRIMARY_Y);
    }

    ~DashboardUI()
    {
    }

    bool isWallboxSmartChecked()
    {
        return lv_obj_get_state(ui_wallboxSmartCheckbox) & LV_STATE_CHECKED;
    }

    void show()
    {
        lv_scr_load(ui_Dashboard);

        // show settings button
        lv_obj_clear_flag(ui_settingsButton, LV_OBJ_FLAG_HIDDEN);
        shownMillis = millis();
    }

    int getSelfUsePowerPercent(InverterData_t &inverterData)
    {
        return constrain(inverterData.loadPower > 0 ? (100 * (inverterData.loadPower + inverterData.gridPower)) / inverterData.loadPower : 0, 0, 100);
    }

    void update(InverterData_t &inverterData, InverterData_t &previousInverterData, MedianPowerSampler &uiMedianPowerSampler, ShellyResult_t &shellyResult, ShellyResult_t &previousShellyResult, WallboxResult_t &wallboxResult, WallboxResult_t &previousWallboxResult, SolarChartDataProvider &solarChartDataProvider, ElectricityPriceResult_t &electricityPriceResult, ElectricityPriceResult_t &previousElectricityPriceResult, int wifiSignalPercent)
    {
        // hide settings button after one minute
        if (millis() - shownMillis > 60000)
        {
            lv_obj_add_flag(ui_settingsButton, LV_OBJ_FLAG_HIDDEN);
        }

        if (uiMedianPowerSampler.hasValidSamples())
        {
            isDarkMode = uiMedianPowerSampler.getMedianPVPower() == 0;
            uiMedianPowerSampler.resetSamples();
        }
        int selfUseEnergyTodayPercent = inverterData.loadToday > 0 ? ((inverterData.loadToday - inverterData.gridBuyToday) / inverterData.loadToday) * 100 : 0;
        selfUseEnergyTodayPercent = constrain(selfUseEnergyTodayPercent, 0, 100);
        int pvPower = inverterData.pv1Power + inverterData.pv2Power + inverterData.pv3Power + inverterData.pv4Power;
        int inPower = pvPower;

        if (inverterData.batteryPower < 0)
        {
            inPower += abs(inverterData.batteryPower);
        }
        if (inverterData.gridPower < 0)
        {
            inPower += abs(inverterData.gridPower);
        }

        int outPower = inverterData.loadPower;
        if (inverterData.batteryPower > 0)
        {
            outPower += inverterData.batteryPower;
        }
        if (inverterData.gridPower > 0)
        {
            outPower += inverterData.gridPower;
        }
        int totalPhasePower = inverterData.L1Power + inverterData.L2Power + inverterData.L3Power;
        int l1PercentUsage = totalPhasePower > 0 ? (100 * inverterData.L1Power) / totalPhasePower : 0;
        int l2PercentUsage = totalPhasePower > 0 ? (100 * inverterData.L2Power) / totalPhasePower : 0;
        int l3PercentUsage = totalPhasePower > 0 ? (100 * inverterData.L3Power) / totalPhasePower : 0;
        bool hasPhases = inverterData.L1Power > 0 || inverterData.L2Power > 0 || inverterData.L3Power > 0;

        if (hasPhases)
        {
            // show inverter phase container
            lv_obj_clear_flag(ui_inverterPhasesContainer, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(ui_inverterPhasesContainer, LV_OBJ_FLAG_HIDDEN);
        }

        int totalGridPhasePower = abs(inverterData.gridPowerL1) + abs(inverterData.gridPowerL2) + abs(inverterData.gridPowerL3);
        bool hasGridPhases = inverterData.gridPowerL1 != 0 || inverterData.gridPowerL2 != 0 || inverterData.gridPowerL3 != 0;
        if (hasGridPhases)
        {
            // show grid phase container
            lv_obj_clear_flag(ui_meterPowerBarL1, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_meterPowerBarL2, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_meterPowerBarL3, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_meterPowerLabelL1, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_meterPowerLabelL2, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_meterPowerLabelL3, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            // hide grid phase container
            lv_obj_add_flag(ui_meterPowerBarL1, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_meterPowerBarL2, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_meterPowerBarL3, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_meterPowerLabelL1, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_meterPowerLabelL2, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_meterPowerLabelL3, LV_OBJ_FLAG_HIDDEN);
        }

        lv_color_t black = lv_color_make(0, 0, 0);
        lv_color_t white = lv_color_make(255, 255, 255);

        lv_color_t textColor = isDarkMode ? white : black;
        lv_color_t containerBackground = isDarkMode ? black : white;

        pvPowerTextAnimator.animate(ui_pvLabel,
                                    previousInverterData.pv1Power + previousInverterData.pv2Power + previousInverterData.pv3Power + previousInverterData.pv4Power,
                                    inverterData.pv1Power + inverterData.pv2Power + inverterData.pv3Power + inverterData.pv4Power);
        lv_label_set_text(ui_pv1Label, format(POWER, inverterData.pv1Power, 1.0f, true).formatted.c_str());
        lv_label_set_text(ui_pv2Label, format(POWER, inverterData.pv2Power, 1.0f, true).formatted.c_str());
        lv_label_set_text(ui_pv3Label, format(POWER, inverterData.pv3Power, 1.0f, true).formatted.c_str());
        lv_label_set_text(ui_pv4Label, format(POWER, inverterData.pv4Power, 1.0f, true).formatted.c_str());

        if (inverterData.pv1Power == 0 || inverterData.pv2Power == 0)
        { // hide
            lv_obj_add_flag(ui_pvStringsContainer, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_clear_flag(ui_pvStringsContainer, LV_OBJ_FLAG_HIDDEN);
        }
        if (inverterData.pv3Power == 0 && inverterData.pv4Power == 0)
        {
            lv_obj_add_flag(ui_pvStringsContainer1, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_clear_flag(ui_pvStringsContainer1, LV_OBJ_FLAG_HIDDEN);
        }

        lv_label_set_text_fmt(ui_inverterTemperatureLabel, "%d°C", inverterData.inverterTemperature);
        if (inverterData.inverterTemperature > 50)
        {
            lv_obj_set_style_bg_color(ui_inverterTemperatureLabel, red, 0);
            lv_obj_set_style_shadow_color(ui_inverterTemperatureLabel, red, 0);
            lv_obj_set_style_text_color(ui_inverterTemperatureLabel, white, 0);
        }
        else if (inverterData.inverterTemperature > 40)
        {
            lv_obj_set_style_bg_color(ui_inverterTemperatureLabel, orange, 0);
            lv_obj_set_style_shadow_color(ui_inverterTemperatureLabel, orange, 0);
            lv_obj_set_style_text_color(ui_inverterTemperatureLabel, black, 0);
        }
        else
        {
            lv_obj_set_style_bg_color(ui_inverterTemperatureLabel, green, 0);
            lv_obj_set_style_shadow_color(ui_inverterTemperatureLabel, green, 0);
            lv_obj_set_style_text_color(ui_inverterTemperatureLabel, white, 0);
        }

        if (inverterData.inverterTemperature == 0)
        {
            lv_obj_add_flag(ui_inverterTemperatureLabel, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_clear_flag(ui_inverterTemperatureLabel, LV_OBJ_FLAG_HIDDEN);
        }

        inverterPowerTextAnimator.animate(ui_inverterPowerLabel, previousInverterData.inverterPower, inverterData.inverterPower);
        pvBackgroundAnimator.animate(ui_pvContainer, ((inverterData.pv1Power + inverterData.pv2Power + inverterData.pv3Power + inverterData.pv4Power) > 0) ? lv_color_hex(_ui_theme_color_pvColor[0]) : containerBackground);
        lv_label_set_text(ui_inverterPowerUnitLabel, format(POWER, inverterData.inverterPower).unit.c_str());
        
        //phases
        lv_label_set_text(ui_inverterPowerL1Label, format(POWER, inverterData.L1Power, 1.0f, false).formatted.c_str());
        lv_bar_set_value(ui_inverterPowerBar1, min(2400, inverterData.L1Power), LV_ANIM_ON);
        lv_obj_set_style_bg_color(ui_inverterPowerBar1, l1PercentUsage > 50 && inverterData.L1Power > 1200 ? red : textColor, LV_PART_INDICATOR);
        lv_obj_set_style_text_color(ui_inverterPowerL1Label, l1PercentUsage > 50 && inverterData.L1Power > 1200 ? red : textColor, 0);
        lv_label_set_text(ui_inverterPowerL2Label, format(POWER, inverterData.L2Power, 1.0f, false).formatted.c_str());
        lv_bar_set_value(ui_inverterPowerBar2, min(2400, inverterData.L2Power), LV_ANIM_ON);
        lv_obj_set_style_bg_color(ui_inverterPowerBar2, l2PercentUsage > 50 && inverterData.L2Power > 1200 ? red : textColor, LV_PART_INDICATOR);
        lv_obj_set_style_text_color(ui_inverterPowerL2Label, l2PercentUsage > 50 && inverterData.L2Power > 1200 ? red : textColor, 0);
        lv_label_set_text(ui_inverterPowerL3Label, format(POWER, inverterData.L3Power, 1.0f, false).formatted.c_str());
        lv_bar_set_value(ui_inverterPowerBar3, min(2400, inverterData.L3Power), LV_ANIM_ON);
        lv_obj_set_style_bg_color(ui_inverterPowerBar3, l3PercentUsage > 50 && inverterData.L3Power > 1200 ? red : textColor, LV_PART_INDICATOR);
        lv_obj_set_style_text_color(ui_inverterPowerL3Label, l3PercentUsage > 50 && inverterData.L3Power > 1200 ? red : textColor, 0);

        //grid phases
        lv_label_set_text(ui_meterPowerLabelL1, format(POWER, inverterData.gridPowerL1, 1.0f, false).formatted.c_str());
        lv_bar_set_value(ui_meterPowerBarL1, max((int32_t)-2400, min((int32_t)2400, inverterData.gridPowerL1)), LV_ANIM_ON);
        lv_obj_set_style_bg_color(ui_meterPowerBarL1, inverterData.gridPowerL1 < 0 ? orange : green, LV_PART_INDICATOR);
        lv_obj_set_style_text_color(ui_meterPowerLabelL1, inverterData.gridPowerL1 < 0 ? orange : textColor, 0);
        lv_label_set_text(ui_meterPowerLabelL2, format(POWER, inverterData.gridPowerL2, 1.0f, false).formatted.c_str());
        lv_bar_set_value(ui_meterPowerBarL2, max((int32_t)-2400, min((int32_t)2400, inverterData.gridPowerL2)), LV_ANIM_ON);
        lv_obj_set_style_bg_color(ui_meterPowerBarL2, inverterData.gridPowerL2 < 0 ? orange : green, LV_PART_INDICATOR);
        lv_obj_set_style_text_color(ui_meterPowerLabelL2, inverterData.gridPowerL2 < 0 ? orange : textColor, 0);
        lv_label_set_text(ui_meterPowerLabelL3, format(POWER, inverterData.gridPowerL3, 1.0f, false).formatted.c_str());
        lv_bar_set_value(ui_meterPowerBarL3, max((int32_t)-2400, min((int32_t)2400, inverterData.gridPowerL3)), LV_ANIM_ON);
        lv_obj_set_style_bg_color(ui_meterPowerBarL3, inverterData.gridPowerL3 < 0 ? orange : green, LV_PART_INDICATOR);
        lv_obj_set_style_text_color(ui_meterPowerLabelL3, inverterData.gridPowerL3 < 0 ? orange : textColor, 0);

        loadPowerTextAnimator.animate(ui_loadPowerLabel, previousInverterData.loadPower, inverterData.loadPower);
        lv_label_set_text(ui_loadPowerUnitLabel, format(POWER, inverterData.loadPower).unit.c_str());
        feedInPowerTextAnimator.animate(ui_feedInPowerLabel, abs(previousInverterData.gridPower), abs(inverterData.gridPower));
        lv_label_set_text(ui_feedInPowerUnitLabel, format(POWER, abs(inverterData.gridPower)).unit.c_str());
        gridBackgroundAnimator.animate(ui_gridContainer, ((inverterData.gridPower) < 0) ? lv_color_hex(_ui_theme_color_gridColor[0]) : containerBackground);
        lv_label_set_text_fmt(ui_socLabel, (inverterData.socApproximated ? "~%d" : "%d"), inverterData.soc);

        lv_label_set_text(ui_batteryPowerLabel, format(POWER, abs(inverterData.batteryPower), 1.0f, true).formatted.c_str());
        batteryBackgroundAnimator.animate(ui_batteryContainer, ((inverterData.batteryPower) < 0) ? lv_color_hex(_ui_theme_color_batteryColor[0]) : containerBackground);
        updateBatteryIcon(inverterData.soc);
        if (inverterData.batteryCapacityWh > 0)
        {
            if (abs(inverterData.batteryPower) > 100)
            {

                if (inverterData.batteryPower < 0)
                {
                    int capacityRemainingWh = (inverterData.soc - inverterData.minSoc) * inverterData.batteryCapacityWh / 100;
                    int secondsRemaining = (3600 * capacityRemainingWh) / abs(inverterData.batteryPower);
                    lv_label_set_text_fmt(ui_batteryTimeLabel, "%s - %d%%", formatTimeSpan(secondsRemaining).c_str(), inverterData.minSoc);
                }
                else if (inverterData.batteryPower > 0)
                {
                    int availableCapacityWh = (inverterData.maxSoc - inverterData.soc) * inverterData.batteryCapacityWh / 100;
                    int secondsRemaining = (3600 * availableCapacityWh) / inverterData.batteryPower;
                    lv_label_set_text_fmt(ui_batteryTimeLabel, "%s - %d%%", formatTimeSpan(secondsRemaining).c_str(), inverterData.maxSoc);
                }
            }
            else
            {
                lv_label_set_text(ui_batteryTimeLabel, "");
            }
        }
        else
        {
            lv_label_set_text(ui_batteryTimeLabel, "");
        }

        lv_label_set_text_fmt(ui_batteryTemperatureLabel, "%d°C", inverterData.batteryTemperature);

        if (inverterData.batteryTemperature > 40)
        {
            lv_obj_set_style_bg_color(ui_batteryTemperatureLabel, red, 0);
            lv_obj_set_style_shadow_color(ui_batteryTemperatureLabel, red, 0);
            lv_obj_set_style_text_color(ui_batteryTemperatureLabel, white, 0);
        }
        else if (inverterData.batteryTemperature > 30)
        {
            lv_obj_set_style_bg_color(ui_batteryTemperatureLabel, orange, 0);
            lv_obj_set_style_shadow_color(ui_batteryTemperatureLabel, orange, 0);
            lv_obj_set_style_text_color(ui_batteryTemperatureLabel, black, 0);
        }
        else
        {
            lv_obj_set_style_bg_color(ui_batteryTemperatureLabel, green, 0);
            lv_obj_set_style_shadow_color(ui_batteryTemperatureLabel, green, 0);
            lv_obj_set_style_text_color(ui_batteryTemperatureLabel, white, 0);
        }

        lv_label_set_text_fmt(ui_selfUsePercentLabel, "%d%%", getSelfUsePowerPercent(inverterData));

        if (getSelfUsePowerPercent(inverterData) > 50)
        {
            selfUseBackgroundAnimator.animate(ui_selfUsePercentLabel, green);
            // lv_obj_set_style_bg_color(ui_selfUsePercentLabel, green, 0);
        }
        else if (getSelfUsePowerPercent(inverterData) > 30)
        {
            selfUseBackgroundAnimator.animate(ui_selfUsePercentLabel, orange);
            // lv_obj_set_style_bg_color(ui_selfUsePercentLabel, orange, 0);
        }
        else
        {
            selfUseBackgroundAnimator.animate(ui_selfUsePercentLabel, red);
            // lv_obj_set_style_bg_color(ui_selfUsePercentLabel, red, 0);
        }
        lv_label_set_text(ui_yieldTodayLabel, format(ENERGY, inverterData.pvToday * 1000.0, 1).value.c_str());
        lv_label_set_text(ui_yieldTodayUnitLabel, format(ENERGY, inverterData.pvToday * 1000.0, 1).unit.c_str());
        lv_label_set_text(ui_yieldTotalLabel, format(ENERGY, inverterData.pvTotal * 1000.0, 1, true).value.c_str());
        lv_label_set_text(ui_yieldTotalUnitLabel, format(ENERGY, inverterData.pvTotal * 1000.0, 1, true).unit.c_str());
        lv_label_set_text(ui_gridSellTodayLabel, ("+" + format(ENERGY, inverterData.gridSellToday * 1000.0, 1).value).c_str());
        lv_label_set_text(ui_gridSellTodayUnitLabel, format(ENERGY, inverterData.gridSellToday * 1000.0, 1).unit.c_str());
        lv_label_set_text(ui_gridBuyTodayLabel, ("-" + format(ENERGY, inverterData.gridBuyToday * 1000.0, 1).value).c_str());
        lv_obj_set_style_text_color(ui_gridBuyTodayLabel, red, 0);
        lv_obj_set_style_text_color(ui_gridBuyTodayUnitLabel, red, 0);
        lv_label_set_text(ui_gridBuyTodayUnitLabel, format(ENERGY, inverterData.gridBuyToday * 1000.0, 1).unit.c_str());
        lv_label_set_text(ui_batteryChargedTodayLabel, ("+" + format(ENERGY, inverterData.batteryChargedToday * 1000.0, 1).value).c_str());
        lv_label_set_text(ui_batteryChargedTodayUnitLabel, (format(ENERGY, inverterData.batteryChargedToday * 1000.0, 1).unit).c_str());
        lv_label_set_text(ui_batteryDischargedTodayLabel, ("-" + format(ENERGY, inverterData.batteryDischargedToday * 1000.0, 1).value).c_str());
        lv_label_set_text(ui_batteryDischargedTodayUnitLabel, (format(ENERGY, inverterData.batteryDischargedToday * 1000.0, 1).unit).c_str());
        lv_obj_set_style_text_color(ui_batteryDischargedTodayLabel, red, 0);
        lv_obj_set_style_text_color(ui_batteryDischargedTodayUnitLabel, red, 0);
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

        if (inverterData.hasBattery)
        {
            lv_obj_clear_flag(ui_batteryContainer, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(ui_batteryStatsContainer, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(ui_batteryContainer, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_batteryStatsContainer, LV_OBJ_FLAG_HIDDEN);
        }

        if (shellyResult.pairedCount > 0)
        {
            lv_obj_clear_flag(ui_shellyContainer, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(ui_shellyContainer, LV_OBJ_FLAG_HIDDEN);
        }
        lv_label_set_text(ui_shellyPowerLabel, format(POWER, shellyResult.totalPower).formatted.c_str());
        if (shellyResult.maxPercent > 0)
        {
            int uiPercent = shellyResult.maxPercent;
            if (uiPercent < 60)
            {
                uiPercent = 10;
            }
            else if (uiPercent > 90)
            {
                uiPercent = 100;
            }
            else
            {
                uiPercent = map(uiPercent, 60, 90, 10, 100);
            }

            lv_label_set_text_fmt(ui_shellyCountLabel, "%d%% / %d / %d", uiPercent, shellyResult.activeCount, shellyResult.pairedCount);
        }
        else
        {
            lv_label_set_text_fmt(ui_shellyCountLabel, "%d / %d", shellyResult.activeCount, shellyResult.pairedCount);
        }

        wallboxPowerTextAnimator.animate(ui_wallboxPowerLabel, previousWallboxResult.chargingPower, wallboxResult.chargingPower);
        lv_label_set_text(ui_wallboxPowerUnitLabel, format(POWER, wallboxResult.chargingPower).unit.c_str());
        wallboxBackgroundAnimator.animate(ui_wallboxContainer, wallboxResult.chargingPower > 0 ? /*orange*/ containerBackground : containerBackground);
        if (wallboxResult.chargingControlEnabled)
        {
            // show charging control
            lv_obj_clear_flag(ui_wallboxSmartCheckbox, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(ui_wallboxSmartCheckbox, LV_OBJ_FLAG_HIDDEN);
        }
        if (wallboxResult.evConnected)
        {
            lv_obj_clear_flag(ui_wallboxPowerContainer, LV_OBJ_FLAG_HIDDEN);

            // charged energy
            if (wallboxResult.chargedEnergy > 0)
            {
                lv_label_set_text(ui_wallboxEnergyLabel, format(ENERGY, wallboxResult.chargedEnergy * 1000.0, 1).formatted.c_str());
                lv_obj_clear_flag(ui_wallboxEnergyContainer, LV_OBJ_FLAG_HIDDEN);
            }
            else
            {
                lv_obj_add_flag(ui_wallboxEnergyContainer, LV_OBJ_FLAG_HIDDEN);
            }
        }
        else
        {
            lv_obj_add_flag(ui_wallboxPowerContainer, LV_OBJ_FLAG_HIDDEN);

            // charged total energy
            if (wallboxResult.totalChargedEnergy > 0)
            {
                lv_label_set_text(ui_wallboxEnergyLabel, format(ENERGY, wallboxResult.totalChargedEnergy * 1000.0, 1, true).formatted.c_str());
                lv_obj_clear_flag(ui_wallboxEnergyContainer, LV_OBJ_FLAG_HIDDEN);
            }
            else
            {
                lv_obj_add_flag(ui_wallboxEnergyContainer, LV_OBJ_FLAG_HIDDEN);
            }
        }

        if (wallboxResult.updated > 0)
        {
            // show container
            lv_obj_clear_flag(ui_wallboxContainer, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(ui_wallboxContainer, LV_OBJ_FLAG_HIDDEN);
        }

        // wallbox temperature
        if (wallboxResult.temperature > 0)
        {
            lv_label_set_text_fmt(ui_wallboxTemperatureLabel, "%d°C", wallboxResult.temperature);
            if (wallboxResult.temperature > 40)
            {
                lv_obj_set_style_bg_color(ui_wallboxTemperatureLabel, red, 0);
                lv_obj_set_style_shadow_color(ui_wallboxTemperatureLabel, red, 0);
                lv_obj_set_style_text_color(ui_wallboxTemperatureLabel, white, 0);
            }
            else if (wallboxResult.temperature > 30)
            {
                lv_obj_set_style_bg_color(ui_wallboxTemperatureLabel, orange, 0);
                lv_obj_set_style_shadow_color(ui_wallboxTemperatureLabel, orange, 0);
                lv_obj_set_style_text_color(ui_wallboxTemperatureLabel, black, 0);
            }
            else
            {
                lv_obj_set_style_bg_color(ui_wallboxTemperatureLabel, green, 0);
                lv_obj_set_style_shadow_color(ui_wallboxTemperatureLabel, green, 0);
                lv_obj_set_style_text_color(ui_wallboxTemperatureLabel, white, 0);
            }
            lv_obj_clear_flag(ui_wallboxTemperatureLabel, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(ui_wallboxTemperatureLabel, LV_OBJ_FLAG_HIDDEN);
        }

        // hide all logos
        lv_obj_add_flag(ui_wallboxLogoEcovolterImage, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_wallboxLogoSolaxImage, LV_OBJ_FLAG_HIDDEN);

        switch (wallboxResult.type)
        {
        case WALLBOX_TYPE_SOLAX:
            // show solax logo
            lv_obj_clear_flag(ui_wallboxLogoSolaxImage, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_shadow_color(ui_wallboxContainer, lv_color_hex(_ui_theme_color_pvColor[0]), 0);
            break;
        case WALLBOX_TYPE_ECOVOLTER_PRO_V2:
            // show ecovolter logo
            lv_obj_clear_flag(ui_wallboxLogoEcovolterImage, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_shadow_color(ui_wallboxContainer, lv_color_hex(_ui_theme_color_loadColor[0]), 0);
            break;
        default:
            break;
        }

        updateSolarChart(inverterData, solarChartDataProvider, isDarkMode);

        lv_obj_set_style_text_color(ui_statusLabel, lv_palette_main(LV_PALETTE_DEEP_ORANGE), 0);

        switch (inverterData.status)
        {
        case DONGLE_STATUS_OK:
            lv_obj_set_style_text_color(ui_statusLabel, lv_palette_main(LV_PALETTE_GREY), 0);
            lv_label_set_text_fmt(ui_statusLabel, "%s %d%%", inverterData.sn.c_str(), wifiSignalPercent);

            lv_label_set_text(ui_dongleFWVersion, inverterData.dongleFWVersion.c_str());
            if (inverterData.dongleFWVersion.isEmpty())
            {
                lv_obj_add_flag(ui_dongleFWVersion, LV_OBJ_FLAG_HIDDEN);
            }
            else
            {
                lv_obj_clear_flag(ui_dongleFWVersion, LV_OBJ_FLAG_HIDDEN);
            }
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

        // electricity spot price block
        if (electricityPriceResult.updated > 0)
        {
            // show
            lv_obj_clear_flag(ui_spotPriceContainer, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            // hide
            lv_obj_add_flag(ui_spotPriceContainer, LV_OBJ_FLAG_HIDDEN);
        }

        updateElectricityPriceChart(electricityPriceResult, isDarkMode);
        updateCurrentPrice(electricityPriceResult, isDarkMode);

        lv_obj_set_style_bg_color(ui_Dashboard, isDarkMode ? black : white, 0);
        lv_obj_set_style_bg_color(ui_LeftContainer, containerBackground, 0);
        lv_obj_set_style_bg_opa(ui_LeftContainer, isDarkMode ? LV_OPA_80 : LV_OPA_80, 0);
        lv_obj_set_style_bg_color(ui_pvStatsContainer, containerBackground, 0);
        lv_obj_set_style_bg_opa(ui_pvStatsContainer, isDarkMode ? LV_OPA_80 : LV_OPA_80, 0);
        lv_obj_set_style_bg_color(ui_batteryStatsContainer, containerBackground, 0);
        lv_obj_set_style_bg_opa(ui_batteryStatsContainer, isDarkMode ? LV_OPA_80 : LV_OPA_80, 0);
        lv_obj_set_style_bg_color(ui_gridStatsContainer, containerBackground, 0);
        lv_obj_set_style_bg_opa(ui_gridStatsContainer, isDarkMode ? LV_OPA_80 : LV_OPA_80, 0);
        lv_obj_set_style_bg_color(ui_loadStatsContainer, containerBackground, 0);
        lv_obj_set_style_bg_opa(ui_loadStatsContainer, isDarkMode ? LV_OPA_80 : LV_OPA_80, 0);
        lv_obj_set_style_bg_color(ui_RightBottomContainer, containerBackground, 0);
        lv_obj_set_style_bg_opa(ui_RightBottomContainer, isDarkMode ? LV_OPA_80 : LV_OPA_80, 0);
        lv_obj_set_style_bg_color(ui_inverterContainer, containerBackground, 0);
        lv_obj_set_style_outline_color(ui_inverterContainer, containerBackground, 0);
        lv_obj_set_style_outline_opa(ui_inverterContainer, LV_OPA_80, 0);
        lv_obj_set_style_line_opa(ui_Chart1, isDarkMode ? LV_OPA_20 : LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(ui_loadContainer, isDarkMode ? black : white, 0);
        lv_obj_set_style_bg_color(ui_spotPriceContainer, isDarkMode ? black : white, 0);
        lv_obj_set_style_bg_opa(ui_spotPriceContainer, isDarkMode ? LV_OPA_80 : LV_OPA_80, 0);
        lv_obj_set_style_text_color(ui_Dashboard, isDarkMode ? white : black, 0);
        lv_obj_set_style_text_color(ui_selfUsePercentLabel, isDarkMode ? black : white, 0);
    }

    void updateBatteryIcon(int soc) {
        if (soc >= 95) {
            lv_img_set_src(ui_batteryImage, &ui_img_battery_100_png);
        } else if (soc >= 75) {
            lv_img_set_src(ui_batteryImage, &ui_img_battery_80_png);
        } else if (soc >= 55) {
            lv_img_set_src(ui_batteryImage, &ui_img_battery_60_png);
        } else if (soc >= 35) {
            lv_img_set_src(ui_batteryImage, &ui_img_battery_40_png);
        } else if (soc >= 15) {
            lv_img_set_src(ui_batteryImage, &ui_img_battery_20_png);
        } else {
            lv_img_set_src(ui_batteryImage, &ui_img_battery_0_png);
        }
    }
private:
    int const UI_TEXT_CHANGE_ANIMATION_DURATION = UI_REFRESH_PERIOD_MS;
    int const UI_BACKGROUND_ANIMATION_DURATION = UI_REFRESH_PERIOD_MS / 3;
    UITextChangeAnimator loadPowerTextAnimator = UITextChangeAnimator(POWER, UI_TEXT_CHANGE_ANIMATION_DURATION);
    UITextChangeAnimator feedInPowerTextAnimator = UITextChangeAnimator(POWER, UI_TEXT_CHANGE_ANIMATION_DURATION);
    UITextChangeAnimator inverterPowerTextAnimator = UITextChangeAnimator(POWER, UI_TEXT_CHANGE_ANIMATION_DURATION);
    UITextChangeAnimator wallboxPowerTextAnimator = UITextChangeAnimator(POWER, UI_TEXT_CHANGE_ANIMATION_DURATION);
    UITextChangeAnimator pvPowerTextAnimator = UITextChangeAnimator(POWER, UI_TEXT_CHANGE_ANIMATION_DURATION);
    UITextChangeAnimator batteryPercentTextAnimator = UITextChangeAnimator(PERCENT, UI_TEXT_CHANGE_ANIMATION_DURATION);
    UITextChangeAnimator selfUsePercentTextAnimator = UITextChangeAnimator(PERCENT, UI_TEXT_CHANGE_ANIMATION_DURATION);
    UIBackgroundAnimator pvBackgroundAnimator = UIBackgroundAnimator(UI_BACKGROUND_ANIMATION_DURATION);
    UIBackgroundAnimator batteryBackgroundAnimator = UIBackgroundAnimator(UI_BACKGROUND_ANIMATION_DURATION);
    UIBackgroundAnimator gridBackgroundAnimator = UIBackgroundAnimator(UI_BACKGROUND_ANIMATION_DURATION);
    UIBackgroundAnimator wallboxBackgroundAnimator = UIBackgroundAnimator(UI_BACKGROUND_ANIMATION_DURATION);
    UIBackgroundAnimator selfUseBackgroundAnimator = UIBackgroundAnimator(UI_BACKGROUND_ANIMATION_DURATION);

    UIBallAnimator pvAnimator;
    UIBallAnimator batteryAnimator;
    UIBallAnimator gridAnimator;
    UIBallAnimator loadAnimator;
    UIBallAnimator shellyAnimator;

    lv_chart_series_t *pvPowerSeries;
    lv_chart_series_t *acPowerSeries;
    lv_chart_series_t *socSeries;
    void updateSolarChart(InverterData_t &inverterData, SolarChartDataProvider &solarChartDataProvider, bool isDarkMode)
    {
        uint32_t i;

        pvPowerSeries->start_point = 0;
        acPowerSeries->start_point = 0;

        float maxPower = 5000.0f;
        int c = 0;
        for (i = 0; i < CHART_SAMPLES_PER_DAY; i++)
        {
            SolarChartDataItem_t item = solarChartDataProvider.getData()[CHART_SAMPLES_PER_DAY - i - 1];
            if (item.samples == 0)
            {
                continue;
            }

            lv_chart_set_next_value(ui_Chart1, pvPowerSeries, item.pvPower);
            lv_chart_set_next_value(ui_Chart1, acPowerSeries, item.loadPower);
            if (inverterData.hasBattery)
            {
                lv_chart_set_next_value(ui_Chart1, socSeries, item.soc);
            }

            maxPower = max(maxPower, max(item.pvPower, item.loadPower));

            c++;
        }
        lv_chart_set_point_count(ui_Chart1, c);
        // lv_chart_set_div_line_count(ui_Chart1, 5, 6);
        // lv_chart_set_axis_tick( ui_Chart1, LV_CHART_AXIS_PRIMARY_X, 0, 0, 6, max(2, min(5, c)), true, 32);
        lv_chart_set_range(ui_Chart1, LV_CHART_AXIS_SECONDARY_Y, 0, (lv_coord_t)maxPower);
        lv_obj_set_style_text_color(ui_Chart1, isDarkMode ? lv_color_white() : lv_color_black(), LV_PART_TICKS);
    }

    void updateCurrentPrice(ElectricityPriceResult_t &electricityPriceResult, bool isDarkMode)
    {
        ElectricityPriceItem_t currentPrice = getCurrentQuarterElectricityPrice(electricityPriceResult);
        String priceText = String(currentPrice.electricityPrice, 2) + " " + electricityPriceResult.currency + " / " + electricityPriceResult.energyUnit;
        priceText.replace(".", ",");
        lv_label_set_text(ui_currentPriceLabel, priceText.c_str());
        lv_color_t color = getPriceLevelColor(currentPrice.priceLevel);

        lv_obj_set_style_bg_color(ui_currentPriceLabel, color, 0);
        lv_obj_set_style_text_color(ui_currentPriceLabel, isDarkMode ? lv_color_black() : lv_color_white(), 0);
        lv_obj_set_style_shadow_color(ui_currentPriceLabel, color, 0);
    }

    void updateElectricityPriceChart(ElectricityPriceResult_t &electricityPriceResult, bool isDarkMode)
    {
        lv_obj_set_user_data(ui_spotPriceContainer, (void *)&electricityPriceResult);
        lv_obj_invalidate(ui_spotPriceContainer);
    }

    void updateFlowAnimations(InverterData_t inverterData, ShellyResult_t shellyResult)
    {
        int duration = UI_REFRESH_PERIOD_MS / 3;
        int offsetY = 15;
        int offsetX = 30;

        if ((inverterData.pv1Power + inverterData.pv2Power + inverterData.pv3Power + inverterData.pv4Power) > 0)
        {
            pvAnimator.run(ui_pvContainer, ui_inverterContainer, duration, UI_BACKGROUND_ANIMATION_DURATION + 0, 0, ((inverterData.pv1Power + inverterData.pv2Power + inverterData.pv3Power + inverterData.pv4Power) / 1000) + 1, -offsetX, -offsetY);
        }
        else
        {
            pvAnimator.hide();
        }

        if (inverterData.hasBattery)
        {
            if (inverterData.batteryPower > 0)
            {
                batteryAnimator.run(ui_inverterContainer, ui_batteryContainer, duration, UI_BACKGROUND_ANIMATION_DURATION + duration, 1, (inverterData.batteryPower / 1000) + 1, offsetX, -offsetY);
            }
            else if (inverterData.batteryPower < 0)
            {
                batteryAnimator.run(ui_batteryContainer, ui_inverterContainer, duration, UI_BACKGROUND_ANIMATION_DURATION + 0, 0, (abs(inverterData.batteryPower) / 1000) + 1, offsetX, -offsetY);
            }
            else
            {
                batteryAnimator.hide();
            }
        }
        else
        {
            batteryAnimator.hide();
        }

        if (inverterData.gridPower > 0)
        {
            gridAnimator.run(ui_inverterContainer, ui_gridContainer, duration, UI_BACKGROUND_ANIMATION_DURATION + duration, 1, (inverterData.gridPower / 1000) + 1, offsetX, offsetY);
        }
        else if (inverterData.gridPower < 0)
        {
            gridAnimator.run(ui_gridContainer, ui_inverterContainer, duration, UI_BACKGROUND_ANIMATION_DURATION + 0, 0, (abs(inverterData.gridPower) / 1000) + 1, offsetX, offsetY);
        }
        else
        {
            gridAnimator.hide();
        }

        if (inverterData.loadPower > 0)
        {
            loadAnimator.run(ui_inverterContainer, ui_loadContainer, duration, UI_BACKGROUND_ANIMATION_DURATION + duration, 1, (inverterData.loadPower / 1000) + 1, -offsetX, offsetY);
        }
        else
        {
            loadAnimator.hide();
        }
    }

    void updateWallboxData(WallboxResult_t &wallboxResult, WallboxResult_t &previousWallboxResult)
    {
        // Update wallbox data UI elements
    }
};