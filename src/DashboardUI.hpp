#pragma once

#include <Arduino.h>
#include <RemoteLogger.hpp>
#include <lvgl.h>
#include "BaseUI.hpp"
#include "ui/ui_helpers.h"
#include "ui/ui_theme_manager.h"
#include "ui/ui_themes.h"

// Image declarations
LV_IMG_DECLARE(ui_img_logo_png);
LV_IMG_DECLARE(ui_img_1516017106);
LV_IMG_DECLARE(ui_img_battery_60_png);
LV_IMG_DECLARE(ui_img_564643105);
LV_IMG_DECLARE(ui_img_960241876);
LV_IMG_DECLARE(ui_img_1061873486);
LV_IMG_DECLARE(ui_img_solax_png);
LV_IMG_DECLARE(ui_img_ecovolter3_png);
LV_IMG_DECLARE(ui_img_performance_png);
LV_IMG_DECLARE(ui_img_settings_png);
LV_IMG_DECLARE(ui_img_battery_0_png);
LV_IMG_DECLARE(ui_img_battery_100_png);
LV_IMG_DECLARE(ui_img_battery_20_png);
LV_IMG_DECLARE(ui_img_battery_40_png);
LV_IMG_DECLARE(ui_img_battery_80_png);
LV_IMG_DECLARE(ui_img_1279321064);
LV_IMG_DECLARE(ui_img_1337922523);

// Font declarations
LV_FONT_DECLARE(ui_font_OpenSansExtraSmall);
LV_FONT_DECLARE(ui_font_OpenSansLarge);
LV_FONT_DECLARE(ui_font_OpenSansLargeBold);
LV_FONT_DECLARE(ui_font_OpenSansMedium);
LV_FONT_DECLARE(ui_font_OpenSansMediumBold);
LV_FONT_DECLARE(ui_font_OpenSansSmall);
#include "utils/UIBallAnimator.hpp"
#include "Inverters/InverterResult.hpp"
#include "Shelly/Shelly.hpp"
#include "utils/UITextChangeAnimator.hpp"
#include "utils/UIBackgroundAnimatior.hpp"
#include "utils/MedianPowerSampler.hpp"
#include "Spot/ElectricityPriceLoader.hpp"
#include <SolarIntelligence.h>
#include "utils/IntelligenceHelpers.hpp"
#include "utils/Localization.hpp"

lv_color_t red = lv_color_hex(0xE63946);
lv_color_t orange = lv_color_hex(0xFFA726);
lv_color_t green = lv_color_hex(0x4CAF50);

// Callback typ pro změnu režimu střídače
typedef void (*InverterModeChangeCallback)(SolarInverterMode_t mode, bool intelligenceEnabled);

// Forward declaration for touch callback
static void onAnyTouchShowButtons(lv_event_t *e);

static bool isDarkMode = false;

/**
 * Struktura pro kombinovaná data grafu spotových cen + plán inteligence
 */
typedef struct SpotChartData {
    ElectricityPriceTwoDays_t* priceResult;
    bool hasIntelligencePlan;                             // Zda máme platný plán
    bool hasValidPrices;                                  // Zda máme platné spotové ceny
} SpotChartData_t;

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
    SpotChartData_t *chartData = (SpotChartData_t *)lv_obj_get_user_data(obj);
    
    if (chartData == nullptr || chartData->priceResult == nullptr) return;
    ElectricityPriceTwoDays_t *electricityPriceResult = chartData->priceResult;

    if (dsc->part == LV_PART_MAIN)
    {
        lv_coord_t pad_left = lv_obj_get_style_pad_left(obj, LV_PART_MAIN);
        lv_coord_t pad_right = lv_obj_get_style_pad_right(obj, LV_PART_MAIN);
        lv_coord_t pad_top = lv_obj_get_style_pad_top(obj, LV_PART_MAIN);
        lv_coord_t pad_bottom = lv_obj_get_style_pad_bottom(obj, LV_PART_MAIN);
        lv_coord_t w = (int32_t)lv_obj_get_content_width(obj);
        lv_coord_t h = (int32_t)lv_obj_get_content_height(obj);
        
        // Determine if showing 1 or 2 days
        bool showTwoDays = electricityPriceResult->hasTomorrowData;
        uint32_t segmentCount = showTwoDays ? QUARTERS_TWO_DAYS : QUARTERS_OF_DAY;
        
        // Calculate segment width dynamically based on available width
        int32_t segmentWidth = w / segmentCount;
        if (segmentWidth < 1) segmentWidth = 1;
        int32_t offset_x = (w - (segmentCount * segmentWidth)) / 2;  // Center the chart

        // Use pre-calculated max from the price result, calculate min on the fly (it's fast)
        float minPrice = 0.0f;
        float maxPrice = electricityPriceResult->scaleMaxValue;
        for (uint32_t i = 0; i < segmentCount; i++) {
            float price = electricityPriceResult->prices[i].electricityPrice;
            if (price < minPrice) minPrice = price;
            if (price > maxPrice) maxPrice = price;
        }
        float priceRange = maxPrice - minPrice;
        if (priceRange < 0.1f) priceRange = 0.1f;  // Avoid division by zero
        lv_coord_t chartHeight = h;

        // Current quarter
        time_t now = time(nullptr);
        struct tm *timeinfo = localtime(&now);
        int currentQuarter = (timeinfo->tm_hour * 60 + timeinfo->tm_min) / 15;

        // Draw current quarter highlight
        lv_draw_rect_dsc_t current_quarter_dsc;
        lv_draw_rect_dsc_init(&current_quarter_dsc);
        current_quarter_dsc.bg_opa = LV_OPA_50;
        current_quarter_dsc.bg_color = isDarkMode ? lv_color_hex(0x555555) : lv_color_hex(0xAAAAAA);
        lv_area_t cq_a;
        cq_a.x1 = obj->coords.x1 + offset_x + currentQuarter * segmentWidth;
        cq_a.x2 = cq_a.x1 + segmentWidth - 1;
        cq_a.y1 = obj->coords.y1 + pad_top;
        cq_a.y2 = obj->coords.y2 - pad_bottom;
        lv_draw_rect(dsc->draw_ctx, &current_quarter_dsc, &cq_a);

        // Draw price segments - merge consecutive segments with same price level
        lv_draw_rect_dsc_t draw_rect_dsc;
        lv_draw_rect_dsc_init(&draw_rect_dsc);
        draw_rect_dsc.bg_opa = LV_OPA_30;
        draw_rect_dsc.border_opa = LV_OPA_80;
        draw_rect_dsc.border_width = 1;
        
        uint32_t i = 0;
        while (i < segmentCount)
        {
            PriceLevel_t currentLevel = electricityPriceResult->prices[i].priceLevel;
            float price = electricityPriceResult->prices[i].electricityPrice;
            uint32_t runStart = i;
            
            // Find run of same price level (for merging)
            while (i < segmentCount && electricityPriceResult->prices[i].priceLevel == currentLevel) {
                i++;
            }
            
            // Draw merged segment (but individual bars for price accuracy)
            lv_color_t color = getPriceLevelColor(currentLevel);
            draw_rect_dsc.bg_color = color;
            draw_rect_dsc.border_color = color;
            
            for (uint32_t j = runStart; j < i; j++) {
                float segPrice = electricityPriceResult->prices[j].electricityPrice;
                lv_area_t a;
                a.x1 = obj->coords.x1 + offset_x + j * segmentWidth;
                a.x2 = a.x1 + segmentWidth - 1;
                lv_coord_t barTopY = obj->coords.y1 + pad_top + (priceRange - segPrice + minPrice) * chartHeight / priceRange;
                lv_coord_t barBottomY = obj->coords.y1 + pad_top + (priceRange + minPrice) * chartHeight / priceRange - 1;
                a.y1 = barTopY;
                a.y2 = barBottomY;
                if (a.y1 > a.y2) {
                    lv_coord_t temp = a.y1;
                    a.y1 = a.y2;
                    a.y2 = temp;
                }
                lv_draw_rect(dsc->draw_ctx, &draw_rect_dsc, &a);
            }
        }

        // Draw x-axis time labels
        lv_draw_label_dsc_t label_dsc;
        label_dsc.font = &ui_font_OpenSansExtraSmall;
        lv_draw_label_dsc_init(&label_dsc);
        lv_obj_init_draw_label_dsc(obj, LV_PART_MAIN, &label_dsc);
        lv_area_t la;
        la.y1 = obj->coords.y2 - pad_bottom + 2;
        la.y2 = la.y1 + lv_font_get_line_height(label_dsc.font);
        
        if (showTwoDays)
        {
            // For 2 days: show 12:00, 00:00 (midnight separator), 12:00
            int hours[] = {12, 24, 36};  // 12:00 today, 00:00 tomorrow, 12:00 tomorrow
            for (int i = 0; i < 3; i++)
            {
                int quarter = hours[i] * 4;
                int displayHour = hours[i] % 24;
                String text = (displayHour < 10 ? "0" : "") + String(displayHour) + ":00";
                lv_point_t size;
                lv_txt_get_size(&size, text.c_str(), label_dsc.font, label_dsc.letter_space, label_dsc.line_space, LV_COORD_MAX, label_dsc.flag);
                la.x1 = obj->coords.x1 + offset_x + quarter * segmentWidth + (segmentWidth - size.x) / 2;
                la.x2 = la.x1 + size.x;
                lv_draw_label(dsc->draw_ctx, &label_dsc, &la, text.c_str(), NULL);
            }
        }
        else
        {
            // For 1 day: show 06:00, 12:00, 18:00
            for (int hour = 6; hour <= 18; hour += 6)
            {
                int quarter = hour * 4;
                String text = (hour < 10 ? "0" : "") + String(hour) + ":00";
                lv_point_t size;
                lv_txt_get_size(&size, text.c_str(), label_dsc.font, label_dsc.letter_space, label_dsc.line_space, LV_COORD_MAX, label_dsc.flag);
                la.x1 = obj->coords.x1 + offset_x + quarter * segmentWidth + (segmentWidth - size.x) / 2;
                la.x2 = la.x1 + size.x;
                lv_draw_label(dsc->draw_ctx, &label_dsc, &la, text.c_str(), NULL);
            }
        }
    }
}

static void solar_chart_draw_event_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    /*Add the faded area before the lines are drawn*/
    lv_obj_draw_part_dsc_t *dsc = lv_event_get_draw_part_dsc(e);
    
    // Zjistíme počet bodů v grafu (96 = 1 den, 192 = 2 dny)
    uint16_t pointCount = lv_chart_get_point_count(obj);
    bool showTwoDays = (pointCount > QUARTERS_OF_DAY);
    
    // Vykreslení vertikální čáry aktuálního času
    if (dsc->part == LV_PART_MAIN)
    {
        lv_coord_t pad_left = lv_obj_get_style_pad_left(obj, LV_PART_MAIN);
        lv_coord_t pad_right = lv_obj_get_style_pad_right(obj, LV_PART_MAIN);
        lv_coord_t pad_top = lv_obj_get_style_pad_top(obj, LV_PART_MAIN);
        lv_coord_t pad_bottom = lv_obj_get_style_pad_bottom(obj, LV_PART_MAIN);
        lv_coord_t w = lv_obj_get_content_width(obj);
        
        // Aktuální čtvrthodina
        time_t now = time(nullptr);
        struct tm *timeinfo = localtime(&now);
        int currentQuarter = (timeinfo->tm_hour * 60 + timeinfo->tm_min) / 15;
        
        // Pozice čáry
        float quarterWidth = (float)w / (float)pointCount;
        lv_coord_t lineX = obj->coords.x1 + pad_left + (lv_coord_t)(currentQuarter * quarterWidth + quarterWidth / 2);
        
        // Vykreslení čáry aktuálního času
        lv_draw_rect_dsc_t line_dsc;
        lv_draw_rect_dsc_init(&line_dsc);
        line_dsc.bg_opa = LV_OPA_70;
        line_dsc.bg_color = lv_color_hex(0xFF4444);  // Červená barva pro lepší viditelnost
        
        lv_area_t line_a;
        line_a.x1 = lineX - 1;
        line_a.x2 = lineX + 1;
        line_a.y1 = obj->coords.y1 + pad_top;
        line_a.y2 = obj->coords.y2 - pad_bottom;
        
        lv_draw_rect(dsc->draw_ctx, &line_dsc, &line_a);
    }
    else if (dsc->part == LV_PART_ITEMS)
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
        uint16_t pointCount = lv_chart_get_point_count(obj);
        bool showTwoDays = (pointCount > QUARTERS_OF_DAY);
        
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
            // Osa X - tick popisky
            int tickCount = 5;
            int quartersPerTick = pointCount / (tickCount - 1);
            int quarter = dsc->value * quartersPerTick;
            
            if (showTwoDays)
            {
                // Pro 2 dny: ukaž hodiny s označením dne
                int day = quarter / QUARTERS_OF_DAY;
                int hourInDay = (quarter % QUARTERS_OF_DAY) / 4;
                
                if (quarter == 0 || quarter >= pointCount)
                {
                    memset(dsc->text, 0, dsc->text_length);
                }
                else if (quarter == QUARTERS_OF_DAY)
                {
                    lv_snprintf(dsc->text, dsc->text_length, "00:00");
                }
                else
                {
                    lv_snprintf(dsc->text, dsc->text_length, "%02d:00", hourInDay);
                }
            }
            else
            {
                // Pro 1 den
                int hour = quarter / 4;
                
                if (hour == 0 || hour == 24)
                {
                    memset(dsc->text, 0, dsc->text_length);
                }
                else
                {
                    lv_snprintf(dsc->text, dsc->text_length, "%02d:00", hour);
                }
            }
        }
    }
}

class DashboardUI : public BaseUI
{
private:
    // UI elements (from screen.c)
    lv_obj_t* LeftContainer = nullptr;
    lv_obj_t* pvContainer = nullptr;
    lv_obj_t* Container11 = nullptr;
    lv_obj_t* pvLabel = nullptr;
    lv_obj_t* pvUnitLabel = nullptr;
    lv_obj_t* Container1 = nullptr;
    lv_obj_t* pvStringsContainer = nullptr;
    lv_obj_t* pv1Label = nullptr;
    lv_obj_t* pv2Label = nullptr;
    lv_obj_t* pvStringsContainer1 = nullptr;
    lv_obj_t* pv3Label = nullptr;
    lv_obj_t* pv4Label = nullptr;
    lv_obj_t* Image6 = nullptr;
    lv_obj_t* batteryContainer = nullptr;
    lv_obj_t* batteryTemperatureLabel = nullptr;
    lv_obj_t* Container23 = nullptr;
    lv_obj_t* socLabel = nullptr;
    lv_obj_t* socLabel1 = nullptr;
    lv_obj_t* Container17 = nullptr;
    lv_obj_t* batteryTimeLabel = nullptr;
    lv_obj_t* Container24 = nullptr;
    lv_obj_t* batteryPowerLabel = nullptr;
    lv_obj_t* Container25 = nullptr;
    lv_obj_t* batteryImage = nullptr;
    lv_obj_t* loadContainer = nullptr;
    lv_obj_t* Image8 = nullptr;
    lv_obj_t* Container14 = nullptr;
    lv_obj_t* shellyContainer = nullptr;
    lv_obj_t* shellyCountLabel = nullptr;
    lv_obj_t* shellyPowerLabel = nullptr;
    lv_obj_t* Container20 = nullptr;
    lv_obj_t* loadPowerLabel = nullptr;
    lv_obj_t* loadPowerUnitLabel = nullptr;
    lv_obj_t* gridContainer = nullptr;
    lv_obj_t* Image9 = nullptr;
    lv_obj_t* smartMeterContainer = nullptr;
    lv_obj_t* Container19 = nullptr;
    lv_obj_t* meterPowerBarL1 = nullptr;
    lv_obj_t* meterPowerLabelL1 = nullptr;
    lv_obj_t* Container2 = nullptr;
    lv_obj_t* meterPowerBarL2 = nullptr;
    lv_obj_t* meterPowerLabelL2 = nullptr;
    lv_obj_t* Container13 = nullptr;
    lv_obj_t* meterPowerBarL3 = nullptr;
    lv_obj_t* meterPowerLabelL3 = nullptr;
    lv_obj_t* Container22 = nullptr;
    lv_obj_t* feedInPowerLabel = nullptr;
    lv_obj_t* feedInPowerUnitLabel = nullptr;
    lv_obj_t* selfUsePercentLabel = nullptr;
    lv_obj_t* inverterContainer = nullptr;
    lv_obj_t* Image5 = nullptr;
    lv_obj_t* dongleFWVersion = nullptr;
    lv_obj_t* inverterTemperatureLabel = nullptr;
    lv_obj_t* Container26 = nullptr;
    lv_obj_t* inverterPowerLabel = nullptr;
    lv_obj_t* inverterPowerUnitLabel = nullptr;
    lv_obj_t* inverterPhasesContainer = nullptr;
    lv_obj_t* Container29 = nullptr;
    lv_obj_t* inverterPowerBar1 = nullptr;
    lv_obj_t* inverterPowerL1Label = nullptr;
    lv_obj_t* Container3 = nullptr;
    lv_obj_t* inverterPowerBar2 = nullptr;
    lv_obj_t* inverterPowerL2Label = nullptr;
    lv_obj_t* Container28 = nullptr;
    lv_obj_t* inverterPowerBar3 = nullptr;
    lv_obj_t* inverterPowerL3Label = nullptr;
    lv_obj_t* statusLabel = nullptr;
    lv_obj_t* wallboxContainer = nullptr;
    lv_obj_t* wallboxTemperatureLabel = nullptr;
    lv_obj_t* wallboxLogoSolaxImage = nullptr;
    lv_obj_t* wallboxLogoEcovolterImage = nullptr;
    lv_obj_t* wallboxPowerContainer = nullptr;
    lv_obj_t* wallboxPowerLabel = nullptr;
    lv_obj_t* wallboxPowerUnitLabel = nullptr;
    lv_obj_t* wallboxEnergyContainer = nullptr;
    lv_obj_t* wallboxEnergyLabel = nullptr;
    lv_obj_t* wallboxSmartCheckbox = nullptr;
    lv_obj_t* RightContainer = nullptr;
    lv_obj_t* TopRightContainer = nullptr;
    lv_obj_t* Container33 = nullptr;
    lv_obj_t* pvStatsContainer = nullptr;
    lv_obj_t* Image11 = nullptr;
    lv_obj_t* Container34 = nullptr;
    lv_obj_t* Container27 = nullptr;
    lv_obj_t* yieldTodayLabel = nullptr;
    lv_obj_t* yieldTodayUnitLabel = nullptr;
    lv_obj_t* Container5 = nullptr;
    lv_obj_t* yieldTotalLabel = nullptr;
    lv_obj_t* yieldTotalUnitLabel = nullptr;
    lv_obj_t* loadStatsContainer = nullptr;
    lv_obj_t* Image13 = nullptr;
    lv_obj_t* Container37 = nullptr;
    lv_obj_t* Container8 = nullptr;
    lv_obj_t* loadTodayLabel = nullptr;
    lv_obj_t* loadTodayUnitLabel = nullptr;
    lv_obj_t* Container30 = nullptr;
    lv_obj_t* selfUseTodayLabel = nullptr;
    lv_obj_t* selfUseTodayUnitLabel = nullptr;
    lv_obj_t* Container36 = nullptr;
    lv_obj_t* batteryStatsContainer = nullptr;
    lv_obj_t* Image12 = nullptr;
    lv_obj_t* Container35 = nullptr;
    lv_obj_t* Container6 = nullptr;
    lv_obj_t* batteryChargedTodayLabel = nullptr;
    lv_obj_t* batteryChargedTodayUnitLabel = nullptr;
    lv_obj_t* Container9 = nullptr;
    lv_obj_t* batteryDischargedTodayLabel = nullptr;
    lv_obj_t* batteryDischargedTodayUnitLabel = nullptr;
    lv_obj_t* gridStatsContainer = nullptr;
    lv_obj_t* Image14 = nullptr;
    lv_obj_t* Container38 = nullptr;
    lv_obj_t* Container7 = nullptr;
    lv_obj_t* gridSellTodayLabel = nullptr;
    lv_obj_t* gridSellTodayUnitLabel = nullptr;
    lv_obj_t* Container10 = nullptr;
    lv_obj_t* gridBuyTodayLabel = nullptr;
    lv_obj_t* gridBuyTodayUnitLabel = nullptr;
    lv_obj_t* clocksLabel = nullptr;
    lv_obj_t* RightBottomContainer = nullptr;
    lv_obj_t* Chart1 = nullptr;
    lv_obj_t* spotPriceContainer = nullptr;
    lv_obj_t* currentPriceLabel = nullptr;
    lv_obj_t* settingsButton = nullptr;

    long shownMillis = 0;
    long lastTouchMillis = 0;  // Time of last touch on dashboard
    static const long BUTTONS_HIDE_TIMEOUT_MS = 10000;  // Hide buttons after 10 seconds
    bool intelligenceSupported = false;  // Whether current inverter supports intelligence
    bool intelligenceEnabled = false;    // Whether intelligence is enabled in settings
    lv_obj_t *intelligenceModeLabel = nullptr;  // Label pro zobrazení režimu inteligence
    lv_obj_t *ipBadge = nullptr;  // Floating badge showing station IP address
    lv_obj_t *ipBadgeLabel = nullptr;  // Label inside IP badge
    lv_obj_t *inverterModeMenu = nullptr;   // Popup menu pro výběr režimu střídače
    lv_obj_t *inverterModeOverlay = nullptr; // Overlay za popup menu
    lv_obj_t *modeChangeSpinner = nullptr;  // Spinner overlay při změně režimu
    InverterModeChangeCallback modeChangeCallback = nullptr;
    void (*onSettingsShowCallback)(lv_event_t*) = nullptr;
    void (*onIntelligenceShowCallback)(lv_event_t*) = nullptr;
    
    // Chart zoom state - which chart is currently expanded (nullptr = none)
    lv_obj_t *expandedChart = nullptr;
    
    // Intelligence plan tile
    lv_obj_t *intelligencePlanTile = nullptr;
    lv_obj_t *intelligencePlanSummary = nullptr;  // Container for summary row
    lv_obj_t *intelligenceSummaryTitle = nullptr;  // "Inteligence" label
    lv_obj_t *intelligenceSummarySavings = nullptr;  // Savings value
    lv_obj_t *intelligenceSummaryBadge = nullptr;  // Mode badge with background
    lv_obj_t *intelligenceSummaryChevron = nullptr; // Chevron icon (right arrow)
    lv_obj_t *intelligencePlanDetail = nullptr;
    lv_obj_t *intelligenceDetailTitle = nullptr;
    // Upcoming plans container and rows
    lv_obj_t *intelligenceUpcomingContainer = nullptr;
    static constexpr int VISIBLE_PLAN_ROWS = 48;  // Support up to 48 plan changes (2 days)
    lv_obj_t *intelligenceUpcomingRows[VISIBLE_PLAN_ROWS] = {nullptr};
    lv_obj_t *intelligenceUpcomingModes[VISIBLE_PLAN_ROWS] = {nullptr};
    lv_obj_t *intelligenceUpcomingTimes[VISIBLE_PLAN_ROWS] = {nullptr};
    lv_obj_t *intelligenceUpcomingReasons[VISIBLE_PLAN_ROWS] = {nullptr};
    lv_obj_t *intelligenceUpcomingBullets[VISIBLE_PLAN_ROWS] = {nullptr};  // Timeline bullets
    lv_obj_t *intelligenceUpcomingLines[VISIBLE_PLAN_ROWS] = {nullptr};    // Timeline lines
    // Stats container
    lv_obj_t *intelligenceStatsContainer = nullptr;
    lv_obj_t *intelligenceStatsSeparator = nullptr;  // Vertical separator between stats
    lv_obj_t *intelligenceStatsProduction = nullptr;
    lv_obj_t *intelligenceStatsProductionUnit = nullptr;
    lv_obj_t *intelligenceStatsConsumption = nullptr;
    lv_obj_t *intelligenceStatsConsumptionUnit = nullptr;


    void createUI() {

screen = lv_obj_create(NULL);
lv_obj_clear_flag( screen, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM );    /// Flags
lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
lv_obj_set_scroll_dir(screen, LV_DIR_VER);
lv_obj_set_flex_flow(screen,LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
lv_obj_set_style_radius(screen, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
ui_object_set_themeable_style_property(screen, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BG_COLOR, _ui_theme_color_pvColor);
ui_object_set_themeable_style_property(screen, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BG_OPA, _ui_theme_alpha_pvColor);
ui_object_set_themeable_style_property(screen, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BG_GRAD_COLOR, _ui_theme_color_batteryColor);
lv_obj_set_style_bg_main_stop(screen, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_bg_grad_stop(screen, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_bg_grad_dir(screen, LV_GRAD_DIR_VER, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_left(screen, 8, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_right(screen, 8, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_top(screen, 8, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_bottom(screen, 8, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_row(screen, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_column(screen, 8, LV_PART_MAIN| LV_STATE_DEFAULT);

LeftContainer = lv_obj_create(screen);
lv_obj_remove_style_all(LeftContainer);
lv_obj_set_height( LeftContainer, lv_pct(100));
lv_obj_set_flex_grow( LeftContainer, 1);
lv_obj_set_x( LeftContainer, 4 );
lv_obj_set_y( LeftContainer, -20 );
lv_obj_set_align( LeftContainer, LV_ALIGN_CENTER );
lv_obj_add_flag( LeftContainer, LV_OBJ_FLAG_OVERFLOW_VISIBLE );   /// Flags
lv_obj_clear_flag( LeftContainer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_radius(LeftContainer, 24, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_bg_color(LeftContainer, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_bg_opa(LeftContainer, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_color(LeftContainer, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_shadow_opa(LeftContainer, 64, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_width(LeftContainer, 32, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_spread(LeftContainer, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_left(LeftContainer, 8, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_right(LeftContainer, 8, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_top(LeftContainer, 8, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_bottom(LeftContainer, 8, LV_PART_MAIN| LV_STATE_DEFAULT);

pvContainer = lv_obj_create(LeftContainer);
lv_obj_remove_style_all(pvContainer);
lv_obj_set_width( pvContainer, lv_pct(38));
lv_obj_set_height( pvContainer, lv_pct(38));
lv_obj_set_flex_flow(pvContainer,LV_FLEX_FLOW_COLUMN);
lv_obj_set_flex_align(pvContainer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
lv_obj_clear_flag( pvContainer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_radius(pvContainer, 16, LV_PART_MAIN| LV_STATE_DEFAULT);
ui_object_set_themeable_style_property(pvContainer, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BG_COLOR, _ui_theme_color_pvColor);
ui_object_set_themeable_style_property(pvContainer, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BG_OPA, _ui_theme_alpha_pvColor);
ui_object_set_themeable_style_property(pvContainer, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BORDER_COLOR, _ui_theme_color_pvColor);
ui_object_set_themeable_style_property(pvContainer, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BORDER_OPA, _ui_theme_alpha_pvColor);
lv_obj_set_style_border_side(pvContainer, LV_BORDER_SIDE_FULL, LV_PART_MAIN| LV_STATE_DEFAULT);
ui_object_set_themeable_style_property(pvContainer, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_SHADOW_COLOR, _ui_theme_color_pvColor);
ui_object_set_themeable_style_property(pvContainer, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_SHADOW_OPA, _ui_theme_alpha_pvColor);
lv_obj_set_style_shadow_width(pvContainer, 48, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_spread(pvContainer, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_left(pvContainer, 16, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_right(pvContainer, 16, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_top(pvContainer, 16, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_bottom(pvContainer, 16, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_row(pvContainer, 4, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_column(pvContainer, 0, LV_PART_MAIN| LV_STATE_DEFAULT);

Container11 = lv_obj_create(pvContainer);
lv_obj_remove_style_all(Container11);
lv_obj_set_width( Container11, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( Container11, LV_SIZE_CONTENT);   /// 20
lv_obj_set_align( Container11, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(Container11,LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(Container11, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
lv_obj_clear_flag( Container11, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags

pvLabel = lv_label_create(Container11);
lv_obj_set_width( pvLabel, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( pvLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( pvLabel, LV_ALIGN_CENTER );
lv_label_set_text(pvLabel,"6534");
lv_obj_set_style_text_font(pvLabel, &ui_font_OpenSansLargeBold, LV_PART_MAIN| LV_STATE_DEFAULT);

pvUnitLabel = lv_label_create(Container11);
lv_obj_set_width( pvUnitLabel, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( pvUnitLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( pvUnitLabel, LV_ALIGN_CENTER );
lv_label_set_text(pvUnitLabel,"W");
lv_obj_set_style_text_font(pvUnitLabel, &ui_font_OpenSansSmall, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_bottom(pvUnitLabel, 5, LV_PART_MAIN| LV_STATE_DEFAULT);

Container1 = lv_obj_create(pvContainer);
lv_obj_remove_style_all(Container1);
lv_obj_set_width( Container1, lv_pct(100));
lv_obj_set_flex_grow( Container1, 1);
lv_obj_set_align( Container1, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(Container1,LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(Container1, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
lv_obj_add_flag( Container1, LV_OBJ_FLAG_OVERFLOW_VISIBLE );   /// Flags
lv_obj_clear_flag( Container1, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_pad_row(Container1, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_column(Container1, 4, LV_PART_MAIN| LV_STATE_DEFAULT);

pvStringsContainer = lv_obj_create(Container1);
lv_obj_remove_style_all(pvStringsContainer);
lv_obj_set_width( pvStringsContainer, LV_SIZE_CONTENT);  /// 100
lv_obj_set_height( pvStringsContainer, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( pvStringsContainer, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(pvStringsContainer,LV_FLEX_FLOW_COLUMN);
lv_obj_set_flex_align(pvStringsContainer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
lv_obj_clear_flag( pvStringsContainer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_pad_row(pvStringsContainer, 4, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_column(pvStringsContainer, 8, LV_PART_MAIN| LV_STATE_DEFAULT);

pv1Label = lv_label_create(pvStringsContainer);
lv_obj_set_width( pv1Label, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( pv1Label, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( pv1Label, LV_ALIGN_CENTER );
lv_label_set_text(pv1Label,"3014W");
lv_obj_set_style_text_align(pv1Label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(pv1Label, &ui_font_OpenSansMedium, LV_PART_MAIN| LV_STATE_DEFAULT);

pv2Label = lv_label_create(pvStringsContainer);
lv_obj_set_width( pv2Label, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( pv2Label, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( pv2Label, LV_ALIGN_CENTER );
lv_label_set_text(pv2Label,"3520W");
lv_obj_set_style_text_align(pv2Label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(pv2Label, &ui_font_OpenSansMedium, LV_PART_MAIN| LV_STATE_DEFAULT);

pvStringsContainer1 = lv_obj_create(Container1);
lv_obj_remove_style_all(pvStringsContainer1);
lv_obj_set_width( pvStringsContainer1, LV_SIZE_CONTENT);  /// 100
lv_obj_set_height( pvStringsContainer1, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( pvStringsContainer1, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(pvStringsContainer1,LV_FLEX_FLOW_COLUMN);
lv_obj_set_flex_align(pvStringsContainer1, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
lv_obj_clear_flag( pvStringsContainer1, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_pad_row(pvStringsContainer1, 4, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_column(pvStringsContainer1, 8, LV_PART_MAIN| LV_STATE_DEFAULT);

pv3Label = lv_label_create(pvStringsContainer1);
lv_obj_set_width( pv3Label, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( pv3Label, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( pv3Label, LV_ALIGN_CENTER );
lv_label_set_text(pv3Label,"3014W");
lv_obj_set_style_text_align(pv3Label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(pv3Label, &ui_font_OpenSansMedium, LV_PART_MAIN| LV_STATE_DEFAULT);

pv4Label = lv_label_create(pvStringsContainer1);
lv_obj_set_width( pv4Label, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( pv4Label, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( pv4Label, LV_ALIGN_CENTER );
lv_label_set_text(pv4Label,"3520W");
lv_obj_set_style_text_align(pv4Label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(pv4Label, &ui_font_OpenSansMedium, LV_PART_MAIN| LV_STATE_DEFAULT);

Image6 = lv_img_create(pvContainer);
lv_img_set_src(Image6, &ui_img_1516017106);
lv_obj_set_width( Image6, LV_SIZE_CONTENT);  /// 48
lv_obj_set_height( Image6, LV_SIZE_CONTENT);   /// 48
lv_obj_set_x( Image6, -141 );
lv_obj_set_y( Image6, -169 );
lv_obj_set_align( Image6, LV_ALIGN_CENTER );
lv_obj_add_flag( Image6, LV_OBJ_FLAG_ADV_HITTEST );   /// Flags
lv_obj_clear_flag( Image6, LV_OBJ_FLAG_SCROLLABLE );    /// Flags

batteryContainer = lv_obj_create(LeftContainer);
lv_obj_remove_style_all(batteryContainer);
lv_obj_set_width( batteryContainer, lv_pct(38));
lv_obj_set_height( batteryContainer, lv_pct(38));
lv_obj_set_align( batteryContainer, LV_ALIGN_TOP_RIGHT );
lv_obj_set_flex_flow(batteryContainer,LV_FLEX_FLOW_COLUMN);
lv_obj_set_flex_align(batteryContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
lv_obj_add_flag( batteryContainer, LV_OBJ_FLAG_OVERFLOW_VISIBLE );   /// Flags
lv_obj_clear_flag( batteryContainer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_radius(batteryContainer, 16, LV_PART_MAIN| LV_STATE_DEFAULT);
ui_object_set_themeable_style_property(batteryContainer, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BG_COLOR, _ui_theme_color_batteryColor);
ui_object_set_themeable_style_property(batteryContainer, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BG_OPA, _ui_theme_alpha_batteryColor);
ui_object_set_themeable_style_property(batteryContainer, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BORDER_COLOR, _ui_theme_color_batteryColor);
ui_object_set_themeable_style_property(batteryContainer, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BORDER_OPA, _ui_theme_alpha_batteryColor);
lv_obj_set_style_border_side(batteryContainer, LV_BORDER_SIDE_FULL, LV_PART_MAIN| LV_STATE_DEFAULT);
ui_object_set_themeable_style_property(batteryContainer, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_SHADOW_COLOR, _ui_theme_color_batteryColor);
ui_object_set_themeable_style_property(batteryContainer, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_SHADOW_OPA, _ui_theme_alpha_batteryColor);
lv_obj_set_style_shadow_width(batteryContainer, 48, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_spread(batteryContainer, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_left(batteryContainer, 6, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_right(batteryContainer, 6, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_top(batteryContainer, 12, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_bottom(batteryContainer, 12, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_row(batteryContainer, 4, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_column(batteryContainer, 0, LV_PART_MAIN| LV_STATE_DEFAULT);

batteryTemperatureLabel = lv_label_create(batteryContainer);
lv_obj_set_width( batteryTemperatureLabel, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( batteryTemperatureLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_x( batteryTemperatureLabel, 16 );
lv_obj_set_y( batteryTemperatureLabel, -24 );
lv_obj_set_align( batteryTemperatureLabel, LV_ALIGN_TOP_RIGHT );
lv_label_set_text(batteryTemperatureLabel,"56°C");
lv_obj_add_flag( batteryTemperatureLabel, LV_OBJ_FLAG_IGNORE_LAYOUT );   /// Flags
lv_obj_set_style_text_align(batteryTemperatureLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(batteryTemperatureLabel, &ui_font_OpenSansSmall, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_radius(batteryTemperatureLabel, 8, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_bg_color(batteryTemperatureLabel, lv_color_hex(0xFFAA00), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_bg_opa(batteryTemperatureLabel, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_color(batteryTemperatureLabel, lv_color_hex(0xFFAA00), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_shadow_opa(batteryTemperatureLabel, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_width(batteryTemperatureLabel, 32, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_spread(batteryTemperatureLabel, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_left(batteryTemperatureLabel, 4, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_right(batteryTemperatureLabel, 4, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_top(batteryTemperatureLabel, 4, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_bottom(batteryTemperatureLabel, 4, LV_PART_MAIN| LV_STATE_DEFAULT);

Container23 = lv_obj_create(batteryContainer);
lv_obj_remove_style_all(Container23);
lv_obj_set_width( Container23, 100);
lv_obj_set_height( Container23, LV_SIZE_CONTENT);   /// 50
lv_obj_set_align( Container23, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(Container23,LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(Container23, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START);
lv_obj_clear_flag( Container23, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags

socLabel = lv_label_create(Container23);
lv_obj_set_width( socLabel, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( socLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( socLabel, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(socLabel,LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(socLabel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
lv_label_set_text(socLabel,"80");
lv_obj_set_style_text_font(socLabel, &ui_font_OpenSansLargeBold, LV_PART_MAIN| LV_STATE_DEFAULT);

socLabel1 = lv_label_create(Container23);
lv_obj_set_width( socLabel1, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( socLabel1, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( socLabel1, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(socLabel1,LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(socLabel1, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
lv_label_set_text(socLabel1,"%");
lv_obj_set_style_text_font(socLabel1, &ui_font_OpenSansSmall, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_bottom(socLabel1, 5, LV_PART_MAIN| LV_STATE_DEFAULT);

Container17 = lv_obj_create(batteryContainer);
lv_obj_remove_style_all(Container17);
lv_obj_set_width( Container17, lv_pct(100));
lv_obj_set_flex_grow( Container17, 1);
lv_obj_set_align( Container17, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(Container17,LV_FLEX_FLOW_COLUMN);
lv_obj_set_flex_align(Container17, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
lv_obj_clear_flag( Container17, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_pad_row(Container17, 4, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_column(Container17, 8, LV_PART_MAIN| LV_STATE_DEFAULT);

batteryTimeLabel = lv_label_create(Container17);
lv_obj_set_width( batteryTimeLabel, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( batteryTimeLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( batteryTimeLabel, LV_ALIGN_CENTER );
lv_label_set_text(batteryTimeLabel,"1d 12h 21m - 100%");
lv_obj_set_style_text_font(batteryTimeLabel, &ui_font_OpenSansExtraSmall, LV_PART_MAIN| LV_STATE_DEFAULT);

Container24 = lv_obj_create(Container17);
lv_obj_remove_style_all(Container24);
lv_obj_set_width( Container24, lv_pct(100));
lv_obj_set_height( Container24, LV_SIZE_CONTENT);   /// 50
lv_obj_set_align( Container24, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(Container24,LV_FLEX_FLOW_COLUMN);
lv_obj_set_flex_align(Container24, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
lv_obj_clear_flag( Container24, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_pad_row(Container24, 4, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_column(Container24, 0, LV_PART_MAIN| LV_STATE_DEFAULT);

batteryPowerLabel = lv_label_create(Container24);
lv_obj_set_width( batteryPowerLabel, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( batteryPowerLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( batteryPowerLabel, LV_ALIGN_CENTER );
lv_label_set_text(batteryPowerLabel,"5600W");
lv_obj_set_style_text_align(batteryPowerLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(batteryPowerLabel, &ui_font_OpenSansMedium, LV_PART_MAIN| LV_STATE_DEFAULT);

Container25 = lv_obj_create(batteryContainer);
lv_obj_remove_style_all(Container25);
lv_obj_set_height( Container25, 48);
lv_obj_set_width( Container25, lv_pct(100));
lv_obj_set_align( Container25, LV_ALIGN_CENTER );
lv_obj_add_flag( Container25, LV_OBJ_FLAG_OVERFLOW_VISIBLE );   /// Flags
lv_obj_clear_flag( Container25, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags

batteryImage = lv_img_create(Container25);
lv_img_set_src(batteryImage, &ui_img_battery_60_png);
lv_obj_set_width( batteryImage, LV_SIZE_CONTENT);  /// 48
lv_obj_set_height( batteryImage, LV_SIZE_CONTENT);   /// 48
lv_obj_set_align( batteryImage, LV_ALIGN_CENTER );
lv_obj_add_flag( batteryImage, LV_OBJ_FLAG_ADV_HITTEST );   /// Flags
lv_obj_clear_flag( batteryImage, LV_OBJ_FLAG_SCROLLABLE );    /// Flags

loadContainer = lv_obj_create(LeftContainer);
lv_obj_remove_style_all(loadContainer);
lv_obj_set_width( loadContainer, lv_pct(38));
lv_obj_set_height( loadContainer, lv_pct(40));
lv_obj_set_align( loadContainer, LV_ALIGN_BOTTOM_LEFT );
lv_obj_set_flex_flow(loadContainer,LV_FLEX_FLOW_COLUMN);
lv_obj_set_flex_align(loadContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
lv_obj_clear_flag( loadContainer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_radius(loadContainer, 16, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_bg_color(loadContainer, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_bg_opa(loadContainer, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
ui_object_set_themeable_style_property(loadContainer, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BORDER_COLOR, _ui_theme_color_loadColor);
ui_object_set_themeable_style_property(loadContainer, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BORDER_OPA, _ui_theme_alpha_loadColor);
lv_obj_set_style_border_side(loadContainer, LV_BORDER_SIDE_FULL, LV_PART_MAIN| LV_STATE_DEFAULT);
ui_object_set_themeable_style_property(loadContainer, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_SHADOW_COLOR, _ui_theme_color_loadColor);
ui_object_set_themeable_style_property(loadContainer, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_SHADOW_OPA, _ui_theme_alpha_loadColor);
lv_obj_set_style_shadow_width(loadContainer, 42, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_spread(loadContainer, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_ofs_x(loadContainer, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_ofs_y(loadContainer, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_left(loadContainer, 12, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_right(loadContainer, 12, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_top(loadContainer, 12, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_bottom(loadContainer, 12, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_row(loadContainer, 4, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_column(loadContainer, 0, LV_PART_MAIN| LV_STATE_DEFAULT);

Image8 = lv_img_create(loadContainer);
lv_img_set_src(Image8, &ui_img_564643105);
lv_obj_set_width( Image8, LV_SIZE_CONTENT);  /// 48
lv_obj_set_height( Image8, LV_SIZE_CONTENT);   /// 48
lv_obj_set_x( Image8, -141 );
lv_obj_set_y( Image8, -169 );
lv_obj_set_align( Image8, LV_ALIGN_CENTER );
lv_obj_add_flag( Image8, LV_OBJ_FLAG_ADV_HITTEST );   /// Flags
lv_obj_clear_flag( Image8, LV_OBJ_FLAG_SCROLLABLE );    /// Flags

Container14 = lv_obj_create(loadContainer);
lv_obj_remove_style_all(Container14);
lv_obj_set_width( Container14, lv_pct(100));
lv_obj_set_flex_grow( Container14, 1);
lv_obj_set_align( Container14, LV_ALIGN_CENTER );
lv_obj_add_flag( Container14, LV_OBJ_FLAG_OVERFLOW_VISIBLE );   /// Flags
lv_obj_clear_flag( Container14, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags

shellyContainer = lv_obj_create(Container14);
lv_obj_remove_style_all(shellyContainer);
lv_obj_set_width( shellyContainer, lv_pct(100));
lv_obj_set_height( shellyContainer, lv_pct(100));
lv_obj_set_align( shellyContainer, LV_ALIGN_BOTTOM_MID );
lv_obj_set_flex_flow(shellyContainer,LV_FLEX_FLOW_COLUMN);
lv_obj_set_flex_align(shellyContainer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
lv_obj_add_flag( shellyContainer, LV_OBJ_FLAG_OVERFLOW_VISIBLE );   /// Flags
lv_obj_clear_flag( shellyContainer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags

shellyCountLabel = lv_label_create(shellyContainer);
lv_obj_set_width( shellyCountLabel, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( shellyCountLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( shellyCountLabel, LV_ALIGN_CENTER );
lv_label_set_text(shellyCountLabel,"0 / 3");
lv_obj_set_style_text_font(shellyCountLabel, &ui_font_OpenSansSmall, LV_PART_MAIN| LV_STATE_DEFAULT);

shellyPowerLabel = lv_label_create(shellyContainer);
lv_obj_set_width( shellyPowerLabel, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( shellyPowerLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( shellyPowerLabel, LV_ALIGN_CENTER );
lv_label_set_text(shellyPowerLabel,"0W");
lv_obj_set_style_text_align(shellyPowerLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(shellyPowerLabel, &ui_font_OpenSansMedium, LV_PART_MAIN| LV_STATE_DEFAULT);

Container20 = lv_obj_create(loadContainer);
lv_obj_remove_style_all(Container20);
lv_obj_set_width( Container20, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( Container20, LV_SIZE_CONTENT);   /// 50
lv_obj_set_align( Container20, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(Container20,LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(Container20, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START);
lv_obj_clear_flag( Container20, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags

loadPowerLabel = lv_label_create(Container20);
lv_obj_set_width( loadPowerLabel, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( loadPowerLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( loadPowerLabel, LV_ALIGN_CENTER );
lv_label_set_text(loadPowerLabel,"624");
lv_obj_set_style_text_font(loadPowerLabel, &ui_font_OpenSansLargeBold, LV_PART_MAIN| LV_STATE_DEFAULT);

loadPowerUnitLabel = lv_label_create(Container20);
lv_obj_set_width( loadPowerUnitLabel, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( loadPowerUnitLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( loadPowerUnitLabel, LV_ALIGN_CENTER );
lv_label_set_text(loadPowerUnitLabel,"W");
lv_obj_set_style_text_font(loadPowerUnitLabel, &ui_font_OpenSansSmall, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_bottom(loadPowerUnitLabel, 5, LV_PART_MAIN| LV_STATE_DEFAULT);

gridContainer = lv_obj_create(LeftContainer);
lv_obj_remove_style_all(gridContainer);
lv_obj_set_width( gridContainer, lv_pct(38));
lv_obj_set_height( gridContainer, lv_pct(40));
lv_obj_set_align( gridContainer, LV_ALIGN_BOTTOM_RIGHT );
lv_obj_set_flex_flow(gridContainer,LV_FLEX_FLOW_COLUMN);
lv_obj_set_flex_align(gridContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
lv_obj_clear_flag( gridContainer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_radius(gridContainer, 16, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_bg_color(gridContainer, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_bg_opa(gridContainer, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
ui_object_set_themeable_style_property(gridContainer, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BORDER_COLOR, _ui_theme_color_gridColor);
ui_object_set_themeable_style_property(gridContainer, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BORDER_OPA, _ui_theme_alpha_gridColor);
lv_obj_set_style_border_side(gridContainer, LV_BORDER_SIDE_FULL, LV_PART_MAIN| LV_STATE_DEFAULT);
ui_object_set_themeable_style_property(gridContainer, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_SHADOW_COLOR, _ui_theme_color_gridColor);
ui_object_set_themeable_style_property(gridContainer, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_SHADOW_OPA, _ui_theme_alpha_gridColor);
lv_obj_set_style_shadow_width(gridContainer, 48, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_spread(gridContainer, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_left(gridContainer, 12, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_right(gridContainer, 12, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_top(gridContainer, 12, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_bottom(gridContainer, 12, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_row(gridContainer, 4, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_column(gridContainer, 0, LV_PART_MAIN| LV_STATE_DEFAULT);

Image9 = lv_img_create(gridContainer);
lv_img_set_src(Image9, &ui_img_960241876);
lv_obj_set_width( Image9, LV_SIZE_CONTENT);  /// 48
lv_obj_set_height( Image9, LV_SIZE_CONTENT);   /// 48
lv_obj_set_x( Image9, -141 );
lv_obj_set_y( Image9, -169 );
lv_obj_set_align( Image9, LV_ALIGN_CENTER );
lv_obj_add_flag( Image9, LV_OBJ_FLAG_ADV_HITTEST );   /// Flags
lv_obj_clear_flag( Image9, LV_OBJ_FLAG_SCROLLABLE );    /// Flags

smartMeterContainer = lv_obj_create(gridContainer);
lv_obj_remove_style_all(smartMeterContainer);
lv_obj_set_width( smartMeterContainer, lv_pct(100));
lv_obj_set_flex_grow( smartMeterContainer, 1);
lv_obj_set_align( smartMeterContainer, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(smartMeterContainer,LV_FLEX_FLOW_COLUMN);
lv_obj_set_flex_align(smartMeterContainer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
lv_obj_clear_flag( smartMeterContainer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags

Container19 = lv_obj_create(smartMeterContainer);
lv_obj_remove_style_all(Container19);
lv_obj_set_width( Container19, lv_pct(100));
lv_obj_set_height( Container19, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( Container19, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(Container19,LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(Container19, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
lv_obj_clear_flag( Container19, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_pad_row(Container19, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_column(Container19, 4, LV_PART_MAIN| LV_STATE_DEFAULT);

meterPowerBarL1 = lv_bar_create(Container19);
lv_bar_set_mode(meterPowerBarL1, LV_BAR_MODE_SYMMETRICAL);
lv_bar_set_range(meterPowerBarL1, -2400,2400);
lv_bar_set_value(meterPowerBarL1,1626,LV_ANIM_OFF);
lv_bar_set_start_value(meterPowerBarL1, 0, LV_ANIM_OFF);
lv_obj_set_height( meterPowerBarL1, 10);
lv_obj_set_width( meterPowerBarL1, lv_pct(46));
lv_obj_set_x( meterPowerBarL1, -280 );
lv_obj_set_y( meterPowerBarL1, -54 );
lv_obj_set_align( meterPowerBarL1, LV_ALIGN_CENTER );
ui_object_set_themeable_style_property(meterPowerBarL1, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BG_COLOR, _ui_theme_color_inverterColor);
ui_object_set_themeable_style_property(meterPowerBarL1, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BG_OPA, _ui_theme_alpha_inverterColor);

lv_obj_set_style_bg_color(meterPowerBarL1, lv_color_hex(0x000000), LV_PART_INDICATOR | LV_STATE_DEFAULT );
lv_obj_set_style_bg_opa(meterPowerBarL1, 255, LV_PART_INDICATOR| LV_STATE_DEFAULT);

meterPowerLabelL1 = lv_label_create(Container19);
lv_obj_set_height( meterPowerLabelL1, LV_SIZE_CONTENT);   /// 1
lv_obj_set_flex_grow( meterPowerLabelL1, 1);
lv_obj_set_align( meterPowerLabelL1, LV_ALIGN_CENTER );
lv_label_set_text(meterPowerLabelL1,"1625W");
lv_obj_set_style_text_align(meterPowerLabelL1, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(meterPowerLabelL1, &ui_font_OpenSansSmall, LV_PART_MAIN| LV_STATE_DEFAULT);

Container2 = lv_obj_create(smartMeterContainer);
lv_obj_remove_style_all(Container2);
lv_obj_set_width( Container2, lv_pct(100));
lv_obj_set_height( Container2, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( Container2, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(Container2,LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(Container2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
lv_obj_clear_flag( Container2, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_pad_row(Container2, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_column(Container2, 4, LV_PART_MAIN| LV_STATE_DEFAULT);

meterPowerBarL2 = lv_bar_create(Container2);
lv_bar_set_mode(meterPowerBarL2, LV_BAR_MODE_SYMMETRICAL);
lv_bar_set_range(meterPowerBarL2, -2400,2400);
lv_bar_set_value(meterPowerBarL2,25,LV_ANIM_OFF);
lv_bar_set_start_value(meterPowerBarL2, 0, LV_ANIM_OFF);
lv_obj_set_height( meterPowerBarL2, 10);
lv_obj_set_width( meterPowerBarL2, lv_pct(46));
lv_obj_set_align( meterPowerBarL2, LV_ALIGN_CENTER );
ui_object_set_themeable_style_property(meterPowerBarL2, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BG_COLOR, _ui_theme_color_inverterColor);
ui_object_set_themeable_style_property(meterPowerBarL2, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BG_OPA, _ui_theme_alpha_inverterColor);

meterPowerLabelL2 = lv_label_create(Container2);
lv_obj_set_height( meterPowerLabelL2, LV_SIZE_CONTENT);   /// 1
lv_obj_set_flex_grow( meterPowerLabelL2, 1);
lv_obj_set_align( meterPowerLabelL2, LV_ALIGN_CENTER );
lv_label_set_text(meterPowerLabelL2,"123W");
lv_obj_set_style_text_align(meterPowerLabelL2, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(meterPowerLabelL2, &ui_font_OpenSansSmall, LV_PART_MAIN| LV_STATE_DEFAULT);

Container13 = lv_obj_create(smartMeterContainer);
lv_obj_remove_style_all(Container13);
lv_obj_set_width( Container13, lv_pct(100));
lv_obj_set_height( Container13, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( Container13, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(Container13,LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(Container13, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
lv_obj_clear_flag( Container13, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_pad_row(Container13, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_column(Container13, 4, LV_PART_MAIN| LV_STATE_DEFAULT);

meterPowerBarL3 = lv_bar_create(Container13);
lv_bar_set_mode(meterPowerBarL3, LV_BAR_MODE_SYMMETRICAL);
lv_bar_set_range(meterPowerBarL3, -2400,2400);
lv_bar_set_value(meterPowerBarL3,25,LV_ANIM_OFF);
lv_bar_set_start_value(meterPowerBarL3, 0, LV_ANIM_OFF);
lv_obj_set_height( meterPowerBarL3, 10);
lv_obj_set_width( meterPowerBarL3, lv_pct(46));
lv_obj_set_align( meterPowerBarL3, LV_ALIGN_CENTER );
ui_object_set_themeable_style_property(meterPowerBarL3, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BG_COLOR, _ui_theme_color_inverterColor);
ui_object_set_themeable_style_property(meterPowerBarL3, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BG_OPA, _ui_theme_alpha_inverterColor);

meterPowerLabelL3 = lv_label_create(Container13);
lv_obj_set_height( meterPowerLabelL3, LV_SIZE_CONTENT);   /// 1
lv_obj_set_flex_grow( meterPowerLabelL3, 1);
lv_obj_set_align( meterPowerLabelL3, LV_ALIGN_CENTER );
lv_label_set_text(meterPowerLabelL3,"315W");
lv_obj_set_style_text_align(meterPowerLabelL3, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(meterPowerLabelL3, &ui_font_OpenSansSmall, LV_PART_MAIN| LV_STATE_DEFAULT);

Container22 = lv_obj_create(gridContainer);
lv_obj_remove_style_all(Container22);
lv_obj_set_width( Container22, 100);
lv_obj_set_height( Container22, LV_SIZE_CONTENT);   /// 50
lv_obj_set_align( Container22, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(Container22,LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(Container22, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START);
lv_obj_clear_flag( Container22, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags

feedInPowerLabel = lv_label_create(Container22);
lv_obj_set_width( feedInPowerLabel, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( feedInPowerLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( feedInPowerLabel, LV_ALIGN_CENTER );
lv_label_set_text(feedInPowerLabel,"2410");
lv_obj_set_style_text_font(feedInPowerLabel, &ui_font_OpenSansLargeBold, LV_PART_MAIN| LV_STATE_DEFAULT);

feedInPowerUnitLabel = lv_label_create(Container22);
lv_obj_set_width( feedInPowerUnitLabel, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( feedInPowerUnitLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( feedInPowerUnitLabel, LV_ALIGN_CENTER );
lv_label_set_text(feedInPowerUnitLabel,"W");
lv_obj_set_style_text_font(feedInPowerUnitLabel, &ui_font_OpenSansSmall, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_bottom(feedInPowerUnitLabel, 5, LV_PART_MAIN| LV_STATE_DEFAULT);

selfUsePercentLabel = lv_label_create(LeftContainer);
lv_obj_set_width( selfUsePercentLabel, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( selfUsePercentLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( selfUsePercentLabel, LV_ALIGN_TOP_MID );
lv_label_set_text(selfUsePercentLabel,"86%");
lv_obj_set_style_text_color(selfUsePercentLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_text_opa(selfUsePercentLabel, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(selfUsePercentLabel, &ui_font_OpenSansMediumBold, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_radius(selfUsePercentLabel, 12, LV_PART_MAIN| LV_STATE_DEFAULT);
ui_object_set_themeable_style_property(selfUsePercentLabel, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BG_COLOR, _ui_theme_color_loadColor);
ui_object_set_themeable_style_property(selfUsePercentLabel, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BG_OPA, _ui_theme_alpha_loadColor);
lv_obj_set_style_pad_left(selfUsePercentLabel, 8, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_right(selfUsePercentLabel, 8, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_top(selfUsePercentLabel, 4, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_bottom(selfUsePercentLabel, 4, LV_PART_MAIN| LV_STATE_DEFAULT);

inverterContainer = lv_obj_create(LeftContainer);
lv_obj_remove_style_all(inverterContainer);
lv_obj_set_width( inverterContainer, lv_pct(42));
lv_obj_set_height( inverterContainer, LV_SIZE_CONTENT);   /// 1
lv_obj_set_x( inverterContainer, lv_pct(0) );
lv_obj_set_y( inverterContainer, lv_pct(-1) );
lv_obj_set_align( inverterContainer, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(inverterContainer,LV_FLEX_FLOW_COLUMN);
lv_obj_set_flex_align(inverterContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
lv_obj_add_flag( inverterContainer, LV_OBJ_FLAG_OVERFLOW_VISIBLE );   /// Flags
lv_obj_clear_flag( inverterContainer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_radius(inverterContainer, 16, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_bg_color(inverterContainer, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_bg_opa(inverterContainer, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
ui_object_set_themeable_style_property(inverterContainer, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BORDER_COLOR, _ui_theme_color_inverterColor);
ui_object_set_themeable_style_property(inverterContainer, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BORDER_OPA, _ui_theme_alpha_inverterColor);
lv_obj_set_style_border_side(inverterContainer, LV_BORDER_SIDE_FULL, LV_PART_MAIN| LV_STATE_DEFAULT);
ui_object_set_themeable_style_property(inverterContainer, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_SHADOW_COLOR, _ui_theme_color_inverterColor);
ui_object_set_themeable_style_property(inverterContainer, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_SHADOW_OPA, _ui_theme_alpha_inverterColor);
lv_obj_set_style_shadow_width(inverterContainer, 48, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_spread(inverterContainer, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_left(inverterContainer, 16, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_right(inverterContainer, 16, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_top(inverterContainer, 16, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_bottom(inverterContainer, 16, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_row(inverterContainer, 8, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_column(inverterContainer, 0, LV_PART_MAIN| LV_STATE_DEFAULT);

Image5 = lv_img_create(inverterContainer);
lv_img_set_src(Image5, &ui_img_1061873486);
lv_obj_set_width( Image5, LV_SIZE_CONTENT);  /// 48
lv_obj_set_height( Image5, LV_SIZE_CONTENT);   /// 48
lv_obj_set_x( Image5, -141 );
lv_obj_set_y( Image5, -169 );
lv_obj_set_align( Image5, LV_ALIGN_CENTER );
lv_obj_add_flag( Image5, LV_OBJ_FLAG_ADV_HITTEST );   /// Flags
lv_obj_clear_flag( Image5, LV_OBJ_FLAG_SCROLLABLE );    /// Flags

dongleFWVersion = lv_label_create(inverterContainer);
lv_obj_set_width( dongleFWVersion, LV_SIZE_CONTENT);  /// 100
lv_obj_set_height( dongleFWVersion, LV_SIZE_CONTENT);   /// 1
lv_obj_set_x( dongleFWVersion, -10 );
lv_obj_set_y( dongleFWVersion, -10 );
lv_label_set_text(dongleFWVersion,"3.005.01");
lv_obj_add_flag( dongleFWVersion, LV_OBJ_FLAG_IGNORE_LAYOUT );   /// Flags
lv_obj_set_style_text_font(dongleFWVersion, &ui_font_OpenSansExtraSmall, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_radius(dongleFWVersion, 12, LV_PART_MAIN| LV_STATE_DEFAULT);
ui_object_set_themeable_style_property(dongleFWVersion, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BG_COLOR, _ui_theme_color_inverterColor);
ui_object_set_themeable_style_property(dongleFWVersion, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BG_OPA, _ui_theme_alpha_inverterColor);
lv_obj_set_style_pad_left(dongleFWVersion, 6, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_right(dongleFWVersion, 6, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_top(dongleFWVersion, 6, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_bottom(dongleFWVersion, 6, LV_PART_MAIN| LV_STATE_DEFAULT);

inverterTemperatureLabel = lv_label_create(inverterContainer);
lv_obj_set_width( inverterTemperatureLabel, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( inverterTemperatureLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_x( inverterTemperatureLabel, 22 );
lv_obj_set_y( inverterTemperatureLabel, -28 );
lv_obj_set_align( inverterTemperatureLabel, LV_ALIGN_TOP_RIGHT );
lv_label_set_text(inverterTemperatureLabel,"56°C");
lv_obj_add_flag( inverterTemperatureLabel, LV_OBJ_FLAG_IGNORE_LAYOUT );   /// Flags
lv_obj_set_style_text_align(inverterTemperatureLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(inverterTemperatureLabel, &ui_font_OpenSansSmall, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_radius(inverterTemperatureLabel, 8, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_bg_color(inverterTemperatureLabel, lv_color_hex(0xFFAA00), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_bg_opa(inverterTemperatureLabel, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_color(inverterTemperatureLabel, lv_color_hex(0xFFAA00), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_shadow_opa(inverterTemperatureLabel, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_width(inverterTemperatureLabel, 32, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_spread(inverterTemperatureLabel, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_left(inverterTemperatureLabel, 4, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_right(inverterTemperatureLabel, 4, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_top(inverterTemperatureLabel, 4, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_bottom(inverterTemperatureLabel, 4, LV_PART_MAIN| LV_STATE_DEFAULT);

Container26 = lv_obj_create(inverterContainer);
lv_obj_remove_style_all(Container26);
lv_obj_set_width( Container26, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( Container26, LV_SIZE_CONTENT);   /// 50
lv_obj_set_align( Container26, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(Container26,LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(Container26, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START);
lv_obj_clear_flag( Container26, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags

inverterPowerLabel = lv_label_create(Container26);
lv_obj_set_width( inverterPowerLabel, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( inverterPowerLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( inverterPowerLabel, LV_ALIGN_CENTER );
lv_label_set_text(inverterPowerLabel,"1034");
lv_obj_set_style_text_font(inverterPowerLabel, &ui_font_OpenSansLargeBold, LV_PART_MAIN| LV_STATE_DEFAULT);

inverterPowerUnitLabel = lv_label_create(Container26);
lv_obj_set_width( inverterPowerUnitLabel, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( inverterPowerUnitLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( inverterPowerUnitLabel, LV_ALIGN_CENTER );
lv_label_set_text(inverterPowerUnitLabel,"W");
lv_obj_set_style_text_font(inverterPowerUnitLabel, &ui_font_OpenSansSmall, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_bottom(inverterPowerUnitLabel, 5, LV_PART_MAIN| LV_STATE_DEFAULT);

inverterPhasesContainer = lv_obj_create(inverterContainer);
lv_obj_remove_style_all(inverterPhasesContainer);
lv_obj_set_width( inverterPhasesContainer, lv_pct(100));
lv_obj_set_height( inverterPhasesContainer, LV_SIZE_CONTENT);   /// 50
lv_obj_set_align( inverterPhasesContainer, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(inverterPhasesContainer,LV_FLEX_FLOW_COLUMN);
lv_obj_set_flex_align(inverterPhasesContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START);
lv_obj_clear_flag( inverterPhasesContainer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags

Container29 = lv_obj_create(inverterPhasesContainer);
lv_obj_remove_style_all(Container29);
lv_obj_set_width( Container29, lv_pct(100));
lv_obj_set_height( Container29, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( Container29, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(Container29,LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(Container29, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
lv_obj_clear_flag( Container29, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_pad_row(Container29, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_column(Container29, 4, LV_PART_MAIN| LV_STATE_DEFAULT);

inverterPowerBar1 = lv_bar_create(Container29);
lv_bar_set_range(inverterPowerBar1, 0,2400);
lv_bar_set_value(inverterPowerBar1,1626,LV_ANIM_OFF);
lv_bar_set_start_value(inverterPowerBar1, 0, LV_ANIM_OFF);
lv_obj_set_height( inverterPowerBar1, 10);
lv_obj_set_width( inverterPowerBar1, lv_pct(46));
lv_obj_set_x( inverterPowerBar1, -280 );
lv_obj_set_y( inverterPowerBar1, -54 );
lv_obj_set_align( inverterPowerBar1, LV_ALIGN_CENTER );
ui_object_set_themeable_style_property(inverterPowerBar1, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BG_COLOR, _ui_theme_color_inverterColor);
ui_object_set_themeable_style_property(inverterPowerBar1, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BG_OPA, _ui_theme_alpha_inverterColor);

lv_obj_set_style_bg_color(inverterPowerBar1, lv_color_hex(0x000000), LV_PART_INDICATOR | LV_STATE_DEFAULT );
lv_obj_set_style_bg_opa(inverterPowerBar1, 255, LV_PART_INDICATOR| LV_STATE_DEFAULT);

inverterPowerL1Label = lv_label_create(Container29);
lv_obj_set_height( inverterPowerL1Label, LV_SIZE_CONTENT);   /// 1
lv_obj_set_flex_grow( inverterPowerL1Label, 1);
lv_obj_set_align( inverterPowerL1Label, LV_ALIGN_CENTER );
lv_label_set_text(inverterPowerL1Label,"1625W");
lv_obj_set_style_text_align(inverterPowerL1Label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(inverterPowerL1Label, &ui_font_OpenSansSmall, LV_PART_MAIN| LV_STATE_DEFAULT);

Container3 = lv_obj_create(inverterPhasesContainer);
lv_obj_remove_style_all(Container3);
lv_obj_set_width( Container3, lv_pct(100));
lv_obj_set_height( Container3, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( Container3, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(Container3,LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(Container3, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
lv_obj_clear_flag( Container3, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_pad_row(Container3, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_column(Container3, 4, LV_PART_MAIN| LV_STATE_DEFAULT);

inverterPowerBar2 = lv_bar_create(Container3);
lv_bar_set_range(inverterPowerBar2, 0,2400);
lv_bar_set_value(inverterPowerBar2,25,LV_ANIM_OFF);
lv_bar_set_start_value(inverterPowerBar2, 0, LV_ANIM_OFF);
lv_obj_set_height( inverterPowerBar2, 10);
lv_obj_set_width( inverterPowerBar2, lv_pct(46));
lv_obj_set_align( inverterPowerBar2, LV_ALIGN_CENTER );
ui_object_set_themeable_style_property(inverterPowerBar2, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BG_COLOR, _ui_theme_color_inverterColor);
ui_object_set_themeable_style_property(inverterPowerBar2, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BG_OPA, _ui_theme_alpha_inverterColor);

inverterPowerL2Label = lv_label_create(Container3);
lv_obj_set_height( inverterPowerL2Label, LV_SIZE_CONTENT);   /// 1
lv_obj_set_flex_grow( inverterPowerL2Label, 1);
lv_obj_set_align( inverterPowerL2Label, LV_ALIGN_CENTER );
lv_label_set_text(inverterPowerL2Label,"123W");
lv_obj_set_style_text_align(inverterPowerL2Label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(inverterPowerL2Label, &ui_font_OpenSansSmall, LV_PART_MAIN| LV_STATE_DEFAULT);

Container28 = lv_obj_create(inverterPhasesContainer);
lv_obj_remove_style_all(Container28);
lv_obj_set_width( Container28, lv_pct(100));
lv_obj_set_height( Container28, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( Container28, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(Container28,LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(Container28, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
lv_obj_clear_flag( Container28, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_pad_row(Container28, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_column(Container28, 4, LV_PART_MAIN| LV_STATE_DEFAULT);

inverterPowerBar3 = lv_bar_create(Container28);
lv_bar_set_range(inverterPowerBar3, 0,2400);
lv_bar_set_value(inverterPowerBar3,25,LV_ANIM_OFF);
lv_bar_set_start_value(inverterPowerBar3, 0, LV_ANIM_OFF);
lv_obj_set_height( inverterPowerBar3, 10);
lv_obj_set_width( inverterPowerBar3, lv_pct(46));
lv_obj_set_align( inverterPowerBar3, LV_ALIGN_CENTER );
ui_object_set_themeable_style_property(inverterPowerBar3, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BG_COLOR, _ui_theme_color_inverterColor);
ui_object_set_themeable_style_property(inverterPowerBar3, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BG_OPA, _ui_theme_alpha_inverterColor);

inverterPowerL3Label = lv_label_create(Container28);
lv_obj_set_height( inverterPowerL3Label, LV_SIZE_CONTENT);   /// 1
lv_obj_set_flex_grow( inverterPowerL3Label, 1);
lv_obj_set_align( inverterPowerL3Label, LV_ALIGN_CENTER );
lv_label_set_text(inverterPowerL3Label,"315W");
lv_obj_set_style_text_align(inverterPowerL3Label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(inverterPowerL3Label, &ui_font_OpenSansSmall, LV_PART_MAIN| LV_STATE_DEFAULT);

statusLabel = lv_label_create(inverterContainer);
lv_obj_set_width( statusLabel, LV_SIZE_CONTENT);  /// 100
lv_obj_set_height( statusLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( statusLabel, LV_ALIGN_BOTTOM_MID );
lv_obj_set_style_text_font(statusLabel, &ui_font_OpenSansExtraSmall, LV_PART_MAIN| LV_STATE_DEFAULT);

wallboxContainer = lv_obj_create(LeftContainer);
lv_obj_remove_style_all(wallboxContainer);
lv_obj_set_width( wallboxContainer, lv_pct(34));
lv_obj_set_height( wallboxContainer, LV_SIZE_CONTENT);   /// 28
lv_obj_set_x( wallboxContainer, -1 );
lv_obj_set_y( wallboxContainer, lv_pct(3) );
lv_obj_set_align( wallboxContainer, LV_ALIGN_BOTTOM_MID );
lv_obj_set_flex_flow(wallboxContainer,LV_FLEX_FLOW_COLUMN);
lv_obj_set_flex_align(wallboxContainer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
lv_obj_add_flag( wallboxContainer, LV_OBJ_FLAG_OVERFLOW_VISIBLE );   /// Flags
lv_obj_clear_flag( wallboxContainer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_radius(wallboxContainer, 16, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_bg_color(wallboxContainer, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_bg_opa(wallboxContainer, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
ui_object_set_themeable_style_property(wallboxContainer, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BORDER_COLOR, _ui_theme_color_loadColor);
ui_object_set_themeable_style_property(wallboxContainer, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BORDER_OPA, _ui_theme_alpha_loadColor);
ui_object_set_themeable_style_property(wallboxContainer, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_SHADOW_COLOR, _ui_theme_color_loadColor);
ui_object_set_themeable_style_property(wallboxContainer, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_SHADOW_OPA, _ui_theme_alpha_loadColor);
lv_obj_set_style_shadow_width(wallboxContainer, 48, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_spread(wallboxContainer, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_ofs_x(wallboxContainer, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_ofs_y(wallboxContainer, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_left(wallboxContainer, 12, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_right(wallboxContainer, 12, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_top(wallboxContainer, 12, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_bottom(wallboxContainer, 12, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_row(wallboxContainer, 8, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_column(wallboxContainer, 0, LV_PART_MAIN| LV_STATE_DEFAULT);

wallboxTemperatureLabel = lv_label_create(wallboxContainer);
lv_obj_set_width( wallboxTemperatureLabel, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( wallboxTemperatureLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_x( wallboxTemperatureLabel, 16 );
lv_obj_set_y( wallboxTemperatureLabel, -28 );
lv_obj_set_align( wallboxTemperatureLabel, LV_ALIGN_TOP_RIGHT );
lv_label_set_text(wallboxTemperatureLabel,"56°C");
lv_obj_add_flag( wallboxTemperatureLabel, LV_OBJ_FLAG_IGNORE_LAYOUT );   /// Flags
lv_obj_set_style_text_align(wallboxTemperatureLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(wallboxTemperatureLabel, &ui_font_OpenSansSmall, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_radius(wallboxTemperatureLabel, 8, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_bg_color(wallboxTemperatureLabel, lv_color_hex(0xFFAA00), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_bg_opa(wallboxTemperatureLabel, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_color(wallboxTemperatureLabel, lv_color_hex(0xFFAA00), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_shadow_opa(wallboxTemperatureLabel, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_width(wallboxTemperatureLabel, 32, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_spread(wallboxTemperatureLabel, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_left(wallboxTemperatureLabel, 4, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_right(wallboxTemperatureLabel, 4, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_top(wallboxTemperatureLabel, 4, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_bottom(wallboxTemperatureLabel, 4, LV_PART_MAIN| LV_STATE_DEFAULT);

wallboxLogoSolaxImage = lv_img_create(wallboxContainer);
lv_img_set_src(wallboxLogoSolaxImage, &ui_img_solax_png);
lv_obj_set_width( wallboxLogoSolaxImage, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( wallboxLogoSolaxImage, LV_SIZE_CONTENT);   /// 1
lv_obj_set_x( wallboxLogoSolaxImage, -43 );
lv_obj_set_y( wallboxLogoSolaxImage, 36 );
lv_obj_add_flag( wallboxLogoSolaxImage, LV_OBJ_FLAG_ADV_HITTEST );   /// Flags
lv_obj_clear_flag( wallboxLogoSolaxImage, LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags

wallboxLogoEcovolterImage = lv_img_create(wallboxContainer);
lv_img_set_src(wallboxLogoEcovolterImage, &ui_img_ecovolter3_png);
lv_obj_set_width( wallboxLogoEcovolterImage, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( wallboxLogoEcovolterImage, LV_SIZE_CONTENT);   /// 1
lv_obj_set_x( wallboxLogoEcovolterImage, -43 );
lv_obj_set_y( wallboxLogoEcovolterImage, 36 );
lv_obj_add_flag( wallboxLogoEcovolterImage, LV_OBJ_FLAG_ADV_HITTEST );   /// Flags
lv_obj_clear_flag( wallboxLogoEcovolterImage, LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags

wallboxPowerContainer = lv_obj_create(wallboxContainer);
lv_obj_remove_style_all(wallboxPowerContainer);
lv_obj_set_width( wallboxPowerContainer, lv_pct(100));
lv_obj_set_height( wallboxPowerContainer, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( wallboxPowerContainer, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(wallboxPowerContainer,LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(wallboxPowerContainer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
lv_obj_add_flag( wallboxPowerContainer, LV_OBJ_FLAG_OVERFLOW_VISIBLE );   /// Flags
lv_obj_clear_flag( wallboxPowerContainer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_pad_row(wallboxPowerContainer, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_column(wallboxPowerContainer, 4, LV_PART_MAIN| LV_STATE_DEFAULT);

wallboxPowerLabel = lv_label_create(wallboxPowerContainer);
lv_obj_set_width( wallboxPowerLabel, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( wallboxPowerLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( wallboxPowerLabel, LV_ALIGN_CENTER );
lv_label_set_text(wallboxPowerLabel,"5600");
lv_obj_set_style_text_align(wallboxPowerLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(wallboxPowerLabel, &ui_font_OpenSansLargeBold, LV_PART_MAIN| LV_STATE_DEFAULT);

wallboxPowerUnitLabel = lv_label_create(wallboxPowerContainer);
lv_obj_set_width( wallboxPowerUnitLabel, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( wallboxPowerUnitLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( wallboxPowerUnitLabel, LV_ALIGN_CENTER );
lv_label_set_text(wallboxPowerUnitLabel,"W\n");
lv_obj_set_style_text_font(wallboxPowerUnitLabel, &ui_font_OpenSansSmall, LV_PART_MAIN| LV_STATE_DEFAULT);

wallboxEnergyContainer = lv_obj_create(wallboxContainer);
lv_obj_remove_style_all(wallboxEnergyContainer);
lv_obj_set_width( wallboxEnergyContainer, lv_pct(100));
lv_obj_set_height( wallboxEnergyContainer, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( wallboxEnergyContainer, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(wallboxEnergyContainer,LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(wallboxEnergyContainer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
lv_obj_add_flag( wallboxEnergyContainer, LV_OBJ_FLAG_OVERFLOW_VISIBLE );   /// Flags
lv_obj_clear_flag( wallboxEnergyContainer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_pad_row(wallboxEnergyContainer, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_column(wallboxEnergyContainer, 4, LV_PART_MAIN| LV_STATE_DEFAULT);

wallboxEnergyLabel = lv_label_create(wallboxEnergyContainer);
lv_obj_set_width( wallboxEnergyLabel, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( wallboxEnergyLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( wallboxEnergyLabel, LV_ALIGN_CENTER );
lv_label_set_text(wallboxEnergyLabel,"+ 38kWh");
lv_obj_set_style_text_align(wallboxEnergyLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(wallboxEnergyLabel, &ui_font_OpenSansMedium, LV_PART_MAIN| LV_STATE_DEFAULT);

wallboxSmartCheckbox = lv_checkbox_create(wallboxContainer);
lv_checkbox_set_text(wallboxSmartCheckbox,"SMART");
lv_obj_set_width( wallboxSmartCheckbox, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( wallboxSmartCheckbox, LV_SIZE_CONTENT);   /// 1
lv_obj_set_x( wallboxSmartCheckbox, 2 );
lv_obj_set_y( wallboxSmartCheckbox, 0 );
lv_obj_set_align( wallboxSmartCheckbox, LV_ALIGN_CENTER );
lv_obj_add_flag( wallboxSmartCheckbox, LV_OBJ_FLAG_SCROLL_ON_FOCUS );   /// Flags
lv_obj_set_style_text_letter_space(wallboxSmartCheckbox, 2, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_line_space(wallboxSmartCheckbox, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(wallboxSmartCheckbox, &ui_font_OpenSansSmall, LV_PART_MAIN| LV_STATE_DEFAULT);

ui_object_set_themeable_style_property(wallboxSmartCheckbox, LV_PART_INDICATOR| LV_STATE_DEFAULT, LV_STYLE_BORDER_COLOR, _ui_theme_color_loadColor);
ui_object_set_themeable_style_property(wallboxSmartCheckbox, LV_PART_INDICATOR| LV_STATE_DEFAULT, LV_STYLE_BORDER_OPA, _ui_theme_alpha_loadColor);
ui_object_set_themeable_style_property(wallboxSmartCheckbox, LV_PART_INDICATOR| LV_STATE_CHECKED, LV_STYLE_BG_COLOR, _ui_theme_color_loadColor);
ui_object_set_themeable_style_property(wallboxSmartCheckbox, LV_PART_INDICATOR| LV_STATE_CHECKED, LV_STYLE_BG_OPA, _ui_theme_alpha_loadColor);

RightContainer = lv_obj_create(screen);
lv_obj_remove_style_all(RightContainer);
lv_obj_set_height( RightContainer, lv_pct(100));
lv_obj_set_flex_grow( RightContainer, 1);
lv_obj_set_x( RightContainer, -1 );
lv_obj_set_y( RightContainer, 0 );
lv_obj_set_align( RightContainer, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(RightContainer,LV_FLEX_FLOW_COLUMN);
lv_obj_set_flex_align(RightContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
lv_obj_add_flag( RightContainer, LV_OBJ_FLAG_OVERFLOW_VISIBLE );   /// Flags
lv_obj_clear_flag( RightContainer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_pad_row(RightContainer, 8, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_column(RightContainer, 0, LV_PART_MAIN| LV_STATE_DEFAULT);

TopRightContainer = lv_obj_create(RightContainer);
lv_obj_remove_style_all(TopRightContainer);
lv_obj_set_width( TopRightContainer, lv_pct(100));
lv_obj_set_height( TopRightContainer, LV_SIZE_CONTENT);   /// 50
lv_obj_set_align( TopRightContainer, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(TopRightContainer,LV_FLEX_FLOW_COLUMN);
lv_obj_set_flex_align(TopRightContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
lv_obj_add_flag( TopRightContainer, LV_OBJ_FLAG_OVERFLOW_VISIBLE );   /// Flags
lv_obj_clear_flag( TopRightContainer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_pad_row(TopRightContainer, 8, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_column(TopRightContainer, 8, LV_PART_MAIN| LV_STATE_DEFAULT);

Container33 = lv_obj_create(TopRightContainer);
lv_obj_remove_style_all(Container33);
lv_obj_set_width( Container33, lv_pct(100));
lv_obj_set_height( Container33, LV_SIZE_CONTENT);   /// 150
lv_obj_set_align( Container33, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(Container33,LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(Container33, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
lv_obj_add_flag( Container33, LV_OBJ_FLAG_OVERFLOW_VISIBLE );   /// Flags
lv_obj_clear_flag( Container33, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_pad_row(Container33, 8, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_column(Container33, 8, LV_PART_MAIN| LV_STATE_DEFAULT);

pvStatsContainer = lv_obj_create(Container33);
lv_obj_remove_style_all(pvStatsContainer);
lv_obj_set_height( pvStatsContainer, 88);
lv_obj_set_flex_grow( pvStatsContainer, 1);
lv_obj_set_flex_flow(pvStatsContainer,LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(pvStatsContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
lv_obj_clear_flag( pvStatsContainer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_radius(pvStatsContainer, 24, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_bg_color(pvStatsContainer, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_bg_opa(pvStatsContainer, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_border_side(pvStatsContainer, LV_BORDER_SIDE_FULL, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_color(pvStatsContainer, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_shadow_opa(pvStatsContainer, 64, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_width(pvStatsContainer, 32, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_spread(pvStatsContainer, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_left(pvStatsContainer, 12, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_right(pvStatsContainer, 12, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_top(pvStatsContainer, 12, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_bottom(pvStatsContainer, 12, LV_PART_MAIN| LV_STATE_DEFAULT);

Image11 = lv_img_create(pvStatsContainer);
lv_img_set_src(Image11, &ui_img_1516017106);
lv_obj_set_width( Image11, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( Image11, LV_SIZE_CONTENT);   /// 1
lv_obj_set_x( Image11, -141 );
lv_obj_set_y( Image11, -169 );
lv_obj_set_align( Image11, LV_ALIGN_CENTER );
lv_obj_add_flag( Image11, LV_OBJ_FLAG_ADV_HITTEST );   /// Flags
lv_obj_clear_flag( Image11, LV_OBJ_FLAG_SCROLLABLE );    /// Flags

Container34 = lv_obj_create(pvStatsContainer);
lv_obj_remove_style_all(Container34);
lv_obj_set_height( Container34, LV_SIZE_CONTENT);   /// 50
lv_obj_set_flex_grow( Container34, 1);
lv_obj_set_align( Container34, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(Container34,LV_FLEX_FLOW_COLUMN);
lv_obj_set_flex_align(Container34, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
lv_obj_clear_flag( Container34, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags

Container27 = lv_obj_create(Container34);
lv_obj_remove_style_all(Container27);
lv_obj_set_width( Container27, lv_pct(100));
lv_obj_set_height( Container27, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( Container27, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(Container27,LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(Container27, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
lv_obj_clear_flag( Container27, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_pad_row(Container27, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_column(Container27, 4, LV_PART_MAIN| LV_STATE_DEFAULT);

yieldTodayLabel = lv_label_create(Container27);
lv_obj_set_height( yieldTodayLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_flex_grow( yieldTodayLabel, 3);
lv_obj_set_align( yieldTodayLabel, LV_ALIGN_CENTER );
lv_label_set_long_mode(yieldTodayLabel,LV_LABEL_LONG_SCROLL);
lv_label_set_text(yieldTodayLabel,"65");
lv_obj_set_style_text_align(yieldTodayLabel, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(yieldTodayLabel, &ui_font_OpenSansLargeBold, LV_PART_MAIN| LV_STATE_DEFAULT);

yieldTodayUnitLabel = lv_label_create(Container27);
lv_obj_set_height( yieldTodayUnitLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_flex_grow( yieldTodayUnitLabel, 1);
lv_obj_set_align( yieldTodayUnitLabel, LV_ALIGN_CENTER );
lv_label_set_text(yieldTodayUnitLabel,"kWh");
lv_obj_set_style_text_align(yieldTodayUnitLabel, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(yieldTodayUnitLabel, &ui_font_OpenSansExtraSmall, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_bottom(yieldTodayUnitLabel, 6, LV_PART_MAIN| LV_STATE_DEFAULT);

Container5 = lv_obj_create(Container34);
lv_obj_remove_style_all(Container5);
lv_obj_set_width( Container5, lv_pct(100));
lv_obj_set_height( Container5, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( Container5, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(Container5,LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(Container5, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
lv_obj_clear_flag( Container5, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_pad_row(Container5, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_column(Container5, 4, LV_PART_MAIN| LV_STATE_DEFAULT);

yieldTotalLabel = lv_label_create(Container5);
lv_obj_set_height( yieldTotalLabel, LV_SIZE_CONTENT);   /// 100
lv_obj_set_flex_grow( yieldTotalLabel, 3);
lv_obj_set_align( yieldTotalLabel, LV_ALIGN_CENTER );
lv_label_set_long_mode(yieldTotalLabel,LV_LABEL_LONG_SCROLL);
lv_label_set_text(yieldTotalLabel,"1,1");
lv_obj_set_style_text_align(yieldTotalLabel, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(yieldTotalLabel, &ui_font_OpenSansMediumBold, LV_PART_MAIN| LV_STATE_DEFAULT);

yieldTotalUnitLabel = lv_label_create(Container5);
lv_obj_set_height( yieldTotalUnitLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_flex_grow( yieldTotalUnitLabel, 1);
lv_obj_set_align( yieldTotalUnitLabel, LV_ALIGN_CENTER );
lv_label_set_text(yieldTotalUnitLabel,"MWh");
lv_obj_set_style_text_align(yieldTotalUnitLabel, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(yieldTotalUnitLabel, &ui_font_OpenSansExtraSmall, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_bottom(yieldTotalUnitLabel, 3, LV_PART_MAIN| LV_STATE_DEFAULT);

loadStatsContainer = lv_obj_create(Container33);
lv_obj_remove_style_all(loadStatsContainer);
lv_obj_set_height( loadStatsContainer, 88);
lv_obj_set_flex_grow( loadStatsContainer, 1);
lv_obj_set_x( loadStatsContainer, lv_pct(1) );
lv_obj_set_y( loadStatsContainer, lv_pct(0) );
lv_obj_set_flex_flow(loadStatsContainer,LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(loadStatsContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
lv_obj_clear_flag( loadStatsContainer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_radius(loadStatsContainer, 24, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_bg_color(loadStatsContainer, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_bg_opa(loadStatsContainer, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_color(loadStatsContainer, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_shadow_opa(loadStatsContainer, 64, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_width(loadStatsContainer, 32, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_spread(loadStatsContainer, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_left(loadStatsContainer, 12, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_right(loadStatsContainer, 12, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_top(loadStatsContainer, 12, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_bottom(loadStatsContainer, 12, LV_PART_MAIN| LV_STATE_DEFAULT);

Image13 = lv_img_create(loadStatsContainer);
lv_img_set_src(Image13, &ui_img_564643105);
lv_obj_set_width( Image13, LV_SIZE_CONTENT);  /// 48
lv_obj_set_height( Image13, LV_SIZE_CONTENT);   /// 48
lv_obj_set_x( Image13, -141 );
lv_obj_set_y( Image13, -169 );
lv_obj_set_align( Image13, LV_ALIGN_CENTER );
lv_obj_add_flag( Image13, LV_OBJ_FLAG_ADV_HITTEST );   /// Flags
lv_obj_clear_flag( Image13, LV_OBJ_FLAG_SCROLLABLE );    /// Flags

Container37 = lv_obj_create(loadStatsContainer);
lv_obj_remove_style_all(Container37);
lv_obj_set_height( Container37, LV_SIZE_CONTENT);   /// 50
lv_obj_set_flex_grow( Container37, 1);
lv_obj_set_align( Container37, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(Container37,LV_FLEX_FLOW_COLUMN);
lv_obj_set_flex_align(Container37, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
lv_obj_clear_flag( Container37, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags

Container8 = lv_obj_create(Container37);
lv_obj_remove_style_all(Container8);
lv_obj_set_width( Container8, lv_pct(100));
lv_obj_set_height( Container8, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( Container8, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(Container8,LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(Container8, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
lv_obj_clear_flag( Container8, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_pad_row(Container8, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_column(Container8, 4, LV_PART_MAIN| LV_STATE_DEFAULT);

loadTodayLabel = lv_label_create(Container8);
lv_obj_set_height( loadTodayLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_flex_grow( loadTodayLabel, 3);
lv_obj_set_align( loadTodayLabel, LV_ALIGN_CENTER );
lv_label_set_long_mode(loadTodayLabel,LV_LABEL_LONG_SCROLL);
lv_label_set_text(loadTodayLabel,"11");
lv_obj_set_style_text_align(loadTodayLabel, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(loadTodayLabel, &ui_font_OpenSansLargeBold, LV_PART_MAIN| LV_STATE_DEFAULT);

loadTodayUnitLabel = lv_label_create(Container8);
lv_obj_set_height( loadTodayUnitLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_flex_grow( loadTodayUnitLabel, 1);
lv_obj_set_align( loadTodayUnitLabel, LV_ALIGN_CENTER );
lv_label_set_text(loadTodayUnitLabel,"kWh");
lv_obj_set_style_text_align(loadTodayUnitLabel, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(loadTodayUnitLabel, &ui_font_OpenSansExtraSmall, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_bottom(loadTodayUnitLabel, 6, LV_PART_MAIN| LV_STATE_DEFAULT);

Container30 = lv_obj_create(Container37);
lv_obj_remove_style_all(Container30);
lv_obj_set_width( Container30, lv_pct(100));
lv_obj_set_height( Container30, LV_SIZE_CONTENT);   /// 1
lv_obj_set_x( Container30, 0 );
lv_obj_set_y( Container30, -1 );
lv_obj_set_align( Container30, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(Container30,LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(Container30, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
lv_obj_clear_flag( Container30, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_pad_row(Container30, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_column(Container30, 4, LV_PART_MAIN| LV_STATE_DEFAULT);

selfUseTodayLabel = lv_label_create(Container30);
lv_obj_set_height( selfUseTodayLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_flex_grow( selfUseTodayLabel, 3);
lv_obj_set_align( selfUseTodayLabel, LV_ALIGN_CENTER );
lv_label_set_long_mode(selfUseTodayLabel,LV_LABEL_LONG_SCROLL);
lv_label_set_text(selfUseTodayLabel,"86");
ui_object_set_themeable_style_property(selfUseTodayLabel, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_TEXT_COLOR, _ui_theme_color_loadColor);
ui_object_set_themeable_style_property(selfUseTodayLabel, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_TEXT_OPA, _ui_theme_alpha_loadColor);
lv_obj_set_style_text_align(selfUseTodayLabel, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(selfUseTodayLabel, &ui_font_OpenSansMediumBold, LV_PART_MAIN| LV_STATE_DEFAULT);

selfUseTodayUnitLabel = lv_label_create(Container30);
lv_obj_set_height( selfUseTodayUnitLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_flex_grow( selfUseTodayUnitLabel, 1);
lv_obj_set_align( selfUseTodayUnitLabel, LV_ALIGN_CENTER );
lv_label_set_text(selfUseTodayUnitLabel,"%");
ui_object_set_themeable_style_property(selfUseTodayUnitLabel, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_TEXT_COLOR, _ui_theme_color_loadColor);
ui_object_set_themeable_style_property(selfUseTodayUnitLabel, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_TEXT_OPA, _ui_theme_alpha_loadColor);
lv_obj_set_style_text_align(selfUseTodayUnitLabel, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(selfUseTodayUnitLabel, &ui_font_OpenSansExtraSmall, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_bottom(selfUseTodayUnitLabel, 5, LV_PART_MAIN| LV_STATE_DEFAULT);

Container36 = lv_obj_create(TopRightContainer);
lv_obj_remove_style_all(Container36);
lv_obj_set_width( Container36, lv_pct(100));
lv_obj_set_height( Container36, LV_SIZE_CONTENT);   /// 50
lv_obj_set_align( Container36, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(Container36,LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(Container36, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
lv_obj_add_flag( Container36, LV_OBJ_FLAG_OVERFLOW_VISIBLE );   /// Flags
lv_obj_clear_flag( Container36, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_pad_row(Container36, 8, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_column(Container36, 8, LV_PART_MAIN| LV_STATE_DEFAULT);

batteryStatsContainer = lv_obj_create(Container36);
lv_obj_remove_style_all(batteryStatsContainer);
lv_obj_set_height( batteryStatsContainer, 88);
lv_obj_set_flex_grow( batteryStatsContainer, 1);
lv_obj_set_flex_flow(batteryStatsContainer,LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(batteryStatsContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
lv_obj_clear_flag( batteryStatsContainer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_radius(batteryStatsContainer, 24, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_bg_color(batteryStatsContainer, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_bg_opa(batteryStatsContainer, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_color(batteryStatsContainer, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_shadow_opa(batteryStatsContainer, 64, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_width(batteryStatsContainer, 32, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_spread(batteryStatsContainer, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_left(batteryStatsContainer, 12, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_right(batteryStatsContainer, 12, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_top(batteryStatsContainer, 12, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_bottom(batteryStatsContainer, 12, LV_PART_MAIN| LV_STATE_DEFAULT);

Image12 = lv_img_create(batteryStatsContainer);
lv_img_set_src(Image12, &ui_img_performance_png);
lv_obj_set_width( Image12, LV_SIZE_CONTENT);  /// 48
lv_obj_set_height( Image12, LV_SIZE_CONTENT);   /// 48
lv_obj_set_x( Image12, -141 );
lv_obj_set_y( Image12, -169 );
lv_obj_set_align( Image12, LV_ALIGN_CENTER );
lv_obj_add_flag( Image12, LV_OBJ_FLAG_ADV_HITTEST );   /// Flags
lv_obj_clear_flag( Image12, LV_OBJ_FLAG_SCROLLABLE );    /// Flags

Container35 = lv_obj_create(batteryStatsContainer);
lv_obj_remove_style_all(Container35);
lv_obj_set_height( Container35, LV_SIZE_CONTENT);   /// 50
lv_obj_set_flex_grow( Container35, 1);
lv_obj_set_align( Container35, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(Container35,LV_FLEX_FLOW_COLUMN);
lv_obj_set_flex_align(Container35, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
lv_obj_clear_flag( Container35, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags

Container6 = lv_obj_create(Container35);
lv_obj_remove_style_all(Container6);
lv_obj_set_width( Container6, lv_pct(100));
lv_obj_set_height( Container6, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( Container6, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(Container6,LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(Container6, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
lv_obj_clear_flag( Container6, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_pad_row(Container6, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_column(Container6, 4, LV_PART_MAIN| LV_STATE_DEFAULT);

batteryChargedTodayLabel = lv_label_create(Container6);
lv_obj_set_height( batteryChargedTodayLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_flex_grow( batteryChargedTodayLabel, 3);
lv_obj_set_align( batteryChargedTodayLabel, LV_ALIGN_CENTER );
lv_label_set_long_mode(batteryChargedTodayLabel,LV_LABEL_LONG_SCROLL);
lv_label_set_text(batteryChargedTodayLabel,"+9");
lv_obj_set_style_text_align(batteryChargedTodayLabel, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(batteryChargedTodayLabel, &ui_font_OpenSansMediumBold, LV_PART_MAIN| LV_STATE_DEFAULT);

batteryChargedTodayUnitLabel = lv_label_create(Container6);
lv_obj_set_height( batteryChargedTodayUnitLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_flex_grow( batteryChargedTodayUnitLabel, 1);
lv_obj_set_align( batteryChargedTodayUnitLabel, LV_ALIGN_CENTER );
lv_label_set_long_mode(batteryChargedTodayUnitLabel,LV_LABEL_LONG_DOT);
lv_label_set_text(batteryChargedTodayUnitLabel,"kWh");
lv_obj_set_style_text_align(batteryChargedTodayUnitLabel, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(batteryChargedTodayUnitLabel, &ui_font_OpenSansExtraSmall, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_bottom(batteryChargedTodayUnitLabel, 3, LV_PART_MAIN| LV_STATE_DEFAULT);

Container9 = lv_obj_create(Container35);
lv_obj_remove_style_all(Container9);
lv_obj_set_width( Container9, lv_pct(100));
lv_obj_set_height( Container9, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( Container9, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(Container9,LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(Container9, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
lv_obj_clear_flag( Container9, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_pad_row(Container9, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_column(Container9, 4, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_color(Container9, lv_color_hex(0xCD0000), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_text_opa(Container9, 255, LV_PART_MAIN| LV_STATE_DEFAULT);

batteryDischargedTodayLabel = lv_label_create(Container9);
lv_obj_set_height( batteryDischargedTodayLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_flex_grow( batteryDischargedTodayLabel, 3);
lv_obj_set_align( batteryDischargedTodayLabel, LV_ALIGN_CENTER );
lv_label_set_long_mode(batteryDischargedTodayLabel,LV_LABEL_LONG_SCROLL);
lv_label_set_text(batteryDischargedTodayLabel,"-3");
lv_obj_set_style_text_align(batteryDischargedTodayLabel, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(batteryDischargedTodayLabel, &ui_font_OpenSansMediumBold, LV_PART_MAIN| LV_STATE_DEFAULT);

batteryDischargedTodayUnitLabel = lv_label_create(Container9);
lv_obj_set_height( batteryDischargedTodayUnitLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_flex_grow( batteryDischargedTodayUnitLabel, 1);
lv_obj_set_align( batteryDischargedTodayUnitLabel, LV_ALIGN_CENTER );
lv_label_set_long_mode(batteryDischargedTodayUnitLabel,LV_LABEL_LONG_DOT);
lv_label_set_text(batteryDischargedTodayUnitLabel,"kWh");
lv_obj_set_style_text_align(batteryDischargedTodayUnitLabel, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(batteryDischargedTodayUnitLabel, &ui_font_OpenSansExtraSmall, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_bottom(batteryDischargedTodayUnitLabel, 3, LV_PART_MAIN| LV_STATE_DEFAULT);

gridStatsContainer = lv_obj_create(Container36);
lv_obj_remove_style_all(gridStatsContainer);
lv_obj_set_height( gridStatsContainer, 88);
lv_obj_set_flex_grow( gridStatsContainer, 1);
lv_obj_set_flex_flow(gridStatsContainer,LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(gridStatsContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
lv_obj_clear_flag( gridStatsContainer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_radius(gridStatsContainer, 24, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_bg_color(gridStatsContainer, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_bg_opa(gridStatsContainer, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_color(gridStatsContainer, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_shadow_opa(gridStatsContainer, 64, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_width(gridStatsContainer, 32, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_spread(gridStatsContainer, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_left(gridStatsContainer, 12, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_right(gridStatsContainer, 12, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_top(gridStatsContainer, 12, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_bottom(gridStatsContainer, 12, LV_PART_MAIN| LV_STATE_DEFAULT);

Image14 = lv_img_create(gridStatsContainer);
lv_img_set_src(Image14, &ui_img_960241876);
lv_obj_set_width( Image14, LV_SIZE_CONTENT);  /// 48
lv_obj_set_height( Image14, LV_SIZE_CONTENT);   /// 48
lv_obj_set_x( Image14, -141 );
lv_obj_set_y( Image14, -169 );
lv_obj_set_align( Image14, LV_ALIGN_CENTER );
lv_obj_add_flag( Image14, LV_OBJ_FLAG_ADV_HITTEST );   /// Flags
lv_obj_clear_flag( Image14, LV_OBJ_FLAG_SCROLLABLE );    /// Flags

Container38 = lv_obj_create(gridStatsContainer);
lv_obj_remove_style_all(Container38);
lv_obj_set_height( Container38, LV_SIZE_CONTENT);   /// 50
lv_obj_set_flex_grow( Container38, 1);
lv_obj_set_align( Container38, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(Container38,LV_FLEX_FLOW_COLUMN);
lv_obj_set_flex_align(Container38, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
lv_obj_clear_flag( Container38, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags

Container7 = lv_obj_create(Container38);
lv_obj_remove_style_all(Container7);
lv_obj_set_width( Container7, lv_pct(100));
lv_obj_set_height( Container7, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( Container7, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(Container7,LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(Container7, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
lv_obj_clear_flag( Container7, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_pad_row(Container7, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_column(Container7, 4, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(Container7, &ui_font_OpenSansSmall, LV_PART_MAIN| LV_STATE_DEFAULT);

gridSellTodayLabel = lv_label_create(Container7);
lv_obj_set_height( gridSellTodayLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_flex_grow( gridSellTodayLabel, 3);
lv_obj_set_align( gridSellTodayLabel, LV_ALIGN_CENTER );
lv_label_set_long_mode(gridSellTodayLabel,LV_LABEL_LONG_SCROLL);
lv_label_set_text(gridSellTodayLabel,"+45");
lv_obj_set_style_text_align(gridSellTodayLabel, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(gridSellTodayLabel, &ui_font_OpenSansMediumBold, LV_PART_MAIN| LV_STATE_DEFAULT);

gridSellTodayUnitLabel = lv_label_create(Container7);
lv_obj_set_height( gridSellTodayUnitLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_flex_grow( gridSellTodayUnitLabel, 1);
lv_obj_set_align( gridSellTodayUnitLabel, LV_ALIGN_CENTER );
lv_label_set_text(gridSellTodayUnitLabel,"kWh");
lv_obj_set_style_text_align(gridSellTodayUnitLabel, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(gridSellTodayUnitLabel, &ui_font_OpenSansExtraSmall, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_bottom(gridSellTodayUnitLabel, 3, LV_PART_MAIN| LV_STATE_DEFAULT);

Container10 = lv_obj_create(Container38);
lv_obj_remove_style_all(Container10);
lv_obj_set_width( Container10, lv_pct(100));
lv_obj_set_height( Container10, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( Container10, LV_ALIGN_CENTER );
lv_obj_set_flex_flow(Container10,LV_FLEX_FLOW_ROW);
lv_obj_set_flex_align(Container10, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
lv_obj_clear_flag( Container10, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_pad_row(Container10, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_column(Container10, 4, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_color(Container10, lv_color_hex(0xCD0000), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_text_opa(Container10, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(Container10, &lv_font_montserrat_14, LV_PART_MAIN| LV_STATE_DEFAULT);

gridBuyTodayLabel = lv_label_create(Container10);
lv_obj_set_height( gridBuyTodayLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_flex_grow( gridBuyTodayLabel, 3);
lv_obj_set_align( gridBuyTodayLabel, LV_ALIGN_CENTER );
lv_label_set_long_mode(gridBuyTodayLabel,LV_LABEL_LONG_SCROLL);
lv_label_set_text(gridBuyTodayLabel,"-2");
lv_obj_set_style_text_align(gridBuyTodayLabel, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(gridBuyTodayLabel, &ui_font_OpenSansMediumBold, LV_PART_MAIN| LV_STATE_DEFAULT);

gridBuyTodayUnitLabel = lv_label_create(Container10);
lv_obj_set_height( gridBuyTodayUnitLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_flex_grow( gridBuyTodayUnitLabel, 1);
lv_obj_set_align( gridBuyTodayUnitLabel, LV_ALIGN_CENTER );
lv_label_set_text(gridBuyTodayUnitLabel,"kWh");
lv_obj_set_style_text_align(gridBuyTodayUnitLabel, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(gridBuyTodayUnitLabel, &ui_font_OpenSansExtraSmall, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_bottom(gridBuyTodayUnitLabel, 3, LV_PART_MAIN| LV_STATE_DEFAULT);

clocksLabel = lv_label_create(TopRightContainer);
lv_obj_set_width( clocksLabel, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( clocksLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_align( clocksLabel, LV_ALIGN_CENTER );
lv_label_set_text(clocksLabel,"10:32");
lv_obj_add_flag( clocksLabel, LV_OBJ_FLAG_IGNORE_LAYOUT );   /// Flags
lv_obj_set_style_text_font(clocksLabel, &ui_font_OpenSansMediumBold, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_radius(clocksLabel, 12, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_bg_color(clocksLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_bg_opa(clocksLabel, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_color(clocksLabel, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_shadow_opa(clocksLabel, 64, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_width(clocksLabel, 32, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_spread(clocksLabel, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_left(clocksLabel, 6, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_right(clocksLabel, 6, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_top(clocksLabel, 2, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_bottom(clocksLabel, 2, LV_PART_MAIN| LV_STATE_DEFAULT);

RightBottomContainer = lv_obj_create(RightContainer);
lv_obj_remove_style_all(RightBottomContainer);
lv_obj_set_width( RightBottomContainer, lv_pct(100));
lv_obj_set_flex_grow( RightBottomContainer, 1);
lv_obj_set_align( RightBottomContainer, LV_ALIGN_CENTER );
lv_obj_add_flag( RightBottomContainer, LV_OBJ_FLAG_OVERFLOW_VISIBLE );   /// Flags
lv_obj_clear_flag( RightBottomContainer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_radius(RightBottomContainer, 24, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_bg_color(RightBottomContainer, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_bg_opa(RightBottomContainer, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_color(RightBottomContainer, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_shadow_opa(RightBottomContainer, 64, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_width(RightBottomContainer, 32, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_spread(RightBottomContainer, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_left(RightBottomContainer, 48, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_right(RightBottomContainer, 42, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_top(RightBottomContainer, 16, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_bottom(RightBottomContainer, 24, LV_PART_MAIN| LV_STATE_DEFAULT);

Chart1 = lv_chart_create(RightBottomContainer);
lv_obj_set_width( Chart1, lv_pct(100));
lv_obj_set_height( Chart1, lv_pct(100));
lv_obj_set_align( Chart1, LV_ALIGN_CENTER );
lv_obj_add_flag( Chart1, LV_OBJ_FLAG_OVERFLOW_VISIBLE );   /// Flags
lv_chart_set_type( Chart1, LV_CHART_TYPE_LINE);
lv_chart_set_point_count( Chart1, 96);
lv_chart_set_range( Chart1, LV_CHART_AXIS_SECONDARY_Y, 0, 15);
lv_chart_set_div_line_count( Chart1, 5, 5);
lv_chart_set_axis_tick( Chart1, LV_CHART_AXIS_PRIMARY_X, 0, 0, 5, 24, true, 32);
lv_chart_set_axis_tick( Chart1, LV_CHART_AXIS_PRIMARY_Y, 0, 0, 5, 2, true, 32);
lv_chart_set_axis_tick( Chart1, LV_CHART_AXIS_SECONDARY_Y, 0, 0, 3, 2, true, 32);
lv_chart_series_t* ui_Chart1_series_1 = lv_chart_add_series(Chart1, lv_color_hex(0x2EB5FF), LV_CHART_AXIS_PRIMARY_Y);
static lv_coord_t ui_Chart1_series_1_array[] = { 15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,16,18,19,21,22,24,25,27,29,32,35,38,42,45,50,54,59,63,68,72,75,78,81,84,86,88,90,91,92,93,94,94,95,95,96,96,96,96,96,95,95,94,94,93,92,91,90,89,88,87,85,83,81,79,77,75,72,69,66,63,60,57,54,51,48,45,43,41,39,37,35,33,31,29,28,27,26,25,24,23,22,21,20,19,18,17,16,15,14,13,12,11,10,9,8,5,5,5,5,15,15 };
lv_chart_set_ext_y_array(Chart1, ui_Chart1_series_1, ui_Chart1_series_1_array);
lv_obj_set_style_bg_color(Chart1, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_bg_opa(Chart1, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_border_width(Chart1, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_line_width(Chart1, 1, LV_PART_MAIN| LV_STATE_DEFAULT);

lv_obj_set_style_line_width(Chart1, 3, LV_PART_ITEMS| LV_STATE_DEFAULT);
lv_obj_set_style_line_rounded(Chart1, true, LV_PART_ITEMS| LV_STATE_DEFAULT);

lv_obj_set_style_size(Chart1, 0, LV_PART_INDICATOR| LV_STATE_DEFAULT);

lv_obj_set_style_text_color(Chart1, lv_color_hex(0x000000), LV_PART_TICKS | LV_STATE_DEFAULT );
lv_obj_set_style_text_opa(Chart1, 255, LV_PART_TICKS| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(Chart1, &ui_font_OpenSansExtraSmall, LV_PART_TICKS| LV_STATE_DEFAULT);

spotPriceContainer = lv_obj_create(RightContainer);
lv_obj_remove_style_all(spotPriceContainer);
lv_obj_set_width( spotPriceContainer, lv_pct(100));
lv_obj_set_flex_grow( spotPriceContainer, 1);
lv_obj_set_align( spotPriceContainer, LV_ALIGN_CENTER );
lv_obj_add_flag( spotPriceContainer, LV_OBJ_FLAG_OVERFLOW_VISIBLE );   /// Flags
lv_obj_clear_flag( spotPriceContainer, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_radius(spotPriceContainer, 24, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_bg_color(spotPriceContainer, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_bg_opa(spotPriceContainer, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_color(spotPriceContainer, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_shadow_opa(spotPriceContainer, 64, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_width(spotPriceContainer, 32, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_spread(spotPriceContainer, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_left(spotPriceContainer, 1, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_right(spotPriceContainer, 1, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_top(spotPriceContainer, 24, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_bottom(spotPriceContainer, 24, LV_PART_MAIN| LV_STATE_DEFAULT);

currentPriceLabel = lv_label_create(spotPriceContainer);
lv_obj_set_width( currentPriceLabel, LV_SIZE_CONTENT);  /// 1
lv_obj_set_height( currentPriceLabel, LV_SIZE_CONTENT);   /// 1
lv_obj_set_x( currentPriceLabel, lv_pct(0) );
lv_obj_set_y( currentPriceLabel, lv_pct(-25) );
lv_obj_set_align( currentPriceLabel, LV_ALIGN_TOP_MID );
lv_label_set_text(currentPriceLabel,"4,34 CZK");
lv_obj_add_flag( currentPriceLabel, LV_OBJ_FLAG_IGNORE_LAYOUT );   /// Flags
lv_obj_set_style_text_align(currentPriceLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_text_font(currentPriceLabel, &ui_font_OpenSansMedium, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_radius(currentPriceLabel, 8, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_bg_color(currentPriceLabel, lv_color_hex(0xFFAA00), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_bg_opa(currentPriceLabel, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_color(currentPriceLabel, lv_color_hex(0xFFAA00), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_shadow_opa(currentPriceLabel, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_width(currentPriceLabel, 32, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_spread(currentPriceLabel, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_left(currentPriceLabel, 8, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_right(currentPriceLabel, 8, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_top(currentPriceLabel, 4, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_pad_bottom(currentPriceLabel, 4, LV_PART_MAIN| LV_STATE_DEFAULT);

settingsButton = lv_btn_create(screen);
lv_obj_set_width( settingsButton, 64);
lv_obj_set_height( settingsButton, 64);
lv_obj_set_x( settingsButton, lv_pct(-2) );
lv_obj_set_y( settingsButton, lv_pct(-2) );
lv_obj_set_align( settingsButton, LV_ALIGN_BOTTOM_RIGHT );
lv_obj_add_flag( settingsButton, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_SCROLL_ON_FOCUS );   /// Flags
lv_obj_clear_flag( settingsButton, LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
lv_obj_set_style_bg_color(settingsButton, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_bg_opa(settingsButton, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_outline_color(settingsButton, lv_color_hex(0xFFDD00), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_outline_opa(settingsButton, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_outline_width(settingsButton, 3, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_outline_pad(settingsButton, 0, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_color(settingsButton, lv_color_hex(0xFFDD00), LV_PART_MAIN | LV_STATE_DEFAULT );
lv_obj_set_style_shadow_opa(settingsButton, 255, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_width(settingsButton, 32, LV_PART_MAIN| LV_STATE_DEFAULT);
lv_obj_set_style_shadow_spread(settingsButton, 0, LV_PART_MAIN| LV_STATE_DEFAULT);

// Add gear icon label
lv_obj_t* settingsIconLabel = lv_label_create(settingsButton);
lv_label_set_text(settingsIconLabel, LV_SYMBOL_SETTINGS);
lv_obj_set_style_text_font(settingsIconLabel, &lv_font_montserrat_24, 0);
lv_obj_set_style_text_color(settingsIconLabel, lv_color_hex(0xFFDD00), 0);
lv_obj_center(settingsIconLabel);

    }


public:
    const int UI_REFRESH_PERIOD_MS = 5000;
    lv_timer_t *clocksTimer = nullptr;
    lv_obj_t *intelligenceButton = nullptr;  // Intelligence settings button
    bool timeSynced = false;  // Flag to track if time has been synchronized
    
    DashboardUI(void (*onSettingsShow)(lv_event_t *), void (*onIntelligenceShow)(lv_event_t *) = nullptr)
    {
        // Store callbacks for later use in show()
        onSettingsShowCallback = onSettingsShow;
        onIntelligenceShowCallback = onIntelligenceShow;
        
        // Initialize spot chart data
        spotChartData.priceResult = nullptr;
        spotChartData.hasIntelligencePlan = false;
        spotChartData.hasValidPrices = false;
    }

    /**
     * Create intelligence plan tile and setup additional event handlers.
     * Called from show() after createUI() has created all base UI elements.
     */
    void initDashboardExtras() {
        // Register chart draw event handlers
        lv_obj_add_event_cb(Chart1, solar_chart_draw_event_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);
        lv_obj_add_event_cb(spotPriceContainer, electricity_price_draw_event_cb, LV_EVENT_DRAW_PART_END, NULL);
        lv_obj_add_event_cb(settingsButton, onSettingsShowCallback, LV_EVENT_RELEASED, NULL);
        
        // Add click handlers for chart expand/collapse
        // For solar chart, add click on Chart1 since it covers the whole container
        lv_obj_add_flag(Chart1, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(Chart1, onAnyTouchShowButtons, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(Chart1, [](lv_event_t *e) {
            LOGD("Solar chart clicked");
            DashboardUI* self = (DashboardUI*)lv_event_get_user_data(e);
            if (self) {
                LOGD("Self valid, calling toggleChartExpand for solar chart");
                self->toggleChartExpand(self->RightBottomContainer);
            }
        }, LV_EVENT_CLICKED, this);
        
        lv_obj_add_flag(spotPriceContainer, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(spotPriceContainer, onAnyTouchShowButtons, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(spotPriceContainer, [](lv_event_t *e) {
            LOGD("Spot price clicked");
            DashboardUI* self = (DashboardUI*)lv_event_get_user_data(e);
            if (self) {
                LOGD("Self valid, calling toggleChartExpand for spot");
                self->toggleChartExpand(self->spotPriceContainer);
            }
        }, LV_EVENT_CLICKED, this);
        
        // Create intelligence plan tile (thin bar, same style as other tiles)
        intelligencePlanTile = lv_obj_create(RightContainer);
        lv_obj_remove_style_all(intelligencePlanTile);
        lv_obj_set_width(intelligencePlanTile, lv_pct(100));
        lv_obj_set_height(intelligencePlanTile, 40);  // Fixed height for collapsed state
        lv_obj_set_flex_grow(intelligencePlanTile, 0);  // No flex grow when collapsed, will be set to 1 when expanded
        lv_obj_set_style_radius(intelligencePlanTile, 24, 0);
        lv_obj_set_style_bg_color(intelligencePlanTile, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(intelligencePlanTile, 255, 0);
        lv_obj_set_style_shadow_color(intelligencePlanTile, lv_color_hex(0x000000), 0);
        lv_obj_set_style_shadow_opa(intelligencePlanTile, 64, 0);
        lv_obj_set_style_shadow_width(intelligencePlanTile, 32, 0);
        lv_obj_set_style_shadow_spread(intelligencePlanTile, 0, 0);
        lv_obj_set_style_pad_left(intelligencePlanTile, 8, 0);
        lv_obj_set_style_pad_right(intelligencePlanTile, 8, 0);
        lv_obj_set_style_pad_top(intelligencePlanTile, 8, 0);
        lv_obj_set_style_pad_bottom(intelligencePlanTile, 8, 0);
        lv_obj_clear_flag(intelligencePlanTile, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(intelligencePlanTile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_flex_flow(intelligencePlanTile, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(intelligencePlanTile, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        
        // Summary container (shown in collapsed state) - ROW layout
        // Layout: [Intelligence label] [Savings value] [Mode badge] [Chevron]
        intelligencePlanSummary = lv_obj_create(intelligencePlanTile);
        lv_obj_remove_style_all(intelligencePlanSummary);
        lv_obj_set_width(intelligencePlanSummary, lv_pct(100));
        lv_obj_set_height(intelligencePlanSummary, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(intelligencePlanSummary, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(intelligencePlanSummary, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_gap(intelligencePlanSummary, 8, 0);
        lv_obj_set_style_pad_hor(intelligencePlanSummary, 12, 0);
        lv_obj_clear_flag(intelligencePlanSummary, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(intelligencePlanSummary, LV_OBJ_FLAG_EVENT_BUBBLE);
        
        // "Inteligence" title label
        intelligenceSummaryTitle = lv_label_create(intelligencePlanSummary);
        lv_label_set_text(intelligenceSummaryTitle, TR(STR_INTELLIGENCE));
        lv_obj_set_style_text_font(intelligenceSummaryTitle, &ui_font_OpenSansSmall, 0);
        lv_obj_set_style_text_color(intelligenceSummaryTitle, lv_color_hex(0x333333), 0);
        lv_obj_set_flex_grow(intelligenceSummaryTitle, 1);  // Title takes remaining space, pushes rest to right
        
        // Savings value - aligned to the right next to badge
        intelligenceSummarySavings = lv_label_create(intelligencePlanSummary);
        lv_label_set_text(intelligenceSummarySavings, "");
        lv_obj_set_style_text_font(intelligenceSummarySavings, &ui_font_OpenSansSmall, 0);
        lv_obj_set_style_text_color(intelligenceSummarySavings, lv_color_hex(0x00AA00), 0);
        
        // Mode badge with colored background
        intelligenceSummaryBadge = lv_label_create(intelligencePlanSummary);
        lv_label_set_text(intelligenceSummaryBadge, "...");
        lv_obj_set_style_text_font(intelligenceSummaryBadge, &ui_font_OpenSansSmall, 0);
        lv_obj_set_style_text_color(intelligenceSummaryBadge, lv_color_white(), 0);
        lv_obj_set_style_bg_color(intelligenceSummaryBadge, lv_color_hex(0x888888), 0);
        lv_obj_set_style_bg_opa(intelligenceSummaryBadge, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(intelligenceSummaryBadge, 4, 0);
        lv_obj_set_style_pad_hor(intelligenceSummaryBadge, 8, 0);
        lv_obj_set_style_pad_ver(intelligenceSummaryBadge, 4, 0);
        
        // Detail container (shown in expanded state) - COLUMN layout (rows)
        intelligencePlanDetail = lv_obj_create(intelligencePlanTile);
        lv_obj_remove_style_all(intelligencePlanDetail);
        lv_obj_set_width(intelligencePlanDetail, lv_pct(100));
        lv_obj_set_flex_grow(intelligencePlanDetail, 1);
        lv_obj_set_flex_flow(intelligencePlanDetail, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(intelligencePlanDetail, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(intelligencePlanDetail, 12, 0);
        lv_obj_set_style_pad_top(intelligencePlanDetail, 8, 0);
        lv_obj_clear_flag(intelligencePlanDetail, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(intelligencePlanDetail, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(intelligencePlanDetail, LV_OBJ_FLAG_CLICKABLE);
        // Event handler will be added after tile is fully created
        
        // --- TOP: Upcoming plans container (scrollable) ---
        intelligenceUpcomingContainer = lv_obj_create(intelligencePlanDetail);
        lv_obj_remove_style_all(intelligenceUpcomingContainer);
        lv_obj_set_width(intelligenceUpcomingContainer, lv_pct(100));
        lv_obj_set_flex_grow(intelligenceUpcomingContainer, 1);  // Take remaining space, push stats to bottom
        lv_obj_set_flex_flow(intelligenceUpcomingContainer, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(intelligenceUpcomingContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(intelligenceUpcomingContainer, 4, 0);  // Reduced spacing
        // Enable vertical scrolling for all plans
        lv_obj_add_flag(intelligenceUpcomingContainer, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(intelligenceUpcomingContainer, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(intelligenceUpcomingContainer, LV_SCROLLBAR_MODE_AUTO);
        // Allow click events to bubble up to parent (so tile can be closed by clicking)
        lv_obj_add_flag(intelligenceUpcomingContainer, LV_OBJ_FLAG_EVENT_BUBBLE);
        
        // Create 4 upcoming plan rows - compact single-line layout
        for (int i = 0; i < VISIBLE_PLAN_ROWS; i++) {
            // Container for one plan entry - ROW layout with timeline on left
            intelligenceUpcomingRows[i] = lv_obj_create(intelligenceUpcomingContainer);
            lv_obj_remove_style_all(intelligenceUpcomingRows[i]);
            lv_obj_set_width(intelligenceUpcomingRows[i], lv_pct(100));
            lv_obj_set_height(intelligenceUpcomingRows[i], LV_SIZE_CONTENT);
            lv_obj_set_flex_flow(intelligenceUpcomingRows[i], LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(intelligenceUpcomingRows[i], LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_all(intelligenceUpcomingRows[i], 4, 0);
            lv_obj_set_style_pad_gap(intelligenceUpcomingRows[i], 6, 0);
            lv_obj_clear_flag(intelligenceUpcomingRows[i], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(intelligenceUpcomingRows[i], LV_OBJ_FLAG_EVENT_BUBBLE);
            
            // Timeline container (bullet + line)
            lv_obj_t *timelineContainer = lv_obj_create(intelligenceUpcomingRows[i]);
            lv_obj_remove_style_all(timelineContainer);
            lv_obj_set_width(timelineContainer, 12);
            lv_obj_set_height(timelineContainer, lv_pct(100));
            lv_obj_set_flex_flow(timelineContainer, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(timelineContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_clear_flag(timelineContainer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(timelineContainer, LV_OBJ_FLAG_EVENT_BUBBLE);
            
            // Bullet (circle) - smaller
            intelligenceUpcomingBullets[i] = lv_obj_create(timelineContainer);
            lv_obj_remove_style_all(intelligenceUpcomingBullets[i]);
            lv_obj_set_size(intelligenceUpcomingBullets[i], 8, 8);
            lv_obj_set_style_radius(intelligenceUpcomingBullets[i], LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_bg_opa(intelligenceUpcomingBullets[i], LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(intelligenceUpcomingBullets[i], lv_color_hex(0x333333), 0);
            lv_obj_clear_flag(intelligenceUpcomingBullets[i], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
            
            // Vertical line (below bullet) - thinner
            intelligenceUpcomingLines[i] = lv_obj_create(timelineContainer);
            lv_obj_remove_style_all(intelligenceUpcomingLines[i]);
            lv_obj_set_width(intelligenceUpcomingLines[i], 2);
            lv_obj_set_flex_grow(intelligenceUpcomingLines[i], 1);
            lv_obj_set_style_bg_opa(intelligenceUpcomingLines[i], LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(intelligenceUpcomingLines[i], lv_color_hex(0x333333), 0);
            lv_obj_clear_flag(intelligenceUpcomingLines[i], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
            // Hide line for last visible item
            if (i == VISIBLE_PLAN_ROWS - 1) {
                lv_obj_add_flag(intelligenceUpcomingLines[i], LV_OBJ_FLAG_HIDDEN);
            }
            
            // Time label (compact)
            intelligenceUpcomingTimes[i] = lv_label_create(intelligenceUpcomingRows[i]);
            lv_label_set_text(intelligenceUpcomingTimes[i], "--:--");
            lv_obj_set_style_text_font(intelligenceUpcomingTimes[i], &ui_font_OpenSansSmall, 0);
            lv_obj_set_style_text_color(intelligenceUpcomingTimes[i], lv_color_hex(0x333333), 0);
            lv_obj_set_width(intelligenceUpcomingTimes[i], 40);  // Fixed width for alignment
            
            // Mode badge (compact)
            intelligenceUpcomingModes[i] = lv_label_create(intelligenceUpcomingRows[i]);
            lv_label_set_text(intelligenceUpcomingModes[i], "---");
            lv_obj_set_style_text_font(intelligenceUpcomingModes[i], &ui_font_OpenSansSmall, 0);
            lv_obj_set_style_text_color(intelligenceUpcomingModes[i], lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_bg_opa(intelligenceUpcomingModes[i], LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(intelligenceUpcomingModes[i], lv_color_hex(0x666666), 0);
            lv_obj_set_style_radius(intelligenceUpcomingModes[i], 4, 0);
            lv_obj_set_style_pad_left(intelligenceUpcomingModes[i], 4, 0);
            lv_obj_set_style_pad_right(intelligenceUpcomingModes[i], 4, 0);
            lv_obj_set_style_pad_top(intelligenceUpcomingModes[i], 1, 0);
            lv_obj_set_style_pad_bottom(intelligenceUpcomingModes[i], 1, 0);
            
            // Reason label (on same line, takes remaining width)
            intelligenceUpcomingReasons[i] = lv_label_create(intelligenceUpcomingRows[i]);
            lv_label_set_text(intelligenceUpcomingReasons[i], "");
            lv_obj_set_flex_grow(intelligenceUpcomingReasons[i], 1);
            lv_label_set_long_mode(intelligenceUpcomingReasons[i], LV_LABEL_LONG_DOT);  // Truncate with ...
            lv_obj_set_style_text_font(intelligenceUpcomingReasons[i], &ui_font_OpenSansSmall, 0);
            lv_obj_set_style_text_color(intelligenceUpcomingReasons[i], lv_color_hex(0x666666), 0);
        }
        
        // --- BOTTOM: Statistics container - like daily stats ---
        intelligenceStatsContainer = lv_obj_create(intelligencePlanDetail);
        lv_obj_remove_style_all(intelligenceStatsContainer);
        lv_obj_set_width(intelligenceStatsContainer, lv_pct(100));
        lv_obj_set_height(intelligenceStatsContainer, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(intelligenceStatsContainer, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(intelligenceStatsContainer, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_gap(intelligenceStatsContainer, 12, 0);
        lv_obj_set_style_pad_top(intelligenceStatsContainer, 12, 0);
        lv_obj_set_style_border_width(intelligenceStatsContainer, 1, 0);
        lv_obj_set_style_border_color(intelligenceStatsContainer, lv_color_hex(0xE0E0E0), 0);
        lv_obj_set_style_border_side(intelligenceStatsContainer, LV_BORDER_SIDE_TOP, 0);
        lv_obj_clear_flag(intelligenceStatsContainer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(intelligenceStatsContainer, LV_OBJ_FLAG_EVENT_BUBBLE);
        
        // --- Production stat (like pvStatsContainer) ---
        lv_obj_t *productionStatContainer = lv_obj_create(intelligenceStatsContainer);
        lv_obj_remove_style_all(productionStatContainer);
        lv_obj_set_height(productionStatContainer, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(productionStatContainer, 1);
        lv_obj_set_flex_flow(productionStatContainer, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(productionStatContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_gap(productionStatContainer, 4, 0);
        lv_obj_clear_flag(productionStatContainer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(productionStatContainer, LV_OBJ_FLAG_EVENT_BUBBLE);
        
        // Sun icon
        lv_obj_t *prodIcon = lv_img_create(productionStatContainer);
        lv_img_set_src(prodIcon, &ui_img_1516017106);
        
        // Value + unit container (takes remaining space, text aligned right)
        lv_obj_t *prodValueContainer = lv_obj_create(productionStatContainer);
        lv_obj_remove_style_all(prodValueContainer);
        lv_obj_set_height(prodValueContainer, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(prodValueContainer, 1);
        lv_obj_set_flex_flow(prodValueContainer, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(prodValueContainer, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_column(prodValueContainer, 2, 0);
        lv_obj_clear_flag(prodValueContainer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        
        intelligenceStatsProduction = lv_label_create(prodValueContainer);
        lv_label_set_text(intelligenceStatsProduction, "~--");
        lv_obj_set_style_text_font(intelligenceStatsProduction, &ui_font_OpenSansLargeBold, 0);
        lv_obj_set_style_text_color(intelligenceStatsProduction, lv_color_hex(0x333333), 0);
        
        intelligenceStatsProductionUnit = lv_label_create(prodValueContainer);
        lv_label_set_text(intelligenceStatsProductionUnit, "kWh");
        lv_obj_set_style_text_font(intelligenceStatsProductionUnit, &ui_font_OpenSansExtraSmall, 0);
        lv_obj_set_style_text_color(intelligenceStatsProductionUnit, lv_color_hex(0x333333), 0);
        
        // Separator between production and consumption
        intelligenceStatsSeparator = lv_obj_create(intelligenceStatsContainer);
        lv_obj_remove_style_all(intelligenceStatsSeparator);
        lv_obj_set_size(intelligenceStatsSeparator, 1, 24);
        lv_obj_set_style_bg_color(intelligenceStatsSeparator, lv_color_hex(0xE0E0E0), 0);
        lv_obj_set_style_bg_opa(intelligenceStatsSeparator, LV_OPA_COVER, 0);
        lv_obj_clear_flag(intelligenceStatsSeparator, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        
        // --- Consumption stat (like loadStatsContainer) ---
        lv_obj_t *consumptionStatContainer = lv_obj_create(intelligenceStatsContainer);
        lv_obj_remove_style_all(consumptionStatContainer);
        lv_obj_set_height(consumptionStatContainer, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(consumptionStatContainer, 1);
        lv_obj_set_flex_flow(consumptionStatContainer, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(consumptionStatContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_gap(consumptionStatContainer, 4, 0);
        lv_obj_clear_flag(consumptionStatContainer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(consumptionStatContainer, LV_OBJ_FLAG_EVENT_BUBBLE);
        
        // Load icon
        lv_obj_t *consIcon = lv_img_create(consumptionStatContainer);
        lv_img_set_src(consIcon, &ui_img_564643105);
        
        // Value + unit container (takes remaining space, text aligned right)
        lv_obj_t *consValueContainer = lv_obj_create(consumptionStatContainer);
        lv_obj_remove_style_all(consValueContainer);
        lv_obj_set_height(consValueContainer, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(consValueContainer, 1);
        lv_obj_set_flex_flow(consValueContainer, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(consValueContainer, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_column(consValueContainer, 2, 0);
        lv_obj_clear_flag(consValueContainer, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        
        intelligenceStatsConsumption = lv_label_create(consValueContainer);
        lv_label_set_text(intelligenceStatsConsumption, "~--");
        lv_obj_set_style_text_font(intelligenceStatsConsumption, &ui_font_OpenSansLargeBold, 0);
        lv_obj_set_style_text_color(intelligenceStatsConsumption, lv_color_hex(0x333333), 0);
        
        intelligenceStatsConsumptionUnit = lv_label_create(consValueContainer);
        lv_label_set_text(intelligenceStatsConsumptionUnit, "kWh");
        lv_obj_set_style_text_font(intelligenceStatsConsumptionUnit, &ui_font_OpenSansExtraSmall, 0);
        lv_obj_set_style_text_color(intelligenceStatsConsumptionUnit, lv_color_hex(0x333333), 0);
        
        // Store DashboardUI pointer in tile's user_data for event handlers
        lv_obj_set_user_data(intelligencePlanTile, this);
        
        // Add click handler for intelligence tile expand/collapse
        // Pass tile as event user_data, get DashboardUI from tile's user_data
        lv_obj_add_flag(intelligencePlanTile, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(intelligencePlanTile, [](lv_event_t *e) {
            // Show buttons on touch
            DashboardUI* self = (DashboardUI*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_user_data(e));
            if (self) self->showButtonsOnTouch();
        }, LV_EVENT_PRESSED, intelligencePlanTile);
        lv_obj_add_event_cb(intelligencePlanTile, [](lv_event_t *e) {
            lv_obj_t* tile = (lv_obj_t*)lv_event_get_user_data(e);
            if (!tile) return;
            DashboardUI* self = (DashboardUI*)lv_obj_get_user_data(tile);
            if (!self) return;
            // Summary is the first child (index 0), detail is second (index 1)
            lv_obj_t* summary = lv_obj_get_child(tile, 0);
            lv_obj_t* detail = lv_obj_get_child(tile, 1);
            LOGD("Intelligence tile clicked, tile=%p, summary=%p, detail=%p", tile, summary, detail);
            self->toggleChartExpand(tile, summary, detail);
        }, LV_EVENT_CLICKED, intelligencePlanTile);
        
        // Add click handler for detail container (same logic)
        lv_obj_add_event_cb(intelligencePlanDetail, [](lv_event_t *e) {
            // Show buttons on touch
            DashboardUI* self = (DashboardUI*)lv_obj_get_user_data((lv_obj_t*)lv_event_get_user_data(e));
            if (self) self->showButtonsOnTouch();
        }, LV_EVENT_PRESSED, intelligencePlanTile);
        lv_obj_add_event_cb(intelligencePlanDetail, [](lv_event_t *e) {
            lv_obj_t* tile = (lv_obj_t*)lv_event_get_user_data(e);
            if (!tile) return;
            DashboardUI* self = (DashboardUI*)lv_obj_get_user_data(tile);
            if (!self) return;
            lv_obj_t* summary = lv_obj_get_child(tile, 0);
            lv_obj_t* detail = lv_obj_get_child(tile, 1);
            LOGD("Intelligence detail clicked, tile=%p, summary=%p, detail=%p", tile, summary, detail);
            self->toggleChartExpand(tile, summary, detail);
        }, LV_EVENT_CLICKED, intelligencePlanTile);
        
        // Move to position 1 (after TopRightContainer, before RightBottomContainer/solar chart)
        lv_obj_move_to_index(intelligencePlanTile, 1);
        lv_obj_move_to_index(intelligencePlanTile, 1);
        
        // Initially hidden - shown only when intelligence is active
        lv_obj_add_flag(intelligencePlanTile, LV_OBJ_FLAG_HIDDEN);
        
        // Adjust flex grow weights - solar chart bigger, spot smaller
        lv_obj_set_flex_grow(RightBottomContainer, 2);  // Solar chart - 2x weight
        lv_obj_set_flex_grow(spotPriceContainer, 1);    // Spot prices - 1x weight
        
        // Create intelligence button (next to settings button)
        if (onIntelligenceShowCallback != nullptr) {
            intelligenceButton = lv_btn_create(screen);
            lv_obj_set_width(intelligenceButton, 64);
            lv_obj_set_height(intelligenceButton, 64);
            lv_obj_set_x(intelligenceButton, lv_pct(-2));
            lv_obj_set_y(intelligenceButton, lv_pct(-18));  // Above settings button
            lv_obj_set_align(intelligenceButton, LV_ALIGN_BOTTOM_RIGHT);
            lv_obj_add_flag(intelligenceButton, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_SCROLL_ON_FOCUS);
            lv_obj_clear_flag(intelligenceButton, LV_OBJ_FLAG_CLICK_FOCUSABLE | LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_bg_color(intelligenceButton, lv_color_hex(0xFFFFFF), LV_STYLE_SELECTOR_DEFAULT);
            lv_obj_set_style_bg_opa(intelligenceButton, 255, LV_STYLE_SELECTOR_DEFAULT);
            lv_obj_set_style_outline_color(intelligenceButton, lv_color_hex(0x00AAFF), LV_STYLE_SELECTOR_DEFAULT);
            lv_obj_set_style_outline_opa(intelligenceButton, 255, LV_STYLE_SELECTOR_DEFAULT);
            lv_obj_set_style_outline_width(intelligenceButton, 3, LV_STYLE_SELECTOR_DEFAULT);
            lv_obj_set_style_shadow_color(intelligenceButton, lv_color_hex(0x00AAFF), LV_STYLE_SELECTOR_DEFAULT);
            lv_obj_set_style_shadow_opa(intelligenceButton, 255, LV_STYLE_SELECTOR_DEFAULT);
            lv_obj_set_style_shadow_width(intelligenceButton, 32, LV_STYLE_SELECTOR_DEFAULT);
            
            // Add brain/AI icon label
            lv_obj_t* iconLabel = lv_label_create(intelligenceButton);
            lv_label_set_text(iconLabel, LV_SYMBOL_EYE_OPEN);  // Loop icon for automatic/intelligence
            lv_obj_set_style_text_font(iconLabel, &lv_font_montserrat_24, 0);
            lv_obj_set_style_text_color(iconLabel, lv_color_hex(0x00AAFF), 0);
            lv_obj_center(iconLabel);
            
            lv_obj_add_event_cb(intelligenceButton, onIntelligenceShowCallback, LV_EVENT_RELEASED, NULL);
        }

        // Create floating IP address badge (bottom left corner)
        ipBadge = lv_obj_create(screen);
        lv_obj_remove_style_all(ipBadge);
        lv_obj_set_size(ipBadge, LV_SIZE_CONTENT, 40);
        lv_obj_set_x(ipBadge, lv_pct(2));
        lv_obj_set_y(ipBadge, lv_pct(-2));
        lv_obj_set_align(ipBadge, LV_ALIGN_BOTTOM_LEFT);
        lv_obj_add_flag(ipBadge, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ipBadge, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(ipBadge, lv_color_hex(0x2196F3), 0);  // Material Blue
        lv_obj_set_style_bg_opa(ipBadge, 255, 0);
        lv_obj_set_style_radius(ipBadge, 20, 0);
        lv_obj_set_style_shadow_color(ipBadge, lv_color_hex(0x1976D2), 0);  // Darker blue shadow
        lv_obj_set_style_shadow_opa(ipBadge, 128, 0);
        lv_obj_set_style_shadow_width(ipBadge, 16, 0);
        lv_obj_set_style_pad_left(ipBadge, 16, 0);
        lv_obj_set_style_pad_right(ipBadge, 16, 0);
        lv_obj_set_style_pad_top(ipBadge, 8, 0);
        lv_obj_set_style_pad_bottom(ipBadge, 8, 0);
        
        // IP address label inside badge
        ipBadgeLabel = lv_label_create(ipBadge);
        lv_obj_set_style_text_font(ipBadgeLabel, &ui_font_OpenSansSmall, 0);
        lv_obj_set_style_text_color(ipBadgeLabel, lv_color_hex(0xFFFFFF), 0);  // White text on blue
        lv_label_set_text(ipBadgeLabel, "---.---.---.---");  // Placeholder, will be updated
        lv_obj_center(ipBadgeLabel);

        // Create intelligence mode label (same style as temperature badge, aligned left)
        intelligenceModeLabel = lv_label_create(inverterContainer);
        lv_obj_set_width(intelligenceModeLabel, LV_SIZE_CONTENT);
        lv_obj_set_height(intelligenceModeLabel, LV_SIZE_CONTENT);
        lv_obj_set_x(intelligenceModeLabel, -22);
        lv_obj_set_y(intelligenceModeLabel, -28);
        lv_obj_set_align(intelligenceModeLabel, LV_ALIGN_TOP_LEFT);
        lv_label_set_text(intelligenceModeLabel, "");
        lv_obj_add_flag(intelligenceModeLabel, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_align(intelligenceModeLabel, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_font(intelligenceModeLabel, &ui_font_OpenSansSmall, 0);
        lv_obj_set_style_text_color(intelligenceModeLabel, lv_color_white(), 0);
        lv_obj_set_style_radius(intelligenceModeLabel, 8, 0);
        lv_obj_set_style_bg_opa(intelligenceModeLabel, 255, 0);
        lv_obj_set_style_shadow_width(intelligenceModeLabel, 32, 0);
        lv_obj_set_style_shadow_spread(intelligenceModeLabel, 0, 0);
        lv_obj_set_style_pad_left(intelligenceModeLabel, 4, 0);
        lv_obj_set_style_pad_right(intelligenceModeLabel, 4, 0);
        lv_obj_set_style_pad_top(intelligenceModeLabel, 4, 0);
        lv_obj_set_style_pad_bottom(intelligenceModeLabel, 4, 0);

        // Set consistent row spacing for stats containers
        lv_obj_set_style_pad_row(Container34, 0, 0);  // pvStats column
        lv_obj_set_style_pad_row(Container37, 0, 0);  // loadStats column

        pvAnimator.setup(LeftContainer, _ui_theme_color_pvColor);
        batteryAnimator.setup(LeftContainer, _ui_theme_color_batteryColor);
        gridAnimator.setup(LeftContainer, _ui_theme_color_gridColor);
        loadAnimator.setup(LeftContainer, _ui_theme_color_loadColor);

        // remove demo chart series from designer
        while (lv_chart_get_series_next(Chart1, NULL))
        {
            lv_chart_remove_series(Chart1, lv_chart_get_series_next(Chart1, NULL));
        }

        // Chart series - jedna sada pro všechna data (reálná i predikce)
        pvPowerSeries = lv_chart_add_series(Chart1, lv_color_hex(_ui_theme_color_pvColor[0]), LV_CHART_AXIS_SECONDARY_Y);
        acPowerSeries = lv_chart_add_series(Chart1, lv_color_hex(_ui_theme_color_loadColor[0]), LV_CHART_AXIS_SECONDARY_Y);
        socSeries = lv_chart_add_series(Chart1, lv_color_hex(_ui_theme_color_batteryColor[0]), LV_CHART_AXIS_PRIMARY_Y);
        
        // Nastavíme počet bodů na celý den (96 čtvrthodin)
        lv_chart_set_point_count(Chart1, CHART_QUARTERS_PER_DAY);

        lv_obj_set_style_anim_time(inverterPowerBar1, UI_BACKGROUND_ANIMATION_DURATION, LV_PART_ANY);
        lv_obj_set_style_anim_time(inverterPowerBar2, UI_BACKGROUND_ANIMATION_DURATION, LV_PART_ANY);
        lv_obj_set_style_anim_time(inverterPowerBar3, UI_BACKGROUND_ANIMATION_DURATION, LV_PART_ANY);
        lv_obj_set_style_anim_time(meterPowerBarL1, UI_BACKGROUND_ANIMATION_DURATION, LV_PART_ANY);
        lv_obj_set_style_anim_time(meterPowerBarL2, UI_BACKGROUND_ANIMATION_DURATION, LV_PART_ANY);
        lv_obj_set_style_anim_time(meterPowerBarL3, UI_BACKGROUND_ANIMATION_DURATION, LV_PART_ANY);

        // add timer
        clocksTimer = lv_timer_create([](lv_timer_t *timer)
                                      {
                                        DashboardUI* self = (DashboardUI*)timer->user_data;
                                        static uint8_t step = 0;
                                        step++;
                                        // update time and date labels
                                        struct tm timeinfo;
                                        time_t now = time(nullptr);
                                        localtime_r(&now, &timeinfo);
                                        
                                        // Check if time is synced (year > 2020 means NTP worked)
                                        if (timeinfo.tm_year > 120) {  // tm_year is years since 1900
                                            self->timeSynced = true;
                                        }
                                        
                                        if (self->timeSynced) {
                                            // Once synced, always show clock
                                            lv_obj_clear_flag(self->clocksLabel, LV_OBJ_FLAG_HIDDEN);
                                            char timeStr[6];
                                            lv_snprintf(timeStr, sizeof(timeStr), step % 2 == 0 ? "%02d %02d" : "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
                                            lv_label_set_text(self->clocksLabel, timeStr);
                                        } else {
                                            // Not yet synced - hide clock
                                            lv_obj_add_flag(self->clocksLabel, LV_OBJ_FLAG_HIDDEN);
                                            lv_label_set_text(self->clocksLabel, "--:--");
                                        }
                                      }, 1000, this);

        // Make inverter container clickable and add menu (only if intelligence is supported)
        lv_obj_add_event_cb(inverterContainer, [](lv_event_t *e) {
            DashboardUI* self = (DashboardUI*)lv_event_get_user_data(e);
            self->showButtonsOnTouch();  // Show buttons on touch
        }, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(inverterContainer, [](lv_event_t *e) {
            DashboardUI* self = (DashboardUI*)lv_event_get_user_data(e);
            if (self->intelligenceSupported) {
                self->showInverterModeMenu();
            }
        }, LV_EVENT_CLICKED, this);
        
        // Add touch handler to entire dashboard to show buttons on any touch
        lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(screen, [](lv_event_t *e) {
            DashboardUI* self = (DashboardUI*)lv_event_get_user_data(e);
            self->showButtonsOnTouch();
        }, LV_EVENT_PRESSED, this);
        
        // Make PV strings container more compact - reduce row spacing
        lv_obj_set_style_pad_row(pvStringsContainer, 0, LV_PART_MAIN);   // Was 4
        lv_obj_set_style_pad_row(pvStringsContainer1, 0, LV_PART_MAIN);  // Was 4
        // Reduce gap between the two string columns
        lv_obj_set_style_pad_column(Container1, 2, LV_PART_MAIN);  // Was 4
        // Use smaller font for PV string labels
        lv_obj_set_style_text_font(pv1Label, &ui_font_OpenSansSmall, 0);
        lv_obj_set_style_text_font(pv2Label, &ui_font_OpenSansSmall, 0);
        lv_obj_set_style_text_font(pv3Label, &ui_font_OpenSansSmall, 0);
        lv_obj_set_style_text_font(pv4Label, &ui_font_OpenSansSmall, 0);
        
        // Apply intelligence supported state (may have been set before show())
        if (intelligenceSupported) {
            lv_obj_add_flag(inverterContainer, LV_OBJ_FLAG_CLICKABLE);
        } else {
            lv_obj_clear_flag(inverterContainer, LV_OBJ_FLAG_CLICKABLE);
        }
    }
    
    /**
     * Show settings and intelligence buttons when user touches the dashboard
     */
    void showButtonsOnTouch() {
        lastTouchMillis = millis();
        
        // Show settings button
        lv_obj_clear_flag(settingsButton, LV_OBJ_FLAG_HIDDEN);
        
        // Show intelligence button if supported
        if (intelligenceButton != nullptr && intelligenceSupported) {
            lv_obj_clear_flag(intelligenceButton, LV_OBJ_FLAG_HIDDEN);
        }
        
        // Show IP badge and update IP address
        if (ipBadge != nullptr) {
            updateIpBadge();
            lv_obj_clear_flag(ipBadge, LV_OBJ_FLAG_HIDDEN);
        }
    }
    
    /**
     * Update IP badge with current WiFi client IP address
     */
    void updateIpBadge() {
        if (ipBadgeLabel == nullptr) return;
        
        IPAddress ip = WiFi.localIP();
        if (ip[0] != 0) {
            // Valid IP address
            lv_label_set_text(ipBadgeLabel, ip.toString().c_str());
        } else {
            // Not connected yet
            lv_label_set_text(ipBadgeLabel, TR(STR_NO_WIFI));
        }
    }

    void showInverterModeMenu() {
        // Create menu if not exists
        if (inverterModeMenu != nullptr) {
            lv_obj_del(inverterModeMenu);
            inverterModeMenu = nullptr;
            return;  // Toggle off
        }

        // Create popup menu
        inverterModeMenu = lv_obj_create(screen);
        lv_obj_set_size(inverterModeMenu, 220, LV_SIZE_CONTENT);
        lv_obj_align(inverterModeMenu, LV_ALIGN_CENTER, 0, 0);
        lv_obj_add_flag(inverterModeMenu, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING);
        lv_obj_set_flex_flow(inverterModeMenu, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(inverterModeMenu, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(inverterModeMenu, 12, 0);
        lv_obj_set_style_pad_row(inverterModeMenu, 8, 0);
        lv_obj_set_style_radius(inverterModeMenu, 16, 0);
        lv_obj_set_style_bg_color(inverterModeMenu, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(inverterModeMenu, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(inverterModeMenu, 0, 0);
        lv_obj_set_style_shadow_width(inverterModeMenu, 32, 0);
        lv_obj_set_style_shadow_opa(inverterModeMenu, LV_OPA_30, 0);
        lv_obj_clear_flag(inverterModeMenu, LV_OBJ_FLAG_SCROLLABLE);

        // Title
        lv_obj_t* title = lv_label_create(inverterModeMenu);
        lv_label_set_text(title, TR(STR_INVERTER_MODE));
        lv_obj_set_style_text_font(title, &ui_font_OpenSansMedium, 0);
        lv_obj_set_width(title, lv_pct(100));
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

        // Get current intelligence state
        SolarIntelligenceSettings_t settings = IntelligenceSettingsStorage::load();

        // Menu items
        struct MenuItem {
            const char* text;
            SolarInverterMode_t mode;
            bool enableIntelligence;
            lv_color_t color;
        };
        
        MenuItem items[] = {
            {TR(STR_INTELLIGENCE), SI_MODE_UNKNOWN, true, lv_color_hex(0x2196F3)},
            {TR(STR_NORMAL), SI_MODE_SELF_USE, false, lv_color_hex(_ui_theme_color_loadColor[0])},
            {TR(STR_CHARGE), SI_MODE_CHARGE_FROM_GRID, false, lv_color_hex(_ui_theme_color_gridColor[0])},
            {TR(STR_DISCHARGE), SI_MODE_DISCHARGE_TO_GRID, false, lv_color_hex(_ui_theme_color_batteryColor[0])},
            {TR(STR_HOLD), SI_MODE_HOLD_BATTERY, false, lv_color_hex(0x888888)},
        };

        for (int i = 0; i < 5; i++) {
            lv_obj_t* btn = lv_btn_create(inverterModeMenu);
            lv_obj_set_width(btn, lv_pct(100));
            lv_obj_set_height(btn, 40);
            lv_obj_set_style_radius(btn, 8, 0);
            lv_obj_set_style_bg_color(btn, items[i].color, 0);
            lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
            
            lv_obj_t* label = lv_label_create(btn);
            lv_label_set_text(label, items[i].text);
            lv_obj_set_style_text_font(label, &ui_font_OpenSansSmall, 0);
            lv_obj_set_style_text_color(label, lv_color_white(), 0);
            lv_obj_center(label);

            // Store data for callback
            uint32_t userData = (items[i].mode << 8) | (items[i].enableIntelligence ? 1 : 0);
            lv_obj_add_event_cb(btn, [](lv_event_t *e) {
                DashboardUI* self = (DashboardUI*)lv_event_get_user_data(e);
                lv_obj_t* btn = lv_event_get_target(e);
                uint32_t data = (uint32_t)(uintptr_t)lv_obj_get_user_data(btn);
                SolarInverterMode_t mode = (SolarInverterMode_t)(data >> 8);
                bool enableIntelligence = (data & 1) != 0;
                
                // Close overlay first
                if (self->inverterModeOverlay != nullptr) {
                    lv_obj_del(self->inverterModeOverlay);
                    self->inverterModeOverlay = nullptr;
                }
                
                // Close menu
                if (self->inverterModeMenu != nullptr) {
                    lv_obj_del(self->inverterModeMenu);
                    self->inverterModeMenu = nullptr;
                }
                
                // Call callback (mode change happens silently in background)
                if (self->modeChangeCallback != nullptr) {
                    self->modeChangeCallback(mode, enableIntelligence);
                }
            }, LV_EVENT_CLICKED, this);
            lv_obj_set_user_data(btn, (void*)(uintptr_t)userData);
        }

        // Add close button/area
        lv_obj_add_event_cb(inverterModeMenu, [](lv_event_t *e) {
            // Prevent closing when clicking inside menu
            lv_event_stop_bubbling(e);
        }, LV_EVENT_CLICKED, NULL);

        // Close on click outside - add overlay behind menu
        inverterModeOverlay = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(inverterModeOverlay);
        lv_obj_set_pos(inverterModeOverlay, 0, 0);
        lv_obj_set_size(inverterModeOverlay, lv_disp_get_hor_res(NULL), lv_disp_get_ver_res(NULL));
        lv_obj_add_flag(inverterModeOverlay, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(inverterModeOverlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(inverterModeOverlay, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(inverterModeOverlay, LV_OPA_70, 0);
        lv_obj_set_style_pad_all(inverterModeOverlay, 0, 0);
        lv_obj_set_style_border_width(inverterModeOverlay, 0, 0);
        lv_obj_set_style_outline_width(inverterModeOverlay, 0, 0);
        
        // Move menu to top layer as well
        lv_obj_set_parent(inverterModeMenu, lv_layer_top());
        lv_obj_move_foreground(inverterModeMenu);
        
        lv_obj_set_user_data(inverterModeOverlay, this);
        lv_obj_add_event_cb(inverterModeOverlay, [](lv_event_t *e) {
            DashboardUI* self = (DashboardUI*)lv_obj_get_user_data(lv_event_get_target(e));
            if (self->inverterModeMenu != nullptr) {
                lv_obj_del(self->inverterModeMenu);
                self->inverterModeMenu = nullptr;
            }
            if (self->inverterModeOverlay != nullptr) {
                lv_obj_del(self->inverterModeOverlay);
                self->inverterModeOverlay = nullptr;
            }
        }, LV_EVENT_CLICKED, NULL);
    }

    // Helper to check if we have valid price data - uses safe flag instead of pointer access
    bool hasValidPriceData() {
        return spotChartData.hasValidPrices;
    }

    // Toggle chart expand - hide siblings to let the clicked chart fill the container
    // For intelligence tile, also pass summary and detail pointers
    // Animation duration in ms
    static const uint32_t ANIM_DURATION = 200;
    
    // Animate opacity (fade in/out)
    void animateFade(lv_obj_t *obj, bool fadeIn, uint32_t delay = 0) {
        if (!obj) return;
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, obj);
        lv_anim_set_values(&a, fadeIn ? 0 : 255, fadeIn ? 255 : 0);
        lv_anim_set_time(&a, ANIM_DURATION);
        lv_anim_set_delay(&a, delay);
        lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) {
            lv_obj_set_style_opa((lv_obj_t*)obj, v, 0);
        });
        if (fadeIn) {
            // Show object before fade in
            lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_opa(obj, 0, 0);
        } else {
            // Hide object after fade out
            lv_anim_set_ready_cb(&a, [](lv_anim_t *a) {
                lv_obj_add_flag((lv_obj_t*)a->var, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_style_opa((lv_obj_t*)a->var, 255, 0);  // Reset opacity
            });
        }
        lv_anim_start(&a);
    }
    
    // Animate height change
    void animateHeight(lv_obj_t *obj, int32_t targetHeight, uint32_t delay = 0, lv_anim_ready_cb_t readyCb = nullptr) {
        if (!obj) return;
        int32_t currentHeight = lv_obj_get_height(obj);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, obj);
        lv_anim_set_values(&a, currentHeight, targetHeight);
        lv_anim_set_time(&a, ANIM_DURATION);
        lv_anim_set_delay(&a, delay);
        lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) {
            lv_obj_set_height((lv_obj_t*)obj, v);
            lv_obj_set_style_height((lv_obj_t*)obj, v, 0);
        });
        if (readyCb) {
            lv_anim_set_ready_cb(&a, readyCb);
        }
        lv_anim_start(&a);
    }

    void toggleChartExpand(lv_obj_t *chart, lv_obj_t *summary = nullptr, lv_obj_t *detail = nullptr) {
        LOGD("toggleChartExpand called with chart=%p, spotPriceContainer=%p, RightBottomContainer=%p", 
              chart, spotPriceContainer, RightBottomContainer);
        if (!chart) {
            LOGE("Toggle chart: null chart pointer");
            return;
        }
        
        bool isCollapsing = (expandedChart == chart);
        bool isIntelligenceTile = (summary != nullptr && detail != nullptr);
        LOGD("Toggle chart: isCollapsing=%d, isIntelligenceTile=%d, expandedChart=%p, chart=%p", 
              isCollapsing, isIntelligenceTile, expandedChart, chart);
        
        if (isCollapsing) {
            // If collapsing intelligence tile
            if (isIntelligenceTile) {
                LOGD("Collapsing intelligence tile");
                // 1. Fade out and hide detail content
                animateFade(detail, false);
                lv_obj_set_flex_grow(detail, 0);
                // After animation, set height to 0
                lv_anim_t a;
                lv_anim_init(&a);
                lv_anim_set_var(&a, detail);
                lv_anim_set_values(&a, lv_obj_get_height(detail), 0);
                lv_anim_set_time(&a, ANIM_DURATION);
                lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) {
                    lv_obj_set_height((lv_obj_t*)obj, v);
                });
                lv_anim_start(&a);
                
                // 2. Summary stays visible (always shown)
                lv_obj_clear_flag(summary, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_height(summary, LV_SIZE_CONTENT);
                // 3. Tile: no flex_grow, animate to content height
                lv_obj_set_flex_grow(chart, 0);
                lv_obj_set_style_min_height(chart, 0, 0);
                lv_obj_set_style_max_height(chart, LV_SIZE_CONTENT, 0);
            }
            
            // Show all siblings with fade in and restore flex grow
            uint32_t childCount = lv_obj_get_child_cnt(RightContainer);
            for (uint32_t i = 0; i < childCount; i++) {
                lv_obj_t *child = lv_obj_get_child(RightContainer, i);
                if (!child) continue;
                
                // Handle intelligence tile - already processed above if it's the one being collapsed
                if (child == chart && isIntelligenceTile) {
                    lv_obj_clear_flag(child, LV_OBJ_FLAG_HIDDEN);
                    continue;
                }
                
                // Handle intelligence tile when collapsing OTHER charts (spot/solar)
                if (child == intelligencePlanTile && !isIntelligenceTile) {
                    // Show intelligence tile only if intelligence is supported AND enabled
                    if (intelligenceSupported && intelligenceEnabled) {
                        animateFade(child, true, ANIM_DURATION / 2);
                        lv_obj_set_flex_grow(child, 0);
                        lv_obj_set_height(child, LV_SIZE_CONTENT);
                        LOGD("Restoring intelligence tile visibility");
                    }
                    continue;
                }
                
                // Handle spot price container
                if (child == spotPriceContainer) {
                    LOGD("Processing spotPriceContainer, hasValidPriceData=%d", hasValidPriceData());
                    if (hasValidPriceData()) {
                        animateFade(child, true, ANIM_DURATION / 2);
                        lv_obj_set_flex_grow(child, 1);
                    }
                    continue;
                }
                
                // Handle solar chart (RightBottomContainer)
                if (child == RightBottomContainer) {
                    LOGD("Processing RightBottomContainer");
                    animateFade(child, true, ANIM_DURATION / 2);
                    lv_obj_set_flex_grow(child, 2);
                    continue;
                }
                
                // TopRightContainer - just show with fade
                if (child == TopRightContainer) {
                    animateFade(child, true, ANIM_DURATION / 2);
                    continue;
                }
                
                // Other unknown children - hide them
                lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
            }
            
            expandedChart = nullptr;
        } else {
            // Expand - fade out all siblings except clicked chart
            uint32_t childCount = lv_obj_get_child_cnt(RightContainer);
            for (uint32_t i = 0; i < childCount; i++) {
                lv_obj_t *child = lv_obj_get_child(RightContainer, i);
                if (child != chart) {
                    animateFade(child, false);
                }
            }
            
            // Set expanded chart to fill available space
            lv_obj_set_flex_grow(chart, 1);
            
            // If expanding intelligence tile
            if (isIntelligenceTile) {
                LOGD("Expanding intelligence tile");
                // Summary stays visible (always shown)
                lv_obj_clear_flag(summary, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_height(summary, LV_SIZE_CONTENT);
                // Fade in detail with flex_grow to fill remaining space
                lv_obj_set_flex_grow(detail, 1);
                lv_obj_set_height(detail, LV_SIZE_CONTENT);
                animateFade(detail, true, ANIM_DURATION / 2);
                // Tile: flex_grow=1 to fill space, no fixed height
                lv_obj_set_height(chart, LV_SIZE_CONTENT);
                lv_obj_set_style_height(chart, LV_SIZE_CONTENT, 0);
                lv_obj_set_style_max_height(chart, LV_PCT(100), 0);
                lv_obj_set_style_min_height(chart, 0, 0);
            }
            expandedChart = chart;
        }
        // Force layout recalculation
        lv_obj_invalidate(RightContainer);
        lv_obj_update_layout(RightContainer);
        
        // Debug: log all children heights
        uint32_t childCount = lv_obj_get_child_cnt(RightContainer);
        for (uint32_t i = 0; i < childCount; i++) {
            lv_obj_t *child = lv_obj_get_child(RightContainer, i);
            bool hidden = lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN);
            LOGD("Child %d: height=%d, flex_grow=%d, hidden=%d", i, lv_obj_get_height(child), lv_obj_get_style_flex_grow(child, 0), hidden);
        }
        LOGD("Toggle chart: layout updated, chart height=%d", lv_obj_get_height(chart));
    }

    void setModeChangeCallback(InverterModeChangeCallback callback) {
        modeChangeCallback = callback;
    }

    /**
     * Enable or disable intelligence features based on inverter support
     * This controls visibility of intelligence button, mode badge, inverter click menu
     * Can be called before show() - UI will be updated when screen is created
     */
    void setIntelligenceSupported(bool supported) {
        intelligenceSupported = supported;
        
        // Update UI only if screen is initialized
        if (!initialized) return;
        
        // Update clickable state for inverter container
        if (inverterContainer != nullptr) {
            if (supported) {
                lv_obj_add_flag(inverterContainer, LV_OBJ_FLAG_CLICKABLE);
            } else {
                lv_obj_clear_flag(inverterContainer, LV_OBJ_FLAG_CLICKABLE);
            }
        }
        
        // Hide intelligence tile if inverter doesn't support intelligence
        if (intelligencePlanTile != nullptr && !supported) {
            lv_obj_add_flag(intelligencePlanTile, LV_OBJ_FLAG_HIDDEN);
        }
        
        // Intelligence button visibility will be handled in show() method based on this flag
    }

    void showModeChangeSpinner() {
        if (modeChangeSpinner != nullptr) {
            return;  // Already showing
        }
        
        // Create fullscreen overlay
        modeChangeSpinner = lv_obj_create(screen);
        lv_obj_remove_style_all(modeChangeSpinner);
        lv_obj_set_size(modeChangeSpinner, LV_PCT(100), LV_PCT(100));
        lv_obj_add_flag(modeChangeSpinner, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING);
        lv_obj_clear_flag(modeChangeSpinner, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(modeChangeSpinner, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(modeChangeSpinner, LV_OPA_50, 0);
        lv_obj_move_foreground(modeChangeSpinner);
        
        // Create spinner container (white rounded box)
        lv_obj_t* container = lv_obj_create(modeChangeSpinner);
        lv_obj_set_size(container, 120, 120);
        lv_obj_align(container, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_radius(container, 16, 0);
        lv_obj_set_style_bg_color(container, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(container, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(container, 0, 0);
        lv_obj_set_style_shadow_width(container, 32, 0);
        lv_obj_set_style_shadow_opa(container, LV_OPA_30, 0);
        lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
        
        // Create spinner
        lv_obj_t* spinner = lv_spinner_create(container, 1000, 60);
        lv_obj_set_size(spinner, 60, 60);
        lv_obj_center(spinner);
        lv_obj_set_style_arc_width(spinner, 6, LV_PART_MAIN);
        lv_obj_set_style_arc_width(spinner, 6, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(spinner, lv_color_hex(0xE0E0E0), LV_PART_MAIN);
        lv_obj_set_style_arc_color(spinner, lv_color_hex(0x00AAFF), LV_PART_INDICATOR);
    }

    void hideModeChangeSpinner() {
        if (modeChangeSpinner != nullptr) {
            lv_obj_del(modeChangeSpinner);
            modeChangeSpinner = nullptr;
        }
    }

    ~DashboardUI()
    {
        if (clocksTimer)
        {
            lv_timer_del(clocksTimer);
            clocksTimer = nullptr;
        }
    }

    bool isWallboxSmartChecked()
    {
        return lv_obj_get_state(wallboxSmartCheckbox) & LV_STATE_CHECKED;
    }

    /**
     * Update intelligence mode label based on current inverter mode
     * Colors:
     * - SELF_USE: Green (normal operation)
     * - CHARGE_FROM_GRID: Blue (buying cheap electricity)
     * - DISCHARGE_TO_GRID: Orange (selling expensive electricity)
     * - HOLD_BATTERY: Gray (holding)
     * - UNKNOWN: Hidden
     */
    void updateIntelligenceModeLabel(SolarInverterMode_t mode) {
        if (intelligenceModeLabel == nullptr) {
            return;
        }
        
        // Hide badge if intelligence is not supported for this inverter
        if (!intelligenceSupported || mode == SI_MODE_UNKNOWN) {
            lv_obj_add_flag(intelligenceModeLabel, LV_OBJ_FLAG_HIDDEN);
            return;
        }
        
        lv_obj_clear_flag(intelligenceModeLabel, LV_OBJ_FLAG_HIDDEN);
        
        const char* labelText = "";
        lv_color_t bgColor;
        
        if (intelligenceEnabled) {
            // When intelligence is active, show "INTELLIGENCE" badge
            labelText = TR(STR_INTELLIGENCE);
            bgColor = lv_color_hex(0x2196F3);  // Blue
        } else {
            // When intelligence is disabled, show actual inverter mode
            switch (mode) {
                case SI_MODE_SELF_USE:
                    labelText = TR(STR_NORMAL);
                    bgColor = lv_color_hex(_ui_theme_color_loadColor[0]);  // Green (load color)
                    break;
                case SI_MODE_CHARGE_FROM_GRID:
                    labelText = TR(STR_CHARGE);
                    bgColor = lv_color_hex(_ui_theme_color_gridColor[0]);  // Red/Pink (grid color)
                    break;
                case SI_MODE_DISCHARGE_TO_GRID:
                    labelText = TR(STR_DISCHARGE);
                    bgColor = lv_color_hex(_ui_theme_color_batteryColor[0]);  // Blue (battery color)
                    break;
                case SI_MODE_HOLD_BATTERY:
                    labelText = TR(STR_HOLD);
                    bgColor = lv_color_hex(0x888888);  // Gray
                    break;
                default:
                    lv_obj_add_flag(intelligenceModeLabel, LV_OBJ_FLAG_HIDDEN);
                    return;
            }
        }
        
        lv_label_set_text(intelligenceModeLabel, labelText);
        lv_obj_set_style_bg_color(intelligenceModeLabel, bgColor, 0);
        lv_obj_set_style_shadow_color(intelligenceModeLabel, bgColor, 0);
        lv_obj_set_style_shadow_opa(intelligenceModeLabel, 255, 0);
        lv_obj_set_style_text_color(intelligenceModeLabel, lv_color_white(), 0);
    }
    
    /**
     * Update intelligence plan summary with current mode and time until next change
     * @param currentMode Current inverter mode
     * @param plan Array of planned modes for each quarter
     * @param currentQuarter Current quarter index
     * @param totalQuarters Total number of quarters in plan
     */
    void updateIntelligencePlanSummary(SolarInverterMode_t currentMode, const SolarInverterMode_t plan[], int currentQuarter, int totalQuarters, float savings, const char* currency = nullptr) {
        if (intelligenceSummaryBadge == nullptr || intelligenceSummaryTitle == nullptr) return;
        
        // Get mode name and color
        const char* modeName = "";
        lv_color_t badgeColor = lv_color_hex(0x666666);
        switch (currentMode) {
            case SI_MODE_SELF_USE: 
                modeName = TR(STR_NORMAL); 
                badgeColor = lv_color_hex(_ui_theme_color_loadColor[0]);  // Green (load color)
                break;
            case SI_MODE_CHARGE_FROM_GRID: 
                modeName = TR(STR_CHARGE); 
                badgeColor = lv_color_hex(_ui_theme_color_gridColor[0]);  // Red/Pink (grid color)
                break;
            case SI_MODE_DISCHARGE_TO_GRID: 
                modeName = TR(STR_DISCHARGE); 
                badgeColor = lv_color_hex(_ui_theme_color_batteryColor[0]);  // Blue (battery color)
                break;
            case SI_MODE_HOLD_BATTERY: 
                modeName = TR(STR_HOLD); 
                badgeColor = lv_color_hex(0x888888);  // Gray
                break;
            default: 
                modeName = "---"; 
                badgeColor = lv_color_hex(0x666666);
                break;
        }
        
        // Update badge
        lv_label_set_text(intelligenceSummaryBadge, modeName);
        lv_obj_set_style_bg_color(intelligenceSummaryBadge, badgeColor, 0);
        
        // Update savings value - only show if positive
        if (intelligenceSummarySavings != nullptr) {
            if (savings > 0) {
                char savingsText[32];
                const char* currencyStr = currency ? currency : TR(STR_CURRENCY_CZK);
                snprintf(savingsText, sizeof(savingsText), "+%.0f %s", savings, currencyStr);
                lv_obj_set_style_text_color(intelligenceSummarySavings, lv_color_hex(0x00AA00), 0);  // Green for positive
                lv_label_set_text(intelligenceSummarySavings, savingsText);
                lv_obj_clear_flag(intelligenceSummarySavings, LV_OBJ_FLAG_HIDDEN);
            } else {
                // Hide savings if zero or negative
                lv_obj_add_flag(intelligenceSummarySavings, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    
    /**
     * Update upcoming plans in the expanded intelligence tile
     * @param plan Array of planned modes for each quarter
     * @param currentQuarter Current quarter index
     * @param totalQuarters Total number of quarters in plan
     * @param prices Optional electricity prices for detailed reasons
     * @param settings Optional intelligence settings for price calculations
     */
    void updateIntelligenceUpcomingPlans(const SolarInverterMode_t plan[], int currentQuarter, int totalQuarters, 
                                         const ElectricityPriceTwoDays_t* prices = nullptr, 
                                         const SolarIntelligenceSettings_t* settings = nullptr) {
        if (intelligenceUpcomingRows[0] == nullptr) return;
        
        // Helper to get mode name
        auto getModeName = [](SolarInverterMode_t mode) -> const char* {
            switch (mode) {
                case SI_MODE_SELF_USE: return TR(STR_NORMAL);
                case SI_MODE_CHARGE_FROM_GRID: return TR(STR_CHARGE);
                case SI_MODE_DISCHARGE_TO_GRID: return TR(STR_DISCHARGE);
                case SI_MODE_HOLD_BATTERY: return TR(STR_HOLD);
                default: return "---";
            }
        };
        
        auto getModeColor = [](SolarInverterMode_t mode) -> lv_color_t {
            switch (mode) {
                case SI_MODE_SELF_USE: return lv_color_hex(_ui_theme_color_loadColor[0]);  // Green (load color)
                case SI_MODE_CHARGE_FROM_GRID: return lv_color_hex(_ui_theme_color_gridColor[0]);  // Red/Pink (grid color)
                case SI_MODE_DISCHARGE_TO_GRID: return lv_color_hex(_ui_theme_color_batteryColor[0]);  // Blue (battery color)
                case SI_MODE_HOLD_BATTERY: return lv_color_hex(0x888888);  // Gray
                default: return lv_color_hex(0x666666);
            }
        };
        
        // Count total mode changes first
        int totalChanges = 0;
        SolarInverterMode_t countMode = (currentQuarter >= 0 && currentQuarter < totalQuarters) ? plan[currentQuarter] : SI_MODE_UNKNOWN;
        for (int q = currentQuarter + 1; q < totalQuarters; q++) {
            if (plan[q] != countMode && plan[q] != SI_MODE_UNKNOWN) {
                totalChanges++;
                countMode = plan[q];
            }
        }
        
        // Find next VISIBLE_PLAN_ROWS mode changes
        int foundChanges = 0;
        SolarInverterMode_t lastMode = (currentQuarter >= 0 && currentQuarter < totalQuarters) ? plan[currentQuarter] : SI_MODE_UNKNOWN;
        
        for (int q = currentQuarter + 1; q < totalQuarters && foundChanges < VISIBLE_PLAN_ROWS; q++) {
            if (plan[q] != lastMode && plan[q] != SI_MODE_UNKNOWN) {
                // Calculate time
                int hour = (q % QUARTERS_OF_DAY) / 4;
                int minute = ((q % QUARTERS_OF_DAY) % 4) * 15;
                
                char timeStr[8];
                snprintf(timeStr, sizeof(timeStr), "%02d:%02d", hour, minute);
                
                lv_label_set_text(intelligenceUpcomingTimes[foundChanges], timeStr);
                lv_label_set_text(intelligenceUpcomingModes[foundChanges], getModeName(plan[q]));
                // Set badge background color (text stays white)
                lv_obj_set_style_bg_color(intelligenceUpcomingModes[foundChanges], getModeColor(plan[q]), 0);
                
                // Build compact reason (single line)
                char reasonBuf[64];
                if (prices != nullptr && settings != nullptr && q < (prices->hasTomorrowData ? 192 : 96)) {
                    float spotPrice = prices->prices[q].electricityPrice;
                    float buyPrice = calculateBuyPrice(spotPrice, *settings);
                    float sellPrice = calculateSellPrice(spotPrice, *settings);
                    
                    switch (plan[q]) {
                        case SI_MODE_SELF_USE:
                            snprintf(reasonBuf, sizeof(reasonBuf), "%s %.1f %s", TR(STR_BATTERY_CHEAPER_THAN), buyPrice, prices->currency);
                            break;
                        case SI_MODE_CHARGE_FROM_GRID:
                            snprintf(reasonBuf, sizeof(reasonBuf), "%s %.1f %s", TR(STR_LOW_PRICE), buyPrice, prices->currency);
                            break;
                        case SI_MODE_DISCHARGE_TO_GRID:
                            snprintf(reasonBuf, sizeof(reasonBuf), "%s %.1f %s", TR(STR_HIGH_PRICE), sellPrice, prices->currency);
                            break;
                        case SI_MODE_HOLD_BATTERY:
                            snprintf(reasonBuf, sizeof(reasonBuf), "%s", TR(STR_WAITING_BETTER_PRICE));
                            break;
                        default:
                            reasonBuf[0] = '\0';
                    }
                } else {
                    // Fallback without prices
                    switch (plan[q]) {
                        case SI_MODE_SELF_USE: 
                            snprintf(reasonBuf, sizeof(reasonBuf), "%s", TR(STR_USING_BATTERY));
                            break;
                        case SI_MODE_CHARGE_FROM_GRID: 
                            snprintf(reasonBuf, sizeof(reasonBuf), "%s", TR(STR_LOW_ELECTRICITY_PRICE));
                            break;
                        case SI_MODE_DISCHARGE_TO_GRID: 
                            snprintf(reasonBuf, sizeof(reasonBuf), "%s", TR(STR_HIGH_ELECTRICITY_PRICE));
                            break;
                        case SI_MODE_HOLD_BATTERY: 
                            snprintf(reasonBuf, sizeof(reasonBuf), "%s", TR(STR_HOLDING_FOR_LATER));
                            break;
                        default:
                            reasonBuf[0] = '\0';
                    }
                }
                lv_label_set_text(intelligenceUpcomingReasons[foundChanges], reasonBuf);
                
                // Show/hide connecting line (hide for last visible item)
                if (intelligenceUpcomingLines[foundChanges] != nullptr) {
                    lv_obj_clear_flag(intelligenceUpcomingLines[foundChanges], LV_OBJ_FLAG_HIDDEN);
                }
                
                lv_obj_clear_flag(intelligenceUpcomingRows[foundChanges], LV_OBJ_FLAG_HIDDEN);
                
                lastMode = plan[q];
                foundChanges++;
            }
        }
        
        // Hide line on last visible item
        if (foundChanges > 0 && intelligenceUpcomingLines[foundChanges - 1] != nullptr) {
            lv_obj_add_flag(intelligenceUpcomingLines[foundChanges - 1], LV_OBJ_FLAG_HIDDEN);
        }
        
        // Hide unused rows
        for (int i = foundChanges; i < VISIBLE_PLAN_ROWS; i++) {
            lv_obj_add_flag(intelligenceUpcomingRows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    
    /**
     * Update statistics in the expanded intelligence tile
     * @param productionKWh Estimated production in kWh
     * @param consumptionKWh Estimated consumption in kWh
     */
    void updateIntelligenceStats(float productionKWh, float consumptionKWh) {
        if (intelligenceStatsProduction == nullptr) return;
        
        char buf[16];
        
        // Production - value only, unit is separate label
        snprintf(buf, sizeof(buf), "~%.1f", productionKWh);
        lv_label_set_text(intelligenceStatsProduction, buf);
        
        // Consumption - value only, unit is separate label
        snprintf(buf, sizeof(buf), "~%.1f", consumptionKWh);
        lv_label_set_text(intelligenceStatsConsumption, buf);
    }

    void show() override
    {
        hide();  // Clean up previous
        createScreen();  // Creates 'screen' from BaseUI
        createUI();  // Creates all UI elements
        initDashboardExtras();  // Create intelligence tile and setup event handlers
        
        // Clear all labels before loading screen to avoid placeholder glitch
        clearAllLabels();
        
        // Hide buttons initially - they will appear on touch
        lv_obj_add_flag(settingsButton, LV_OBJ_FLAG_HIDDEN);
        if (intelligenceButton != nullptr) {
            lv_obj_add_flag(intelligenceButton, LV_OBJ_FLAG_HIDDEN);
        }
        if (ipBadge != nullptr) {
            lv_obj_add_flag(ipBadge, LV_OBJ_FLAG_HIDDEN);
        }
        
        // Reset touch timer
        lastTouchMillis = 0;
        shownMillis = millis();
        
        loadScreen();
    }

    void hide() override {
        // Reset all UI element pointers
        LeftContainer = nullptr;
        pvContainer = nullptr;
        Container11 = nullptr;
        pvLabel = nullptr;
        pvUnitLabel = nullptr;
        Container1 = nullptr;
        pvStringsContainer = nullptr;
        pv1Label = nullptr;
        pv2Label = nullptr;
        pvStringsContainer1 = nullptr;
        pv3Label = nullptr;
        pv4Label = nullptr;
        Image6 = nullptr;
        batteryContainer = nullptr;
        batteryTemperatureLabel = nullptr;
        Container23 = nullptr;
        socLabel = nullptr;
        socLabel1 = nullptr;
        Container17 = nullptr;
        batteryTimeLabel = nullptr;
        Container24 = nullptr;
        batteryPowerLabel = nullptr;
        Container25 = nullptr;
        batteryImage = nullptr;
        loadContainer = nullptr;
        Image8 = nullptr;
        Container14 = nullptr;
        shellyContainer = nullptr;
        shellyCountLabel = nullptr;
        shellyPowerLabel = nullptr;
        Container20 = nullptr;
        loadPowerLabel = nullptr;
        loadPowerUnitLabel = nullptr;
        gridContainer = nullptr;
        Image9 = nullptr;
        smartMeterContainer = nullptr;
        Container19 = nullptr;
        meterPowerBarL1 = nullptr;
        meterPowerLabelL1 = nullptr;
        Container2 = nullptr;
        meterPowerBarL2 = nullptr;
        meterPowerLabelL2 = nullptr;
        Container13 = nullptr;
        meterPowerBarL3 = nullptr;
        meterPowerLabelL3 = nullptr;
        Container22 = nullptr;
        feedInPowerLabel = nullptr;
        feedInPowerUnitLabel = nullptr;
        selfUsePercentLabel = nullptr;
        inverterContainer = nullptr;
        Image5 = nullptr;
        dongleFWVersion = nullptr;
        inverterTemperatureLabel = nullptr;
        Container26 = nullptr;
        inverterPowerLabel = nullptr;
        inverterPowerUnitLabel = nullptr;
        inverterPhasesContainer = nullptr;
        Container29 = nullptr;
        inverterPowerBar1 = nullptr;
        inverterPowerL1Label = nullptr;
        Container3 = nullptr;
        inverterPowerBar2 = nullptr;
        inverterPowerL2Label = nullptr;
        Container28 = nullptr;
        inverterPowerBar3 = nullptr;
        inverterPowerL3Label = nullptr;
        statusLabel = nullptr;
        wallboxContainer = nullptr;
        wallboxTemperatureLabel = nullptr;
        wallboxLogoSolaxImage = nullptr;
        wallboxLogoEcovolterImage = nullptr;
        wallboxPowerContainer = nullptr;
        wallboxPowerLabel = nullptr;
        wallboxPowerUnitLabel = nullptr;
        wallboxEnergyContainer = nullptr;
        wallboxEnergyLabel = nullptr;
        wallboxSmartCheckbox = nullptr;
        RightContainer = nullptr;
        TopRightContainer = nullptr;
        Container33 = nullptr;
        pvStatsContainer = nullptr;
        Image11 = nullptr;
        Container34 = nullptr;
        Container27 = nullptr;
        yieldTodayLabel = nullptr;
        yieldTodayUnitLabel = nullptr;
        Container5 = nullptr;
        yieldTotalLabel = nullptr;
        yieldTotalUnitLabel = nullptr;
        loadStatsContainer = nullptr;
        Image13 = nullptr;
        Container37 = nullptr;
        Container8 = nullptr;
        loadTodayLabel = nullptr;
        loadTodayUnitLabel = nullptr;
        Container30 = nullptr;
        selfUseTodayLabel = nullptr;
        selfUseTodayUnitLabel = nullptr;
        Container36 = nullptr;
        batteryStatsContainer = nullptr;
        Image12 = nullptr;
        Container35 = nullptr;
        Container6 = nullptr;
        batteryChargedTodayLabel = nullptr;
        batteryChargedTodayUnitLabel = nullptr;
        Container9 = nullptr;
        batteryDischargedTodayLabel = nullptr;
        batteryDischargedTodayUnitLabel = nullptr;
        gridStatsContainer = nullptr;
        Image14 = nullptr;
        Container38 = nullptr;
        Container7 = nullptr;
        gridSellTodayLabel = nullptr;
        gridSellTodayUnitLabel = nullptr;
        Container10 = nullptr;
        gridBuyTodayLabel = nullptr;
        gridBuyTodayUnitLabel = nullptr;
        clocksLabel = nullptr;
        RightBottomContainer = nullptr;
        Chart1 = nullptr;
        spotPriceContainer = nullptr;
        currentPriceLabel = nullptr;
        settingsButton = nullptr;
        
        // Call base class hide
        BaseUI::hide();
    }

    
    void clearAllLabels()
    {
        // Clear power labels to avoid showing SquareLine placeholder values
        lv_label_set_text(pvLabel, "-");
        lv_label_set_text(pv1Label, "");
        lv_label_set_text(pv2Label, "");
        if (pv3Label) lv_label_set_text(pv3Label, "");
        if (pv4Label) lv_label_set_text(pv4Label, "");
        lv_label_set_text(socLabel, "-");
        lv_label_set_text(batteryPowerLabel, "");
        lv_label_set_text(batteryTemperatureLabel, "");
        lv_label_set_text(batteryTimeLabel, "");
        lv_label_set_text(loadPowerLabel, "-");
        lv_label_set_text(feedInPowerLabel, "-");
        lv_label_set_text(selfUsePercentLabel, "");
        lv_label_set_text(meterPowerLabelL1, "");
        lv_label_set_text(meterPowerLabelL2, "");
        lv_label_set_text(meterPowerLabelL3, "");
        lv_label_set_text(inverterPowerLabel, "-");
        lv_label_set_text(inverterTemperatureLabel, "");
        lv_label_set_text(dongleFWVersion, "");
        lv_label_set_text(shellyCountLabel, "");
        lv_label_set_text(shellyPowerLabel, "");
    }

    int getSelfUsePowerPercent(InverterData_t &inverterData)
    {
        int gridPower = inverterData.gridPowerL1 + inverterData.gridPowerL2 + inverterData.gridPowerL3;
        return constrain(inverterData.loadPower > 0 ? (100 * (inverterData.loadPower + gridPower)) / inverterData.loadPower : 0, 0, 100);
    }

    void update(InverterData_t &inverterData, InverterData_t &previousInverterData, MedianPowerSampler &uiMedianPowerSampler, ShellyResult_t &shellyResult, ShellyResult_t &previousShellyResult, WallboxResult_t &wallboxResult, WallboxResult_t &previousWallboxResult, SolarChartDataProvider &solarChartDataProvider, ElectricityPriceTwoDays_t &electricityPriceResult, ElectricityPriceTwoDays_t &previousElectricityPriceResult, int wifiSignalPercent)
    {
        // hide settings and intelligence buttons after timeout from last touch
        if (lastTouchMillis > 0 && millis() - lastTouchMillis > BUTTONS_HIDE_TIMEOUT_MS)
        {
            lv_obj_add_flag(settingsButton, LV_OBJ_FLAG_HIDDEN);
            if (intelligenceButton != nullptr && intelligenceSupported) {
                lv_obj_add_flag(intelligenceButton, LV_OBJ_FLAG_HIDDEN);
            }
            if (ipBadge != nullptr) {
                lv_obj_add_flag(ipBadge, LV_OBJ_FLAG_HIDDEN);
            }
        }
        int previousGridPower = previousInverterData.gridPowerL1 + previousInverterData.gridPowerL2 + previousInverterData.gridPowerL3;
        int gridPower = inverterData.gridPowerL1 + inverterData.gridPowerL2 + inverterData.gridPowerL3;
        // Záporný výkon střídače zobrazujeme jako 0
        int previousInverterPower = max(0, previousInverterData.inverterOutpuPowerL1 + previousInverterData.inverterOutpuPowerL2 + previousInverterData.inverterOutpuPowerL3);
        int inverterPower = max(0, inverterData.inverterOutpuPowerL1 + inverterData.inverterOutpuPowerL2 + inverterData.inverterOutpuPowerL3);

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
        if (gridPower < 0)
        {
            inPower += abs(gridPower);
        }

        int outPower = inverterData.loadPower;
        if (inverterData.batteryPower > 0)
        {
            outPower += inverterData.batteryPower;
        }
        if (gridPower > 0)
        {
            outPower += gridPower;
        }
        int totalPhasePower = max(0, inverterData.inverterOutpuPowerL1) + max(0, inverterData.inverterOutpuPowerL2) + max(0, inverterData.inverterOutpuPowerL3);
        int l1PercentUsage = totalPhasePower > 0 ? (100 * max(0, inverterData.inverterOutpuPowerL1)) / totalPhasePower : 0;
        int l2PercentUsage = totalPhasePower > 0 ? (100 * max(0, inverterData.inverterOutpuPowerL2)) / totalPhasePower : 0;
        int l3PercentUsage = totalPhasePower > 0 ? (100 * max(0, inverterData.inverterOutpuPowerL3)) / totalPhasePower : 0;
        bool hasPhases = max(0, inverterData.inverterOutpuPowerL2) > 0 || max(0, inverterData.inverterOutpuPowerL3) > 0;

        if (hasPhases)
        {
            // show inverter phase container
            lv_obj_clear_flag(inverterPhasesContainer, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(inverterPhasesContainer, LV_OBJ_FLAG_HIDDEN);
        }

        bool hasGridPhases = inverterData.gridPowerL2 != 0 || inverterData.gridPowerL3 != 0;
        if (hasGridPhases)
        {
            // show grid phase container
            lv_obj_clear_flag(meterPowerBarL1, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(meterPowerBarL2, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(meterPowerBarL3, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(meterPowerLabelL1, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(meterPowerLabelL2, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(meterPowerLabelL3, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            // hide grid phase container
            lv_obj_add_flag(meterPowerBarL1, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(meterPowerBarL2, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(meterPowerBarL3, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(meterPowerLabelL1, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(meterPowerLabelL2, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(meterPowerLabelL3, LV_OBJ_FLAG_HIDDEN);
        }

        lv_color_t black = lv_color_make(0, 0, 0);
        lv_color_t white = lv_color_make(255, 255, 255);

        lv_color_t textColor = isDarkMode ? white : black;
        lv_color_t containerBackground = isDarkMode ? black : white;

        pvPowerTextAnimator.animate(pvLabel,
                                    previousInverterData.pv1Power + previousInverterData.pv2Power + previousInverterData.pv3Power + previousInverterData.pv4Power,
                                    inverterData.pv1Power + inverterData.pv2Power + inverterData.pv3Power + inverterData.pv4Power);
        lv_label_set_text(pv1Label, format(POWER, inverterData.pv1Power, 1.0f, true).formatted.c_str());
        lv_label_set_text(pv2Label, format(POWER, inverterData.pv2Power, 1.0f, true).formatted.c_str());
        lv_label_set_text(pv3Label, format(POWER, inverterData.pv3Power, 1.0f, true).formatted.c_str());
        lv_label_set_text(pv4Label, format(POWER, inverterData.pv4Power, 1.0f, true).formatted.c_str());

        if (inverterData.pv1Power == 0 || inverterData.pv2Power == 0)
        { // hide
            lv_obj_add_flag(pvStringsContainer, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_clear_flag(pvStringsContainer, LV_OBJ_FLAG_HIDDEN);
        }
        if (inverterData.pv3Power == 0 && inverterData.pv4Power == 0)
        {
            lv_obj_add_flag(pvStringsContainer1, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_clear_flag(pvStringsContainer1, LV_OBJ_FLAG_HIDDEN);
        }

        lv_label_set_text_fmt(inverterTemperatureLabel, "%d°C", inverterData.inverterTemperature);
        if (inverterData.inverterTemperature > 50)
        {
            lv_obj_set_style_bg_color(inverterTemperatureLabel, red, 0);
            lv_obj_set_style_shadow_color(inverterTemperatureLabel, red, 0);
            lv_obj_set_style_text_color(inverterTemperatureLabel, white, 0);
        }
        else if (inverterData.inverterTemperature > 40)
        {
            lv_obj_set_style_bg_color(inverterTemperatureLabel, orange, 0);
            lv_obj_set_style_shadow_color(inverterTemperatureLabel, orange, 0);
            lv_obj_set_style_text_color(inverterTemperatureLabel, black, 0);
        }
        else
        {
            lv_obj_set_style_bg_color(inverterTemperatureLabel, green, 0);
            lv_obj_set_style_shadow_color(inverterTemperatureLabel, green, 0);
            lv_obj_set_style_text_color(inverterTemperatureLabel, white, 0);
        }

        if (inverterData.inverterTemperature == 0)
        {
            lv_obj_add_flag(inverterTemperatureLabel, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_clear_flag(inverterTemperatureLabel, LV_OBJ_FLAG_HIDDEN);
        }

        inverterPowerTextAnimator.animate(inverterPowerLabel, previousInverterPower, inverterPower);
        pvBackgroundAnimator.animate(pvContainer, ((inverterData.pv1Power + inverterData.pv2Power + inverterData.pv3Power + inverterData.pv4Power) > 0) ? lv_color_hex(_ui_theme_color_pvColor[0]) : containerBackground);
        lv_label_set_text(inverterPowerUnitLabel, format(POWER, inverterPower).unit.c_str());

        // phases - záporný výkon střídače zobrazujeme jako 0
        int displayL1Power = max(0, inverterData.inverterOutpuPowerL1);
        int displayL2Power = max(0, inverterData.inverterOutpuPowerL2);
        int displayL3Power = max(0, inverterData.inverterOutpuPowerL3);
        
        lv_label_set_text(inverterPowerL1Label, format(POWER, displayL1Power, 1.0f, false).formatted.c_str());
        lv_bar_set_value(inverterPowerBar1, min(2400, displayL1Power), LV_ANIM_ON);
        lv_obj_set_style_bg_color(inverterPowerBar1, l1PercentUsage > 50 && displayL1Power > 1200 ? red : textColor, LV_PART_INDICATOR);
        lv_obj_set_style_text_color(inverterPowerL1Label, l1PercentUsage > 50 && displayL1Power > 1200 ? red : textColor, 0);
        lv_label_set_text(inverterPowerL2Label, format(POWER, displayL2Power, 1.0f, false).formatted.c_str());
        lv_bar_set_value(inverterPowerBar2, min(2400, displayL2Power), LV_ANIM_ON);
        lv_obj_set_style_bg_color(inverterPowerBar2, l2PercentUsage > 50 && displayL2Power > 1200 ? red : textColor, LV_PART_INDICATOR);
        lv_obj_set_style_text_color(inverterPowerL2Label, l2PercentUsage > 50 && displayL2Power > 1200 ? red : textColor, 0);
        lv_label_set_text(inverterPowerL3Label, format(POWER, displayL3Power, 1.0f, false).formatted.c_str());
        lv_bar_set_value(inverterPowerBar3, min(2400, displayL3Power), LV_ANIM_ON);
        lv_obj_set_style_bg_color(inverterPowerBar3, l3PercentUsage > 50 && displayL3Power > 1200 ? red : textColor, LV_PART_INDICATOR);
        lv_obj_set_style_text_color(inverterPowerL3Label, l3PercentUsage > 50 && displayL3Power > 1200 ? red : textColor, 0);

        // grid phases
        lv_label_set_text(meterPowerLabelL1, format(POWER, inverterData.gridPowerL1, 1.0f, false).formatted.c_str());
        lv_bar_set_value(meterPowerBarL1, max((int32_t)-2400, min((int32_t)2400, inverterData.gridPowerL1)), LV_ANIM_ON);
        lv_obj_set_style_bg_color(meterPowerBarL1, inverterData.gridPowerL1 < 0 ? red : textColor, LV_PART_INDICATOR);
        //lv_obj_set_style_text_color(meterPowerLabelL1, inverterData.gridPowerL1 < 0 ? red : textColor, 0);
        lv_label_set_text(meterPowerLabelL2, format(POWER, inverterData.gridPowerL2, 1.0f, false).formatted.c_str());
        lv_bar_set_value(meterPowerBarL2, max((int32_t)-2400, min((int32_t)2400, inverterData.gridPowerL2)), LV_ANIM_ON);
        lv_obj_set_style_bg_color(meterPowerBarL2, inverterData.gridPowerL2 < 0 ? red : textColor, LV_PART_INDICATOR);
        //lv_obj_set_style_text_color(meterPowerLabelL2, inverterData.gridPowerL2 < 0 ? red : textColor, 0);
        lv_label_set_text(meterPowerLabelL3, format(POWER, inverterData.gridPowerL3, 1.0f, false).formatted.c_str());
        lv_bar_set_value(meterPowerBarL3, max((int32_t)-2400, min((int32_t)2400, inverterData.gridPowerL3)), LV_ANIM_ON);
        lv_obj_set_style_bg_color(meterPowerBarL3, inverterData.gridPowerL3 < 0 ? red : textColor, LV_PART_INDICATOR);
        //lv_obj_set_style_text_color(meterPowerLabelL3, inverterData.gridPowerL3 < 0 ? red : textColor, 0);

        loadPowerTextAnimator.animate(loadPowerLabel, previousInverterData.loadPower, inverterData.loadPower);
        lv_label_set_text(loadPowerUnitLabel, format(POWER, inverterData.loadPower).unit.c_str());
        feedInPowerTextAnimator.animate(feedInPowerLabel, abs(previousGridPower), abs(gridPower));
        lv_label_set_text(feedInPowerUnitLabel, format(POWER, abs(gridPower)).unit.c_str());
        gridBackgroundAnimator.animate(gridContainer, (gridPower < 0) ? lv_color_hex(_ui_theme_color_gridColor[0]) : containerBackground);
        lv_label_set_text_fmt(socLabel, (inverterData.socApproximated ? "~%d" : "%d"), inverterData.soc);

        lv_label_set_text(batteryPowerLabel, format(POWER, abs(inverterData.batteryPower), 1.0f, true).formatted.c_str());
        batteryBackgroundAnimator.animate(batteryContainer, ((inverterData.batteryPower) < 0) ? lv_color_hex(_ui_theme_color_batteryColor[0]) : containerBackground);
        updateBatteryIcon(inverterData.soc);
        if (inverterData.batteryCapacityWh > 0)
        {
            if (abs(inverterData.batteryPower) > 100)
            {

                if (inverterData.batteryPower < 0)
                {
                    int capacityRemainingWh = (inverterData.soc - inverterData.minSoc) * inverterData.batteryCapacityWh / 100;
                    int secondsRemaining = (3600 * capacityRemainingWh) / abs(inverterData.batteryPower);
                    lv_label_set_text_fmt(batteryTimeLabel, "%s - %d%%", formatTimeSpan(secondsRemaining).c_str(), inverterData.minSoc);
                }
                else if (inverterData.batteryPower > 0)
                {
                    int availableCapacityWh = (inverterData.maxSoc - inverterData.soc) * inverterData.batteryCapacityWh / 100;
                    int secondsRemaining = (3600 * availableCapacityWh) / inverterData.batteryPower;
                    lv_label_set_text_fmt(batteryTimeLabel, "%s - %d%%", formatTimeSpan(secondsRemaining).c_str(), inverterData.maxSoc);
                }
            }
            else
            {
                lv_label_set_text(batteryTimeLabel, "");
            }
        }
        else
        {
            lv_label_set_text(batteryTimeLabel, "");
        }

        lv_label_set_text_fmt(batteryTemperatureLabel, "%d°C", inverterData.batteryTemperature);

        if (inverterData.batteryTemperature > 40)
        {
            lv_obj_set_style_bg_color(batteryTemperatureLabel, red, 0);
            lv_obj_set_style_shadow_color(batteryTemperatureLabel, red, 0);
            lv_obj_set_style_text_color(batteryTemperatureLabel, white, 0);
        }
        else if (inverterData.batteryTemperature > 30)
        {
            lv_obj_set_style_bg_color(batteryTemperatureLabel, orange, 0);
            lv_obj_set_style_shadow_color(batteryTemperatureLabel, orange, 0);
            lv_obj_set_style_text_color(batteryTemperatureLabel, black, 0);
        }
        else
        {
            lv_obj_set_style_bg_color(batteryTemperatureLabel, green, 0);
            lv_obj_set_style_shadow_color(batteryTemperatureLabel, green, 0);
            lv_obj_set_style_text_color(batteryTemperatureLabel, white, 0);
        }

        lv_label_set_text_fmt(selfUsePercentLabel, "%d%%", getSelfUsePowerPercent(inverterData));

        if (getSelfUsePowerPercent(inverterData) > 50)
        {
            selfUseBackgroundAnimator.animate(selfUsePercentLabel, green);
            // lv_obj_set_style_bg_color(selfUsePercentLabel, green, 0);
        }
        else if (getSelfUsePowerPercent(inverterData) > 30)
        {
            selfUseBackgroundAnimator.animate(selfUsePercentLabel, orange);
            // lv_obj_set_style_bg_color(selfUsePercentLabel, orange, 0);
        }
        else
        {
            selfUseBackgroundAnimator.animate(selfUsePercentLabel, red);
            // lv_obj_set_style_bg_color(selfUsePercentLabel, red, 0);
        }
        lv_label_set_text(yieldTodayLabel, format(ENERGY, inverterData.pvToday * 1000.0, 1).value.c_str());
        lv_label_set_text(yieldTodayUnitLabel, format(ENERGY, inverterData.pvToday * 1000.0, 1).unit.c_str());
        lv_label_set_text(yieldTotalLabel, format(ENERGY, inverterData.pvTotal * 1000.0, 1, true).value.c_str());
        lv_label_set_text(yieldTotalUnitLabel, format(ENERGY, inverterData.pvTotal * 1000.0, 1, true).unit.c_str());
        lv_label_set_text(gridSellTodayLabel, ("+" + format(ENERGY, inverterData.gridSellToday * 1000.0, 1).value).c_str());
        lv_label_set_text(gridSellTodayUnitLabel, format(ENERGY, inverterData.gridSellToday * 1000.0, 1).unit.c_str());
        lv_label_set_text(gridBuyTodayLabel, ("-" + format(ENERGY, inverterData.gridBuyToday * 1000.0, 1).value).c_str());
        lv_obj_set_style_text_color(gridBuyTodayLabel, red, 0);
        lv_obj_set_style_text_color(gridBuyTodayUnitLabel, red, 0);
        lv_label_set_text(gridBuyTodayUnitLabel, format(ENERGY, inverterData.gridBuyToday * 1000.0, 1).unit.c_str());
        lv_label_set_text(batteryChargedTodayLabel, ("+" + format(ENERGY, inverterData.batteryChargedToday * 1000.0, 1).value).c_str());
        lv_label_set_text(batteryChargedTodayUnitLabel, (format(ENERGY, inverterData.batteryChargedToday * 1000.0, 1).unit).c_str());
        lv_label_set_text(batteryDischargedTodayLabel, ("-" + format(ENERGY, inverterData.batteryDischargedToday * 1000.0, 1).value).c_str());
        lv_label_set_text(batteryDischargedTodayUnitLabel, (format(ENERGY, inverterData.batteryDischargedToday * 1000.0, 1).unit).c_str());
        lv_obj_set_style_text_color(batteryDischargedTodayLabel, red, 0);
        lv_obj_set_style_text_color(batteryDischargedTodayUnitLabel, red, 0);
        lv_label_set_text(loadTodayLabel, format(ENERGY, inverterData.loadToday * 1000.0, 1).value.c_str());
        lv_label_set_text(loadTodayUnitLabel, format(ENERGY, inverterData.loadToday * 1000.0, 1).unit.c_str());

        lv_label_set_text_fmt(selfUseTodayLabel, "%d", selfUseEnergyTodayPercent);
        if (selfUseEnergyTodayPercent > 50)
        {
            lv_obj_set_style_text_color(selfUseTodayLabel, green, 0);
            lv_obj_set_style_text_color(selfUseTodayUnitLabel, green, 0);
        }
        else if (selfUseEnergyTodayPercent > 30)
        {
            lv_obj_set_style_text_color(selfUseTodayLabel, orange, 0);
            lv_obj_set_style_text_color(selfUseTodayUnitLabel, orange, 0);
        }
        else
        {
            lv_obj_set_style_text_color(selfUseTodayLabel, red, 0);
            lv_obj_set_style_text_color(selfUseTodayUnitLabel, red, 0);
        }

        if (inverterData.hasBattery)
        {
            lv_obj_clear_flag(batteryContainer, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(batteryStatsContainer, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(batteryContainer, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(batteryStatsContainer, LV_OBJ_FLAG_HIDDEN);
        }

        if (shellyResult.pairedCount > 0)
        {
            lv_obj_clear_flag(shellyContainer, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(shellyContainer, LV_OBJ_FLAG_HIDDEN);
        }
        lv_label_set_text(shellyPowerLabel, format(POWER, shellyResult.totalPower).formatted.c_str());
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

            lv_label_set_text_fmt(shellyCountLabel, "%d%% / %d / %d", uiPercent, shellyResult.activeCount, shellyResult.pairedCount);
        }
        else
        {
            lv_label_set_text_fmt(shellyCountLabel, "%d / %d", shellyResult.activeCount, shellyResult.pairedCount);
        }

        wallboxPowerTextAnimator.animate(wallboxPowerLabel, previousWallboxResult.chargingPower, wallboxResult.chargingPower);
        lv_label_set_text(wallboxPowerUnitLabel, format(POWER, wallboxResult.chargingPower).unit.c_str());
        wallboxBackgroundAnimator.animate(wallboxContainer, wallboxResult.chargingPower > 0 ? /*orange*/ containerBackground : containerBackground);
        if (wallboxResult.chargingControlEnabled)
        {
            // show charging control
            lv_obj_clear_flag(wallboxSmartCheckbox, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(wallboxSmartCheckbox, LV_OBJ_FLAG_HIDDEN);
        }
        if (wallboxResult.evConnected)
        {
            lv_obj_clear_flag(wallboxPowerContainer, LV_OBJ_FLAG_HIDDEN);

            // charged energy
            if (wallboxResult.chargedEnergy > 0)
            {
                lv_label_set_text(wallboxEnergyLabel, format(ENERGY, wallboxResult.chargedEnergy * 1000.0, 1).formatted.c_str());
                lv_obj_clear_flag(wallboxEnergyContainer, LV_OBJ_FLAG_HIDDEN);
            }
            else
            {
                lv_obj_add_flag(wallboxEnergyContainer, LV_OBJ_FLAG_HIDDEN);
            }
        }
        else
        {
            lv_obj_add_flag(wallboxPowerContainer, LV_OBJ_FLAG_HIDDEN);

            // charged total energy
            if (wallboxResult.totalChargedEnergy > 0)
            {
                lv_label_set_text(wallboxEnergyLabel, format(ENERGY, wallboxResult.totalChargedEnergy * 1000.0, 1, true).formatted.c_str());
                lv_obj_clear_flag(wallboxEnergyContainer, LV_OBJ_FLAG_HIDDEN);
            }
            else
            {
                lv_obj_add_flag(wallboxEnergyContainer, LV_OBJ_FLAG_HIDDEN);
            }
        }

        if (wallboxResult.updated > 0)
        {
            // show container
            lv_obj_clear_flag(wallboxContainer, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(wallboxContainer, LV_OBJ_FLAG_HIDDEN);
        }

        // wallbox temperature
        if (wallboxResult.temperature > 0)
        {
            lv_label_set_text_fmt(wallboxTemperatureLabel, "%d°C", wallboxResult.temperature);
            if (wallboxResult.temperature > 40)
            {
                lv_obj_set_style_bg_color(wallboxTemperatureLabel, red, 0);
                lv_obj_set_style_shadow_color(wallboxTemperatureLabel, red, 0);
                lv_obj_set_style_text_color(wallboxTemperatureLabel, white, 0);
            }
            else if (wallboxResult.temperature > 30)
            {
                lv_obj_set_style_bg_color(wallboxTemperatureLabel, orange, 0);
                lv_obj_set_style_shadow_color(wallboxTemperatureLabel, orange, 0);
                lv_obj_set_style_text_color(wallboxTemperatureLabel, black, 0);
            }
            else
            {
                lv_obj_set_style_bg_color(wallboxTemperatureLabel, green, 0);
                lv_obj_set_style_shadow_color(wallboxTemperatureLabel, green, 0);
                lv_obj_set_style_text_color(wallboxTemperatureLabel, white, 0);
            }
            lv_obj_clear_flag(wallboxTemperatureLabel, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(wallboxTemperatureLabel, LV_OBJ_FLAG_HIDDEN);
        }

        // hide all logos
        lv_obj_add_flag(wallboxLogoEcovolterImage, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(wallboxLogoSolaxImage, LV_OBJ_FLAG_HIDDEN);

        switch (wallboxResult.type)
        {
        case WALLBOX_TYPE_SOLAX:
            // show solax logo
            lv_obj_clear_flag(wallboxLogoSolaxImage, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_shadow_color(wallboxContainer, lv_color_hex(_ui_theme_color_pvColor[0]), 0);
            break;
        case WALLBOX_TYPE_ECOVOLTER_PRO_V2:
            // show ecovolter logo
            lv_obj_clear_flag(wallboxLogoEcovolterImage, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_shadow_color(wallboxContainer, lv_color_hex(_ui_theme_color_loadColor[0]), 0);
            break;
        default:
            break;
        }

        updateSolarChart(inverterData, solarChartDataProvider, isDarkMode);

        lv_obj_set_style_text_color(statusLabel, lv_palette_main(LV_PALETTE_DEEP_ORANGE), 0);

        switch (inverterData.status)
        {
        case DONGLE_STATUS_OK:
            lv_obj_set_style_text_color(statusLabel, lv_palette_main(LV_PALETTE_GREY), 0);
            lv_label_set_text_fmt(statusLabel, "%s %d%%", inverterData.sn.c_str(), wifiSignalPercent);

            lv_label_set_text(dongleFWVersion, inverterData.dongleFWVersion.c_str());
            if (inverterData.dongleFWVersion.isEmpty())
            {
                lv_obj_add_flag(dongleFWVersion, LV_OBJ_FLAG_HIDDEN);
            }
            else
            {
                lv_obj_clear_flag(dongleFWVersion, LV_OBJ_FLAG_HIDDEN);
            }
            break;
        case DONGLE_STATUS_CONNECTION_ERROR:
            lv_label_set_text(statusLabel, TR(STR_CONNECTION_ERROR));
            break;
        case DONGLE_STATUS_HTTP_ERROR:
            lv_label_set_text(statusLabel, TR(STR_HTTP_ERROR));
            break;
        case DONGLE_STATUS_JSON_ERROR:
            lv_label_set_text(statusLabel, TR(STR_JSON_ERROR));
            break;
        case DONGLE_STATUS_WIFI_DISCONNECTED:
            lv_label_set_text(statusLabel, TR(STR_WIFI_DISCONNECTED));
            break;
        default:
            lv_label_set_text(statusLabel, TR(STR_UNKNOWN_ERROR));
            break;
        }

        // Update intelligence mode label
        updateIntelligenceModeLabel(inverterData.inverterMode);

        updateFlowAnimations(inverterData, shellyResult);

        // electricity spot price block - only update visibility if no chart is expanded
        if (expandedChart == nullptr) {
            if (electricityPriceResult.updated > 0)
            {
                // show
                lv_obj_clear_flag(spotPriceContainer, LV_OBJ_FLAG_HIDDEN);
            }
            else
            {
                // hide
                lv_obj_add_flag(spotPriceContainer, LV_OBJ_FLAG_HIDDEN);
            }
        }

        updateElectricityPriceChart(electricityPriceResult, isDarkMode);
        updateCurrentPrice(electricityPriceResult, isDarkMode);

        lv_obj_set_style_bg_color(screen, isDarkMode ? black : white, 0);
        lv_obj_set_style_bg_color(LeftContainer, containerBackground, 0);
        lv_obj_set_style_bg_opa(LeftContainer, isDarkMode ? LV_OPA_80 : LV_OPA_80, 0);
        lv_obj_set_style_bg_color(pvStatsContainer, containerBackground, 0);
        lv_obj_set_style_bg_opa(pvStatsContainer, isDarkMode ? LV_OPA_80 : LV_OPA_80, 0);
        lv_obj_set_style_bg_color(batteryStatsContainer, containerBackground, 0);
        lv_obj_set_style_bg_opa(batteryStatsContainer, isDarkMode ? LV_OPA_80 : LV_OPA_80, 0);
        lv_obj_set_style_bg_color(gridStatsContainer, containerBackground, 0);
        lv_obj_set_style_bg_opa(gridStatsContainer, isDarkMode ? LV_OPA_80 : LV_OPA_80, 0);
        lv_obj_set_style_bg_color(loadStatsContainer, containerBackground, 0);
        lv_obj_set_style_bg_opa(loadStatsContainer, isDarkMode ? LV_OPA_80 : LV_OPA_80, 0);
        lv_obj_set_style_bg_color(RightBottomContainer, containerBackground, 0);
        lv_obj_set_style_bg_opa(RightBottomContainer, isDarkMode ? LV_OPA_80 : LV_OPA_80, 0);
        lv_obj_set_style_bg_color(inverterContainer, containerBackground, 0);
        lv_obj_set_style_outline_color(inverterContainer, containerBackground, 0);
        lv_obj_set_style_outline_opa(inverterContainer, LV_OPA_80, 0);
        lv_obj_set_style_line_opa(Chart1, isDarkMode ? LV_OPA_20 : LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(loadContainer, isDarkMode ? black : white, 0);
        lv_obj_set_style_bg_color(spotPriceContainer, isDarkMode ? black : white, 0);
        lv_obj_set_style_bg_opa(spotPriceContainer, isDarkMode ? LV_OPA_80 : LV_OPA_80, 0);
        
        // Intelligence plan tile dark mode
        lv_obj_set_style_bg_color(intelligencePlanTile, isDarkMode ? black : white, 0);
        lv_obj_set_style_bg_opa(intelligencePlanTile, isDarkMode ? LV_OPA_80 : LV_OPA_80, 0);
        if (intelligenceSummaryTitle != nullptr) {
            lv_obj_set_style_text_color(intelligenceSummaryTitle, isDarkMode ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x333333), 0);
        }
        // Update detail view colors
        for (int i = 0; i < VISIBLE_PLAN_ROWS; i++) {
            if (intelligenceUpcomingTimes[i] != nullptr) {
                lv_obj_set_style_text_color(intelligenceUpcomingTimes[i], isDarkMode ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x333333), 0);
            }
            if (intelligenceUpcomingReasons[i] != nullptr) {
                lv_obj_set_style_text_color(intelligenceUpcomingReasons[i], isDarkMode ? lv_color_hex(0xAAAAAA) : lv_color_hex(0x666666), 0);
            }
            // Timeline bullets and lines
            if (intelligenceUpcomingBullets[i] != nullptr) {
                lv_obj_set_style_bg_color(intelligenceUpcomingBullets[i], isDarkMode ? lv_color_hex(0xAAAAAA) : lv_color_hex(0x333333), 0);
            }
            if (intelligenceUpcomingLines[i] != nullptr) {
                lv_obj_set_style_bg_color(intelligenceUpcomingLines[i], isDarkMode ? lv_color_hex(0x666666) : lv_color_hex(0x333333), 0);
            }
        }
        // Update stats colors - values and units same color
        if (intelligenceStatsProduction != nullptr) {
            lv_obj_set_style_text_color(intelligenceStatsProduction, isDarkMode ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x333333), 0);
        }
        if (intelligenceStatsProductionUnit != nullptr) {
            lv_obj_set_style_text_color(intelligenceStatsProductionUnit, isDarkMode ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x333333), 0);
        }
        if (intelligenceStatsConsumption != nullptr) {
            lv_obj_set_style_text_color(intelligenceStatsConsumption, isDarkMode ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x333333), 0);
        }
        if (intelligenceStatsConsumptionUnit != nullptr) {
            lv_obj_set_style_text_color(intelligenceStatsConsumptionUnit, isDarkMode ? lv_color_hex(0xFFFFFF) : lv_color_hex(0x333333), 0);
        }
        // Stats container border (separator)
        if (intelligenceStatsContainer != nullptr) {
            lv_obj_set_style_border_color(intelligenceStatsContainer, isDarkMode ? lv_color_hex(0x444444) : lv_color_hex(0xE0E0E0), 0);
        }
        // Vertical separator between stats
        if (intelligenceStatsSeparator != nullptr) {
            lv_obj_set_style_bg_color(intelligenceStatsSeparator, isDarkMode ? lv_color_hex(0x444444) : lv_color_hex(0xE0E0E0), 0);
        }
        
        lv_obj_set_style_text_color(screen, isDarkMode ? white : black, 0);
        lv_obj_set_style_text_color(selfUsePercentLabel, isDarkMode ? black : white, 0);

        lv_obj_set_style_bg_color(clocksLabel, containerBackground, 0);
        lv_obj_set_style_text_color(clocksLabel, isDarkMode ? white : black, 0);
    }

    void updateBatteryIcon(int soc)
    {
        if (soc >= 95)
        {
            lv_img_set_src(batteryImage, &ui_img_battery_100_png);
        }
        else if (soc >= 75)
        {
            lv_img_set_src(batteryImage, &ui_img_battery_80_png);
        }
        else if (soc >= 55)
        {
            lv_img_set_src(batteryImage, &ui_img_battery_60_png);
        }
        else if (soc >= 35)
        {
            lv_img_set_src(batteryImage, &ui_img_battery_40_png);
        }
        else if (soc >= 15)
        {
            lv_img_set_src(batteryImage, &ui_img_battery_20_png);
        }
        else
        {
            lv_img_set_src(batteryImage, &ui_img_battery_0_png);
        }
    }

    /**
     * Nastaví stav inteligence a aktualizuje dlaždici
     * @param active Zda je inteligence aktivní a má platná data
     * @param enabled Zda je inteligence zapnutá v nastavení
     * @param hasSpotPrices Zda jsou k dispozici spotové ceny
     */
    void setIntelligenceState(bool active, bool enabled, bool hasSpotPrices)
    {
        spotChartData.hasIntelligencePlan = active;
        intelligenceEnabled = enabled;  // Store enabled state for mode label
        
        if (!enabled) {
            // Intelligence is disabled - hide tile
            lv_obj_add_flag(intelligencePlanTile, LV_OBJ_FLAG_HIDDEN);
        } else if (!hasSpotPrices) {
            // Intelligence enabled but no spot prices - show waiting state
            lv_obj_clear_flag(intelligencePlanTile, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(intelligenceSummaryBadge, TR(STR_WAITING));
            lv_obj_set_style_bg_color(intelligenceSummaryBadge, lv_color_hex(0x888888), 0);
            if (intelligenceSummarySavings != nullptr) {
                lv_label_set_text(intelligenceSummarySavings, "");
            }
        } else if (active) {
            // Intelligence is working
            lv_obj_clear_flag(intelligencePlanTile, LV_OBJ_FLAG_HIDDEN);
            // Summary will be updated by updateIntelligenceModeLabel
        } else {
            // Intelligence enabled but not active for some reason
            lv_obj_add_flag(intelligencePlanTile, LV_OBJ_FLAG_HIDDEN);
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

    SpotChartData_t spotChartData;  // Data pro graf spotových cen včetně plánu inteligence

    lv_chart_series_t *pvPowerSeries;
    lv_chart_series_t *acPowerSeries;
    lv_chart_series_t *socSeries;
    
    // External arrays for chart data - avoid per-point invalidation
    static constexpr int MAX_CHART_POINTS = 192;  // Max 2 days
    lv_coord_t pvPowerData[MAX_CHART_POINTS];
    lv_coord_t acPowerData[MAX_CHART_POINTS];
    lv_coord_t socData[MAX_CHART_POINTS];
    uint32_t lastChartUpdateHash = 0;
    
    void updateSolarChart(InverterData_t &inverterData, SolarChartDataProvider &solarChartDataProvider, bool isDarkMode)
    {
        // Zjistíme jestli máme data na zítřek
        int totalQuarters = solarChartDataProvider.getTotalQuarters();
        if (totalQuarters > MAX_CHART_POINTS) totalQuarters = MAX_CHART_POINTS;
        
        // Calculate simple hash to detect if data changed
        uint32_t hash = totalQuarters;
        int currentQuarter = solarChartDataProvider.getCurrentQuarterIndex();
        SolarChartDataItem_t currentItem = solarChartDataProvider.getQuarter(currentQuarter);
        hash ^= (uint32_t)(currentItem.pvPowerWh * 100) << 8;
        hash ^= (uint32_t)(currentItem.loadPowerWh * 100) << 16;
        hash ^= (uint32_t)currentItem.soc << 24;
        hash ^= (uint32_t)currentQuarter;
        
        // Skip update if data hasn't changed significantly
        if (hash == lastChartUpdateHash) {
            return;
        }
        lastChartUpdateHash = hash;
        
        // Nastavíme počet bodů podle dostupných dat (96 nebo 192)
        lv_chart_set_point_count(Chart1, totalQuarters);
        
        float maxPower = 5000.0f;
        
        // Fill external arrays - no LVGL calls here, just data
        for (int i = 0; i < totalQuarters; i++)
        {
            SolarChartDataItem_t item = solarChartDataProvider.getQuarter(i);
            
            // Převod Wh za čtvrthodinu na průměrný výkon W pro zobrazení
            float pvPowerW = item.pvPowerWh * 4.0f;
            float loadPowerW = item.loadPowerWh * 4.0f;
            
            if (item.samples > 0) {
                pvPowerData[i] = (lv_coord_t)pvPowerW;
                acPowerData[i] = (lv_coord_t)loadPowerW;
                if (inverterData.hasBattery && item.soc >= 0) {
                    socData[i] = item.soc;
                } else {
                    socData[i] = LV_CHART_POINT_NONE;
                }
                maxPower = max(maxPower, max(pvPowerW, loadPowerW));
            } else {
                pvPowerData[i] = LV_CHART_POINT_NONE;
                acPowerData[i] = LV_CHART_POINT_NONE;
                socData[i] = LV_CHART_POINT_NONE;
            }
        }
        
        // Reset series start points
        pvPowerSeries->start_point = 0;
        acPowerSeries->start_point = 0;
        socSeries->start_point = 0;
        
        // Use external arrays - single LVGL call per series instead of 96+ calls
        lv_chart_set_ext_y_array(Chart1, pvPowerSeries, pvPowerData);
        lv_chart_set_ext_y_array(Chart1, acPowerSeries, acPowerData);
        lv_chart_set_ext_y_array(Chart1, socSeries, socData);
        
        lv_chart_set_range(Chart1, LV_CHART_AXIS_SECONDARY_Y, 0, (lv_coord_t)maxPower);
        lv_obj_set_style_text_color(Chart1, isDarkMode ? lv_color_white() : lv_color_black(), LV_PART_TICKS);
        lv_chart_refresh(Chart1);
    }

    void updateCurrentPrice(ElectricityPriceTwoDays_t &electricityPriceResult, bool isDarkMode)
    {
        // Get current quarter price from two-day structure
        time_t now = time(nullptr);
        struct tm *timeinfo = localtime(&now);
        int currentQuarter = timeinfo->tm_hour * 4 + timeinfo->tm_min / 15;
        ElectricityPriceItem_t currentPrice = electricityPriceResult.prices[currentQuarter];
        String priceText = String(currentPrice.electricityPrice, 2) + " " + electricityPriceResult.currency + " / " + electricityPriceResult.energyUnit;
        priceText.replace(".", ",");
        lv_label_set_text(currentPriceLabel, priceText.c_str());
        lv_color_t color = getPriceLevelColor(currentPrice.priceLevel);

        lv_obj_set_style_bg_color(currentPriceLabel, color, 0);
        lv_obj_set_style_text_color(currentPriceLabel, isDarkMode ? lv_color_black() : lv_color_white(), 0);
        lv_obj_set_style_shadow_color(currentPriceLabel, color, 0);
        
        // Posun badge o jeho výšku nahoru
        lv_obj_update_layout(currentPriceLabel);
        int badgeHeight = lv_obj_get_height(currentPriceLabel);
        lv_obj_set_y(currentPriceLabel, -badgeHeight);
    }

    void updateElectricityPriceChart(ElectricityPriceTwoDays_t &electricityPriceResult, bool isDarkMode)
    {
        spotChartData.priceResult = &electricityPriceResult;
        spotChartData.hasValidPrices = (electricityPriceResult.updated > 0);
        // Intelligence plan se aktualizuje separátně přes updateIntelligencePlan()
        lv_obj_set_user_data(spotPriceContainer, (void *)&spotChartData);
        lv_obj_invalidate(spotPriceContainer);
    }

    void updateFlowAnimations(InverterData_t inverterData, ShellyResult_t shellyResult)
    {
        int duration = UI_REFRESH_PERIOD_MS / 3;
        int offsetY = 15;
        int offsetX = 30;

        if ((inverterData.pv1Power + inverterData.pv2Power + inverterData.pv3Power + inverterData.pv4Power) > 0)
        {
            pvAnimator.run(pvContainer, inverterContainer, duration, UI_BACKGROUND_ANIMATION_DURATION + 0, 0, ((inverterData.pv1Power + inverterData.pv2Power + inverterData.pv3Power + inverterData.pv4Power) / 1000) + 1, -offsetX, -offsetY);
        }
        else
        {
            pvAnimator.hide();
        }

        if (inverterData.hasBattery)
        {
            if (inverterData.batteryPower > 0)
            {
                batteryAnimator.run(inverterContainer, batteryContainer, duration, UI_BACKGROUND_ANIMATION_DURATION + duration, 1, (inverterData.batteryPower / 1000) + 1, offsetX, -offsetY);
            }
            else if (inverterData.batteryPower < 0)
            {
                batteryAnimator.run(batteryContainer, inverterContainer, duration, UI_BACKGROUND_ANIMATION_DURATION + 0, 0, (abs(inverterData.batteryPower) / 1000) + 1, offsetX, -offsetY);
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
        int gridPower = inverterData.gridPowerL1 + inverterData.gridPowerL2 + inverterData.gridPowerL3;
        if (gridPower > 0)
        {
            gridAnimator.run(inverterContainer, gridContainer, duration, UI_BACKGROUND_ANIMATION_DURATION + duration, 1, (gridPower / 1000) + 1, offsetX, offsetY);
        }
        else if (gridPower < 0)
        {
            gridAnimator.run(gridContainer, inverterContainer, duration, UI_BACKGROUND_ANIMATION_DURATION + 0, 0, (abs(gridPower) / 1000) + 1, offsetX, offsetY);
        }
        else
        {
            gridAnimator.hide();
        }

        if (inverterData.loadPower > 0)
        {
            loadAnimator.run(inverterContainer, loadContainer, duration, UI_BACKGROUND_ANIMATION_DURATION + duration, 1, (inverterData.loadPower / 1000) + 1, -offsetX, offsetY);
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

// Static callback for showing buttons on any touch
static void onAnyTouchShowButtons(lv_event_t *e) {
    DashboardUI* self = (DashboardUI*)lv_event_get_user_data(e);
    if (self) {
        self->showButtonsOnTouch();
    }
}