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
#include "utils/IntelligenceSettings.hpp"

lv_color_t red = lv_color_hex(0xE63946);
lv_color_t orange = lv_color_hex(0xFFA726);
lv_color_t green = lv_color_hex(0x4CAF50);

// Callback typ pro změnu režimu střídače
typedef void (*InverterModeChangeCallback)(InverterMode_t mode, bool intelligenceEnabled);

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

class DashboardUI
{
private:
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

public:
    const int UI_REFRESH_PERIOD_MS = 5000;
    lv_timer_t *clocksTimer = nullptr;
    lv_obj_t *intelligenceButton = nullptr;  // Intelligence settings button
    bool timeSynced = false;  // Flag to track if time has been synchronized
    
    DashboardUI(void (*onSettingsShow)(lv_event_t *), void (*onIntelligenceShow)(lv_event_t *) = nullptr)
    {
        // Initialize spot chart data
        spotChartData.priceResult = nullptr;
        spotChartData.hasIntelligencePlan = false;
        spotChartData.hasValidPrices = false;
        
        lv_obj_add_event_cb(ui_Chart1, solar_chart_draw_event_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);
        lv_obj_add_event_cb(ui_spotPriceContainer, electricity_price_draw_event_cb, LV_EVENT_DRAW_PART_END, NULL);
        lv_obj_add_event_cb(ui_settingsButton, onSettingsShow, LV_EVENT_RELEASED, NULL);
        
        // Add click handlers for chart expand/collapse
        // For solar chart, add click on ui_Chart1 since it covers the whole container
        lv_obj_add_flag(ui_Chart1, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(ui_Chart1, onAnyTouchShowButtons, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(ui_Chart1, [](lv_event_t *e) {
            log_d("Solar chart clicked");
            DashboardUI* self = (DashboardUI*)lv_event_get_user_data(e);
            if (self) {
                log_d("Self valid, calling toggleChartExpand for solar chart");
                self->toggleChartExpand(ui_RightBottomContainer);
            }
        }, LV_EVENT_CLICKED, this);
        
        lv_obj_add_flag(ui_spotPriceContainer, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(ui_spotPriceContainer, onAnyTouchShowButtons, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(ui_spotPriceContainer, [](lv_event_t *e) {
            log_d("Spot price clicked");
            DashboardUI* self = (DashboardUI*)lv_event_get_user_data(e);
            if (self) {
                log_d("Self valid, calling toggleChartExpand for spot");
                self->toggleChartExpand(ui_spotPriceContainer);
            }
        }, LV_EVENT_CLICKED, this);
        
        // Create intelligence plan tile (thin bar, same style as other tiles)
        intelligencePlanTile = lv_obj_create(ui_RightContainer);
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
        lv_label_set_text(intelligenceSummaryTitle, "Intelligence");
        lv_obj_set_style_text_font(intelligenceSummaryTitle, &ui_font_OpenSansSmall, 0);
        lv_obj_set_style_text_color(intelligenceSummaryTitle, lv_color_hex(0x333333), 0);
        lv_obj_set_flex_grow(intelligenceSummaryTitle, 1);  // Title takes remaining space, pushes rest to right
        
        // Savings value - aligned to the right next to badge
        intelligenceSummarySavings = lv_label_create(intelligencePlanSummary);
        lv_label_set_text(intelligenceSummarySavings, "~-- CZK");
        lv_obj_set_style_text_font(intelligenceSummarySavings, &ui_font_OpenSansSmall, 0);
        lv_obj_set_style_text_color(intelligenceSummarySavings, lv_color_hex(0x00AA00), 0);
        
        // Mode badge with colored background
        intelligenceSummaryBadge = lv_label_create(intelligencePlanSummary);
        lv_label_set_text(intelligenceSummaryBadge, "Self Use");
        lv_obj_set_style_text_font(intelligenceSummaryBadge, &ui_font_OpenSansSmall, 0);
        lv_obj_set_style_text_color(intelligenceSummaryBadge, lv_color_white(), 0);
        lv_obj_set_style_bg_color(intelligenceSummaryBadge, lv_color_hex(0x00AA00), 0);
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
            log_d("Intelligence tile clicked, tile=%p, summary=%p, detail=%p", tile, summary, detail);
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
            log_d("Intelligence detail clicked, tile=%p, summary=%p, detail=%p", tile, summary, detail);
            self->toggleChartExpand(tile, summary, detail);
        }, LV_EVENT_CLICKED, intelligencePlanTile);
        
        // Move to position 1 (after TopRightContainer, before RightBottomContainer/solar chart)
        lv_obj_move_to_index(intelligencePlanTile, 1);
        lv_obj_move_to_index(intelligencePlanTile, 1);
        
        // Initially hidden - shown only when intelligence is active
        lv_obj_add_flag(intelligencePlanTile, LV_OBJ_FLAG_HIDDEN);
        
        // Adjust flex grow weights - solar chart bigger, spot smaller
        lv_obj_set_flex_grow(ui_RightBottomContainer, 2);  // Solar chart - 2x weight
        lv_obj_set_flex_grow(ui_spotPriceContainer, 1);    // Spot prices - 1x weight
        
        // Create intelligence button (next to settings button)
        if (onIntelligenceShow != nullptr) {
            intelligenceButton = lv_btn_create(ui_Dashboard);
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
            
            lv_obj_add_event_cb(intelligenceButton, onIntelligenceShow, LV_EVENT_RELEASED, NULL);
        }

        // Create floating IP address badge (bottom left corner)
        ipBadge = lv_obj_create(ui_Dashboard);
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
        intelligenceModeLabel = lv_label_create(ui_inverterContainer);
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
        lv_obj_set_style_pad_row(ui_Container34, 0, 0);  // pvStats column
        lv_obj_set_style_pad_row(ui_Container37, 0, 0);  // loadStats column

        pvAnimator.setup(ui_LeftContainer, _ui_theme_color_pvColor);
        batteryAnimator.setup(ui_LeftContainer, _ui_theme_color_batteryColor);
        gridAnimator.setup(ui_LeftContainer, _ui_theme_color_gridColor);
        loadAnimator.setup(ui_LeftContainer, _ui_theme_color_loadColor);

        // remove demo chart series from designer
        while (lv_chart_get_series_next(ui_Chart1, NULL))
        {
            lv_chart_remove_series(ui_Chart1, lv_chart_get_series_next(ui_Chart1, NULL));
        }

        // Chart series - jedna sada pro všechna data (reálná i predikce)
        pvPowerSeries = lv_chart_add_series(ui_Chart1, lv_color_hex(_ui_theme_color_pvColor[0]), LV_CHART_AXIS_SECONDARY_Y);
        acPowerSeries = lv_chart_add_series(ui_Chart1, lv_color_hex(_ui_theme_color_loadColor[0]), LV_CHART_AXIS_SECONDARY_Y);
        socSeries = lv_chart_add_series(ui_Chart1, lv_color_hex(_ui_theme_color_batteryColor[0]), LV_CHART_AXIS_PRIMARY_Y);
        
        // Nastavíme počet bodů na celý den (96 čtvrthodin)
        lv_chart_set_point_count(ui_Chart1, CHART_QUARTERS_PER_DAY);

        lv_obj_set_style_anim_time(ui_inverterPowerBar1, UI_BACKGROUND_ANIMATION_DURATION, LV_PART_ANY);
        lv_obj_set_style_anim_time(ui_inverterPowerBar2, UI_BACKGROUND_ANIMATION_DURATION, LV_PART_ANY);
        lv_obj_set_style_anim_time(ui_inverterPowerBar3, UI_BACKGROUND_ANIMATION_DURATION, LV_PART_ANY);
        lv_obj_set_style_anim_time(ui_meterPowerBarL1, UI_BACKGROUND_ANIMATION_DURATION, LV_PART_ANY);
        lv_obj_set_style_anim_time(ui_meterPowerBarL2, UI_BACKGROUND_ANIMATION_DURATION, LV_PART_ANY);
        lv_obj_set_style_anim_time(ui_meterPowerBarL3, UI_BACKGROUND_ANIMATION_DURATION, LV_PART_ANY);

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
                                            lv_obj_clear_flag(ui_clocksLabel, LV_OBJ_FLAG_HIDDEN);
                                            char timeStr[6];
                                            lv_snprintf(timeStr, sizeof(timeStr), step % 2 == 0 ? "%02d %02d" : "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
                                            lv_label_set_text(ui_clocksLabel, timeStr);
                                        } else {
                                            // Not yet synced - hide clock
                                            lv_obj_add_flag(ui_clocksLabel, LV_OBJ_FLAG_HIDDEN);
                                            lv_label_set_text(ui_clocksLabel, "--:--");
                                        }
                                      }, 1000, this);

        // Make inverter container clickable and add menu (only if intelligence is supported)
        lv_obj_add_event_cb(ui_inverterContainer, [](lv_event_t *e) {
            DashboardUI* self = (DashboardUI*)lv_event_get_user_data(e);
            self->showButtonsOnTouch();  // Show buttons on touch
        }, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(ui_inverterContainer, [](lv_event_t *e) {
            DashboardUI* self = (DashboardUI*)lv_event_get_user_data(e);
            if (self->intelligenceSupported) {
                self->showInverterModeMenu();
            }
        }, LV_EVENT_CLICKED, this);
        
        // Add touch handler to entire dashboard to show buttons on any touch
        lv_obj_add_flag(ui_Dashboard, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(ui_Dashboard, [](lv_event_t *e) {
            DashboardUI* self = (DashboardUI*)lv_event_get_user_data(e);
            self->showButtonsOnTouch();
        }, LV_EVENT_PRESSED, this);
    }
    
    /**
     * Show settings and intelligence buttons when user touches the dashboard
     */
    void showButtonsOnTouch() {
        lastTouchMillis = millis();
        
        // Show settings button
        lv_obj_clear_flag(ui_settingsButton, LV_OBJ_FLAG_HIDDEN);
        
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
            lv_label_set_text(ipBadgeLabel, "No WiFi");
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
        inverterModeMenu = lv_obj_create(ui_Dashboard);
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
        lv_label_set_text(title, "Inverter Mode");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
        lv_obj_set_width(title, lv_pct(100));
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

        // Get current intelligence state
        IntelligenceSettings_t settings = IntelligenceSettingsStorage::load();

        // Menu items
        struct MenuItem {
            const char* text;
            InverterMode_t mode;
            bool enableIntelligence;
            lv_color_t color;
        };
        
        MenuItem items[] = {
            {"INTELLIGENCE", INVERTER_MODE_UNKNOWN, true, lv_color_hex(0x2196F3)},
            {"NORMAL", INVERTER_MODE_SELF_USE, false, lv_color_hex(_ui_theme_color_loadColor[0])},
            {"CHARGE", INVERTER_MODE_CHARGE_FROM_GRID, false, lv_color_hex(_ui_theme_color_gridColor[0])},
            {"DISCHARGE", INVERTER_MODE_DISCHARGE_TO_GRID, false, lv_color_hex(_ui_theme_color_batteryColor[0])},
            {"HOLD", INVERTER_MODE_HOLD_BATTERY, false, lv_color_hex(0x888888)},
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
            lv_obj_set_style_text_color(label, lv_color_white(), 0);
            lv_obj_center(label);

            // Store data for callback
            uint32_t userData = (items[i].mode << 8) | (items[i].enableIntelligence ? 1 : 0);
            lv_obj_add_event_cb(btn, [](lv_event_t *e) {
                DashboardUI* self = (DashboardUI*)lv_event_get_user_data(e);
                lv_obj_t* btn = lv_event_get_target(e);
                uint32_t data = (uint32_t)(uintptr_t)lv_obj_get_user_data(btn);
                InverterMode_t mode = (InverterMode_t)(data >> 8);
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
        log_d("toggleChartExpand called with chart=%p, ui_spotPriceContainer=%p, ui_RightBottomContainer=%p", 
              chart, ui_spotPriceContainer, ui_RightBottomContainer);
        if (!chart) {
            log_e("Toggle chart: null chart pointer");
            return;
        }
        
        bool isCollapsing = (expandedChart == chart);
        bool isIntelligenceTile = (summary != nullptr && detail != nullptr);
        log_d("Toggle chart: isCollapsing=%d, isIntelligenceTile=%d, expandedChart=%p, chart=%p", 
              isCollapsing, isIntelligenceTile, expandedChart, chart);
        
        if (isCollapsing) {
            // If collapsing intelligence tile
            if (isIntelligenceTile) {
                log_d("Collapsing intelligence tile");
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
            uint32_t childCount = lv_obj_get_child_cnt(ui_RightContainer);
            for (uint32_t i = 0; i < childCount; i++) {
                lv_obj_t *child = lv_obj_get_child(ui_RightContainer, i);
                if (!child) continue;
                
                // Handle intelligence tile - already processed above if it's the one being collapsed
                if (child == chart && isIntelligenceTile) {
                    lv_obj_clear_flag(child, LV_OBJ_FLAG_HIDDEN);
                    continue;
                }
                
                // Handle intelligence tile when collapsing OTHER charts (spot/solar)
                if (child == intelligencePlanTile && !isIntelligenceTile) {
                    // Show intelligence tile if intelligence is supported
                    if (intelligenceSupported) {
                        animateFade(child, true, ANIM_DURATION / 2);
                        lv_obj_set_flex_grow(child, 0);
                        lv_obj_set_height(child, LV_SIZE_CONTENT);
                        log_d("Restoring intelligence tile visibility");
                    }
                    continue;
                }
                
                // Handle spot price container
                if (child == ui_spotPriceContainer) {
                    log_d("Processing spotPriceContainer, hasValidPriceData=%d", hasValidPriceData());
                    if (hasValidPriceData()) {
                        animateFade(child, true, ANIM_DURATION / 2);
                        lv_obj_set_flex_grow(child, 1);
                    }
                    continue;
                }
                
                // Handle solar chart (RightBottomContainer)
                if (child == ui_RightBottomContainer) {
                    log_d("Processing RightBottomContainer");
                    animateFade(child, true, ANIM_DURATION / 2);
                    lv_obj_set_flex_grow(child, 2);
                    continue;
                }
                
                // ui_TopRightContainer - just show with fade
                if (child == ui_TopRightContainer) {
                    animateFade(child, true, ANIM_DURATION / 2);
                    continue;
                }
                
                // Other unknown children - hide them
                lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
            }
            
            expandedChart = nullptr;
        } else {
            // Expand - fade out all siblings except clicked chart
            uint32_t childCount = lv_obj_get_child_cnt(ui_RightContainer);
            for (uint32_t i = 0; i < childCount; i++) {
                lv_obj_t *child = lv_obj_get_child(ui_RightContainer, i);
                if (child != chart) {
                    animateFade(child, false);
                }
            }
            
            // Set expanded chart to fill available space
            lv_obj_set_flex_grow(chart, 1);
            
            // If expanding intelligence tile
            if (isIntelligenceTile) {
                log_d("Expanding intelligence tile");
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
        lv_obj_invalidate(ui_RightContainer);
        lv_obj_update_layout(ui_RightContainer);
        
        // Debug: log all children heights
        uint32_t childCount = lv_obj_get_child_cnt(ui_RightContainer);
        for (uint32_t i = 0; i < childCount; i++) {
            lv_obj_t *child = lv_obj_get_child(ui_RightContainer, i);
            bool hidden = lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN);
            log_d("Child %d: height=%d, flex_grow=%d, hidden=%d", i, lv_obj_get_height(child), lv_obj_get_style_flex_grow(child, 0), hidden);
        }
        log_d("Toggle chart: layout updated, chart height=%d", lv_obj_get_height(chart));
    }

    void setModeChangeCallback(InverterModeChangeCallback callback) {
        modeChangeCallback = callback;
    }

    /**
     * Enable or disable intelligence features based on inverter support
     * This controls visibility of intelligence button, mode badge, inverter click menu
     */
    void setIntelligenceSupported(bool supported) {
        intelligenceSupported = supported;
        
        // Update clickable state for inverter container
        if (supported) {
            lv_obj_add_flag(ui_inverterContainer, LV_OBJ_FLAG_CLICKABLE);
        } else {
            lv_obj_clear_flag(ui_inverterContainer, LV_OBJ_FLAG_CLICKABLE);
            // Hide intelligence tile if inverter doesn't support intelligence
            lv_obj_add_flag(intelligencePlanTile, LV_OBJ_FLAG_HIDDEN);
        }
        
        // Intelligence button visibility will be handled in show() method based on this flag
    }

    void showModeChangeSpinner() {
        if (modeChangeSpinner != nullptr) {
            return;  // Already showing
        }
        
        // Create fullscreen overlay
        modeChangeSpinner = lv_obj_create(ui_Dashboard);
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
        return lv_obj_get_state(ui_wallboxSmartCheckbox) & LV_STATE_CHECKED;
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
    void updateIntelligenceModeLabel(InverterMode_t mode) {
        if (intelligenceModeLabel == nullptr) {
            return;
        }
        
        // Hide badge if intelligence is not supported for this inverter
        if (!intelligenceSupported || mode == INVERTER_MODE_UNKNOWN) {
            lv_obj_add_flag(intelligenceModeLabel, LV_OBJ_FLAG_HIDDEN);
            return;
        }
        
        lv_obj_clear_flag(intelligenceModeLabel, LV_OBJ_FLAG_HIDDEN);
        
        const char* labelText = "";
        lv_color_t bgColor;
        
        if (intelligenceEnabled) {
            // When intelligence is active, show "INTELLIGENCE" badge
            labelText = "INTELLIGENCE";
            bgColor = lv_color_hex(0x2196F3);  // Blue
        } else {
            // When intelligence is disabled, show actual inverter mode
            switch (mode) {
                case INVERTER_MODE_SELF_USE:
                    labelText = "NORMAL";
                    bgColor = lv_color_hex(_ui_theme_color_loadColor[0]);  // Green (load color)
                    break;
                case INVERTER_MODE_CHARGE_FROM_GRID:
                    labelText = "CHARGE";
                    bgColor = lv_color_hex(_ui_theme_color_gridColor[0]);  // Red/Pink (grid color)
                    break;
                case INVERTER_MODE_DISCHARGE_TO_GRID:
                    labelText = "DISCHARGE";
                    bgColor = lv_color_hex(_ui_theme_color_batteryColor[0]);  // Blue (battery color)
                    break;
                case INVERTER_MODE_HOLD_BATTERY:
                    labelText = "HOLD";
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
    void updateIntelligencePlanSummary(InverterMode_t currentMode, const InverterMode_t plan[], int currentQuarter, int totalQuarters, float savings) {
        if (intelligenceSummaryBadge == nullptr || intelligenceSummaryTitle == nullptr) return;
        
        // Get mode name and color
        const char* modeName = "";
        lv_color_t badgeColor = lv_color_hex(0x666666);
        switch (currentMode) {
            case INVERTER_MODE_SELF_USE: 
                modeName = "NORMAL"; 
                badgeColor = lv_color_hex(_ui_theme_color_loadColor[0]);  // Green (load color)
                break;
            case INVERTER_MODE_CHARGE_FROM_GRID: 
                modeName = "CHARGE"; 
                badgeColor = lv_color_hex(_ui_theme_color_gridColor[0]);  // Red/Pink (grid color)
                break;
            case INVERTER_MODE_DISCHARGE_TO_GRID: 
                modeName = "DISCHARGE"; 
                badgeColor = lv_color_hex(_ui_theme_color_batteryColor[0]);  // Blue (battery color)
                break;
            case INVERTER_MODE_HOLD_BATTERY: 
                modeName = "HOLD"; 
                badgeColor = lv_color_hex(0x888888);  // Gray
                break;
            default: 
                modeName = "UNKNOWN"; 
                badgeColor = lv_color_hex(0x666666);
                break;
        }
        
        // Update badge
        lv_label_set_text(intelligenceSummaryBadge, modeName);
        lv_obj_set_style_bg_color(intelligenceSummaryBadge, badgeColor, 0);
        
        // Update savings value
        if (intelligenceSummarySavings != nullptr) {
            char savingsText[32];
            if (savings >= 0) {
                snprintf(savingsText, sizeof(savingsText), "+%.0f CZK", savings);
                lv_obj_set_style_text_color(intelligenceSummarySavings, lv_color_hex(0x00AA00), 0);  // Green for positive
            } else {
                snprintf(savingsText, sizeof(savingsText), "%.0f CZK", savings);
                lv_obj_set_style_text_color(intelligenceSummarySavings, lv_color_hex(0xAA0000), 0);  // Red for negative
            }
            lv_label_set_text(intelligenceSummarySavings, savingsText);
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
    void updateIntelligenceUpcomingPlans(const InverterMode_t plan[], int currentQuarter, int totalQuarters, 
                                         const ElectricityPriceTwoDays_t* prices = nullptr, 
                                         const IntelligenceSettings_t* settings = nullptr) {
        if (intelligenceUpcomingRows[0] == nullptr) return;
        
        // Helper to get mode name
        auto getModeName = [](InverterMode_t mode) -> const char* {
            switch (mode) {
                case INVERTER_MODE_SELF_USE: return "NORMAL";
                case INVERTER_MODE_CHARGE_FROM_GRID: return "CHARGE";
                case INVERTER_MODE_DISCHARGE_TO_GRID: return "DISCHARGE";
                case INVERTER_MODE_HOLD_BATTERY: return "HOLD";
                default: return "---";
            }
        };
        
        auto getModeColor = [](InverterMode_t mode) -> lv_color_t {
            switch (mode) {
                case INVERTER_MODE_SELF_USE: return lv_color_hex(_ui_theme_color_loadColor[0]);  // Green (load color)
                case INVERTER_MODE_CHARGE_FROM_GRID: return lv_color_hex(_ui_theme_color_gridColor[0]);  // Red/Pink (grid color)
                case INVERTER_MODE_DISCHARGE_TO_GRID: return lv_color_hex(_ui_theme_color_batteryColor[0]);  // Blue (battery color)
                case INVERTER_MODE_HOLD_BATTERY: return lv_color_hex(0x888888);  // Gray
                default: return lv_color_hex(0x666666);
            }
        };
        
        // Count total mode changes first
        int totalChanges = 0;
        InverterMode_t countMode = (currentQuarter >= 0 && currentQuarter < totalQuarters) ? plan[currentQuarter] : INVERTER_MODE_UNKNOWN;
        for (int q = currentQuarter + 1; q < totalQuarters; q++) {
            if (plan[q] != countMode && plan[q] != INVERTER_MODE_UNKNOWN) {
                totalChanges++;
                countMode = plan[q];
            }
        }
        
        // Find next VISIBLE_PLAN_ROWS mode changes
        int foundChanges = 0;
        InverterMode_t lastMode = (currentQuarter >= 0 && currentQuarter < totalQuarters) ? plan[currentQuarter] : INVERTER_MODE_UNKNOWN;
        
        for (int q = currentQuarter + 1; q < totalQuarters && foundChanges < VISIBLE_PLAN_ROWS; q++) {
            if (plan[q] != lastMode && plan[q] != INVERTER_MODE_UNKNOWN) {
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
                    float buyPrice = IntelligenceSettingsStorage::calculateBuyPrice(spotPrice, *settings);
                    float sellPrice = IntelligenceSettingsStorage::calculateSellPrice(spotPrice, *settings);
                    
                    switch (plan[q]) {
                        case INVERTER_MODE_SELF_USE:
                            snprintf(reasonBuf, sizeof(reasonBuf), "battery cheaper than %.1f %s", buyPrice, prices->currency);
                            break;
                        case INVERTER_MODE_CHARGE_FROM_GRID:
                            snprintf(reasonBuf, sizeof(reasonBuf), "low price %.1f %s", buyPrice, prices->currency);
                            break;
                        case INVERTER_MODE_DISCHARGE_TO_GRID:
                            snprintf(reasonBuf, sizeof(reasonBuf), "high price %.1f %s", sellPrice, prices->currency);
                            break;
                        case INVERTER_MODE_HOLD_BATTERY:
                            snprintf(reasonBuf, sizeof(reasonBuf), "waiting for better price");
                            break;
                        default:
                            reasonBuf[0] = '\0';
                    }
                } else {
                    // Fallback without prices
                    switch (plan[q]) {
                        case INVERTER_MODE_SELF_USE: 
                            snprintf(reasonBuf, sizeof(reasonBuf), "using battery");
                            break;
                        case INVERTER_MODE_CHARGE_FROM_GRID: 
                            snprintf(reasonBuf, sizeof(reasonBuf), "low electricity price");
                            break;
                        case INVERTER_MODE_DISCHARGE_TO_GRID: 
                            snprintf(reasonBuf, sizeof(reasonBuf), "high electricity price");
                            break;
                        case INVERTER_MODE_HOLD_BATTERY: 
                            snprintf(reasonBuf, sizeof(reasonBuf), "holding for later");
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

    void show()
    {
        lv_scr_load(ui_Dashboard);

        // Hide buttons initially - they will appear on touch
        lv_obj_add_flag(ui_settingsButton, LV_OBJ_FLAG_HIDDEN);
        if (intelligenceButton != nullptr) {
            lv_obj_add_flag(intelligenceButton, LV_OBJ_FLAG_HIDDEN);
        }
        if (ipBadge != nullptr) {
            lv_obj_add_flag(ipBadge, LV_OBJ_FLAG_HIDDEN);
        }
        
        // Reset touch timer
        lastTouchMillis = 0;
        shownMillis = millis();
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
            lv_obj_add_flag(ui_settingsButton, LV_OBJ_FLAG_HIDDEN);
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
            lv_obj_clear_flag(ui_inverterPhasesContainer, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(ui_inverterPhasesContainer, LV_OBJ_FLAG_HIDDEN);
        }

        bool hasGridPhases = inverterData.gridPowerL2 != 0 || inverterData.gridPowerL3 != 0;
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

        inverterPowerTextAnimator.animate(ui_inverterPowerLabel, previousInverterPower, inverterPower);
        pvBackgroundAnimator.animate(ui_pvContainer, ((inverterData.pv1Power + inverterData.pv2Power + inverterData.pv3Power + inverterData.pv4Power) > 0) ? lv_color_hex(_ui_theme_color_pvColor[0]) : containerBackground);
        lv_label_set_text(ui_inverterPowerUnitLabel, format(POWER, inverterPower).unit.c_str());

        // phases - záporný výkon střídače zobrazujeme jako 0
        int displayL1Power = max(0, inverterData.inverterOutpuPowerL1);
        int displayL2Power = max(0, inverterData.inverterOutpuPowerL2);
        int displayL3Power = max(0, inverterData.inverterOutpuPowerL3);
        
        lv_label_set_text(ui_inverterPowerL1Label, format(POWER, displayL1Power, 1.0f, false).formatted.c_str());
        lv_bar_set_value(ui_inverterPowerBar1, min(2400, displayL1Power), LV_ANIM_ON);
        lv_obj_set_style_bg_color(ui_inverterPowerBar1, l1PercentUsage > 50 && displayL1Power > 1200 ? red : textColor, LV_PART_INDICATOR);
        lv_obj_set_style_text_color(ui_inverterPowerL1Label, l1PercentUsage > 50 && displayL1Power > 1200 ? red : textColor, 0);
        lv_label_set_text(ui_inverterPowerL2Label, format(POWER, displayL2Power, 1.0f, false).formatted.c_str());
        lv_bar_set_value(ui_inverterPowerBar2, min(2400, displayL2Power), LV_ANIM_ON);
        lv_obj_set_style_bg_color(ui_inverterPowerBar2, l2PercentUsage > 50 && displayL2Power > 1200 ? red : textColor, LV_PART_INDICATOR);
        lv_obj_set_style_text_color(ui_inverterPowerL2Label, l2PercentUsage > 50 && displayL2Power > 1200 ? red : textColor, 0);
        lv_label_set_text(ui_inverterPowerL3Label, format(POWER, displayL3Power, 1.0f, false).formatted.c_str());
        lv_bar_set_value(ui_inverterPowerBar3, min(2400, displayL3Power), LV_ANIM_ON);
        lv_obj_set_style_bg_color(ui_inverterPowerBar3, l3PercentUsage > 50 && displayL3Power > 1200 ? red : textColor, LV_PART_INDICATOR);
        lv_obj_set_style_text_color(ui_inverterPowerL3Label, l3PercentUsage > 50 && displayL3Power > 1200 ? red : textColor, 0);

        // grid phases
        lv_label_set_text(ui_meterPowerLabelL1, format(POWER, inverterData.gridPowerL1, 1.0f, false).formatted.c_str());
        lv_bar_set_value(ui_meterPowerBarL1, max((int32_t)-2400, min((int32_t)2400, inverterData.gridPowerL1)), LV_ANIM_ON);
        lv_obj_set_style_bg_color(ui_meterPowerBarL1, inverterData.gridPowerL1 < 0 ? red : textColor, LV_PART_INDICATOR);
        //lv_obj_set_style_text_color(ui_meterPowerLabelL1, inverterData.gridPowerL1 < 0 ? red : textColor, 0);
        lv_label_set_text(ui_meterPowerLabelL2, format(POWER, inverterData.gridPowerL2, 1.0f, false).formatted.c_str());
        lv_bar_set_value(ui_meterPowerBarL2, max((int32_t)-2400, min((int32_t)2400, inverterData.gridPowerL2)), LV_ANIM_ON);
        lv_obj_set_style_bg_color(ui_meterPowerBarL2, inverterData.gridPowerL2 < 0 ? red : textColor, LV_PART_INDICATOR);
        //lv_obj_set_style_text_color(ui_meterPowerLabelL2, inverterData.gridPowerL2 < 0 ? red : textColor, 0);
        lv_label_set_text(ui_meterPowerLabelL3, format(POWER, inverterData.gridPowerL3, 1.0f, false).formatted.c_str());
        lv_bar_set_value(ui_meterPowerBarL3, max((int32_t)-2400, min((int32_t)2400, inverterData.gridPowerL3)), LV_ANIM_ON);
        lv_obj_set_style_bg_color(ui_meterPowerBarL3, inverterData.gridPowerL3 < 0 ? red : textColor, LV_PART_INDICATOR);
        //lv_obj_set_style_text_color(ui_meterPowerLabelL3, inverterData.gridPowerL3 < 0 ? red : textColor, 0);

        loadPowerTextAnimator.animate(ui_loadPowerLabel, previousInverterData.loadPower, inverterData.loadPower);
        lv_label_set_text(ui_loadPowerUnitLabel, format(POWER, inverterData.loadPower).unit.c_str());
        feedInPowerTextAnimator.animate(ui_feedInPowerLabel, abs(previousGridPower), abs(gridPower));
        lv_label_set_text(ui_feedInPowerUnitLabel, format(POWER, abs(gridPower)).unit.c_str());
        gridBackgroundAnimator.animate(ui_gridContainer, (gridPower < 0) ? lv_color_hex(_ui_theme_color_gridColor[0]) : containerBackground);
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

        // Update intelligence mode label
        updateIntelligenceModeLabel(inverterData.inverterMode);

        updateFlowAnimations(inverterData, shellyResult);

        // electricity spot price block - only update visibility if no chart is expanded
        if (expandedChart == nullptr) {
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
        
        lv_obj_set_style_text_color(ui_Dashboard, isDarkMode ? white : black, 0);
        lv_obj_set_style_text_color(ui_selfUsePercentLabel, isDarkMode ? black : white, 0);

        lv_obj_set_style_bg_color(ui_clocksLabel, containerBackground, 0);
        lv_obj_set_style_text_color(ui_clocksLabel, isDarkMode ? white : black, 0);
    }

    void updateBatteryIcon(int soc)
    {
        if (soc >= 95)
        {
            lv_img_set_src(ui_batteryImage, &ui_img_battery_100_png);
        }
        else if (soc >= 75)
        {
            lv_img_set_src(ui_batteryImage, &ui_img_battery_80_png);
        }
        else if (soc >= 55)
        {
            lv_img_set_src(ui_batteryImage, &ui_img_battery_60_png);
        }
        else if (soc >= 35)
        {
            lv_img_set_src(ui_batteryImage, &ui_img_battery_40_png);
        }
        else if (soc >= 15)
        {
            lv_img_set_src(ui_batteryImage, &ui_img_battery_20_png);
        }
        else
        {
            lv_img_set_src(ui_batteryImage, &ui_img_battery_0_png);
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
            lv_label_set_text(intelligenceSummaryBadge, "Waiting");
            lv_obj_set_style_bg_color(intelligenceSummaryBadge, lv_color_hex(0x888888), 0);
            if (intelligenceSummarySavings != nullptr) {
                lv_label_set_text(intelligenceSummarySavings, "-- Kč");
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
        lv_chart_set_point_count(ui_Chart1, totalQuarters);
        
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
        lv_chart_set_ext_y_array(ui_Chart1, pvPowerSeries, pvPowerData);
        lv_chart_set_ext_y_array(ui_Chart1, acPowerSeries, acPowerData);
        lv_chart_set_ext_y_array(ui_Chart1, socSeries, socData);
        
        lv_chart_set_range(ui_Chart1, LV_CHART_AXIS_SECONDARY_Y, 0, (lv_coord_t)maxPower);
        lv_obj_set_style_text_color(ui_Chart1, isDarkMode ? lv_color_white() : lv_color_black(), LV_PART_TICKS);
        lv_chart_refresh(ui_Chart1);
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
        lv_label_set_text(ui_currentPriceLabel, priceText.c_str());
        lv_color_t color = getPriceLevelColor(currentPrice.priceLevel);

        lv_obj_set_style_bg_color(ui_currentPriceLabel, color, 0);
        lv_obj_set_style_text_color(ui_currentPriceLabel, isDarkMode ? lv_color_black() : lv_color_white(), 0);
        lv_obj_set_style_shadow_color(ui_currentPriceLabel, color, 0);
        
        // Posun badge o jeho výšku nahoru
        lv_obj_update_layout(ui_currentPriceLabel);
        int badgeHeight = lv_obj_get_height(ui_currentPriceLabel);
        lv_obj_set_y(ui_currentPriceLabel, -badgeHeight);
    }

    void updateElectricityPriceChart(ElectricityPriceTwoDays_t &electricityPriceResult, bool isDarkMode)
    {
        spotChartData.priceResult = &electricityPriceResult;
        spotChartData.hasValidPrices = (electricityPriceResult.updated > 0);
        // Intelligence plan se aktualizuje separátně přes updateIntelligencePlan()
        lv_obj_set_user_data(ui_spotPriceContainer, (void *)&spotChartData);
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
        int gridPower = inverterData.gridPowerL1 + inverterData.gridPowerL2 + inverterData.gridPowerL3;
        if (gridPower > 0)
        {
            gridAnimator.run(ui_inverterContainer, ui_gridContainer, duration, UI_BACKGROUND_ANIMATION_DURATION + duration, 1, (gridPower / 1000) + 1, offsetX, offsetY);
        }
        else if (gridPower < 0)
        {
            gridAnimator.run(ui_gridContainer, ui_inverterContainer, duration, UI_BACKGROUND_ANIMATION_DURATION + 0, 0, (abs(gridPower) / 1000) + 1, offsetX, offsetY);
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

// Static callback for showing buttons on any touch
static void onAnyTouchShowButtons(lv_event_t *e) {
    DashboardUI* self = (DashboardUI*)lv_event_get_user_data(e);
    if (self) {
        self->showButtonsOnTouch();
    }
}