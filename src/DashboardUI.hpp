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

static bool isDarkMode = false;

/**
 * Struktura pro kombinovaná data grafu spotových cen + plán inteligence
 */
typedef struct SpotChartData {
    ElectricityPriceTwoDays_t* priceResult;
    InverterMode_t intelligencePlan[QUARTERS_TWO_DAYS];  // Plán pro dnešek + zítřek
    bool hasIntelligencePlan;                             // Zda máme platný plán
    bool hasTomorrowPlan;                                 // Zda máme plán na zítřek
} SpotChartData_t;

/**
 * Vrátí barvu pro režim inteligence
 */
static lv_color_t getIntelligenceModeColor(InverterMode_t mode)
{
    switch (mode)
    {
    case INVERTER_MODE_SELF_USE:
        return lv_color_hex(0x4CAF50);  // Zelená
    case INVERTER_MODE_CHARGE_FROM_GRID:
        return lv_color_hex(0x2196F3);  // Modrá
    case INVERTER_MODE_DISCHARGE_TO_GRID:
        return lv_color_hex(0xFF9800);  // Oranžová
    case INVERTER_MODE_HOLD_BATTERY:
        return lv_color_hex(0x9E9E9E);  // Šedá
    default:
        return lv_color_hex(0x000000);  // Černá pro unknown
    }
}

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
        uint32_t segmentGap = 0;
        int32_t segmentWidth = showTwoDays ? 2 : 4;  // Narrower bars for 2 days
        int32_t offset_x = w - (segmentCount * segmentWidth + (segmentCount - 1) * segmentGap);
        
        // Reserve space at top for intelligence plan indicators
        int intelligenceRowHeight = chartData->hasIntelligencePlan ? 6 : 0;
        lv_coord_t chartTop = pad_top + intelligenceRowHeight;

        // Find min/max prices across all displayed data
        float minPrice = electricityPriceResult->prices[0].electricityPrice;
        float maxPrice = electricityPriceResult->prices[0].electricityPrice;
        for (uint32_t i = 0; i < segmentCount; i++)
        {
            float price = electricityPriceResult->prices[i].electricityPrice;
            if (price < minPrice) minPrice = price;
            if (price > maxPrice) maxPrice = price;
        }
        minPrice = min(0.0f, minPrice);
        maxPrice = max(maxPrice, electricityPriceResult->scaleMaxValue);
        float priceRange = maxPrice - minPrice;
        lv_coord_t chartHeight = h - intelligenceRowHeight;

        // Current quarter (relative to today)
        time_t now = time(nullptr);
        struct tm *timeinfo = localtime(&now);
        int currentQuarter = (timeinfo->tm_hour * 60 + timeinfo->tm_min) / 15;



        // Draw vertical line for current quarter
        lv_draw_rect_dsc_t current_quarter_dsc;
        lv_draw_rect_dsc_init(&current_quarter_dsc);
        current_quarter_dsc.bg_opa = LV_OPA_50;
        current_quarter_dsc.bg_color = isDarkMode ? lv_color_hex(0x555555) : lv_color_hex(0xAAAAAA);
        lv_area_t cq_a;
        cq_a.x1 = obj->coords.x1 + offset_x + currentQuarter * (segmentWidth + segmentGap) - segmentGap / 2;
        cq_a.x2 = cq_a.x1 + segmentWidth + segmentGap - 1;
        cq_a.y1 = obj->coords.y1 + chartTop;
        cq_a.y2 = obj->coords.y2 - pad_bottom;
        lv_draw_rect(dsc->draw_ctx, &current_quarter_dsc, &cq_a);

        // Draw intelligence plan row at top (aligned in single line)
        if (chartData->hasIntelligencePlan)
        {
            int squareSize = showTwoDays ? 2 : 3;
            lv_coord_t sqY = obj->coords.y1 + pad_top + squareSize / 2 + 1;
            
            uint32_t planSegments = chartData->hasTomorrowPlan ? segmentCount : QUARTERS_OF_DAY;
            for (uint32_t i = 0; i < planSegments; i++)
            {
                if (i >= (uint32_t)currentQuarter)
                {
                    InverterMode_t mode = chartData->intelligencePlan[i];
                    if (mode != INVERTER_MODE_UNKNOWN)
                    {
                        lv_draw_rect_dsc_t sq_dsc;
                        lv_draw_rect_dsc_init(&sq_dsc);
                        sq_dsc.bg_opa = LV_OPA_COVER;
                        sq_dsc.bg_color = getIntelligenceModeColor(mode);
                        sq_dsc.radius = 0;
                        
                        lv_area_t sq_a;
                        int centerX = obj->coords.x1 + offset_x + i * (segmentWidth + segmentGap) + segmentWidth / 2;
                        sq_a.x1 = centerX - squareSize / 2;
                        sq_a.x2 = centerX + squareSize / 2;
                        sq_a.y1 = sqY - squareSize / 2;
                        sq_a.y2 = sqY + squareSize / 2;
                        
                        lv_draw_rect(dsc->draw_ctx, &sq_dsc, &sq_a);
                    }
                }
            }
        }

        // Draw price segments
        for (uint32_t i = 0; i < segmentCount; i++)
        {
            float price = electricityPriceResult->prices[i].electricityPrice;
            lv_color_t color = getPriceLevelColor(electricityPriceResult->prices[i].priceLevel);

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
            lv_coord_t barTopY = obj->coords.y1 + chartTop + (priceRange - price + minPrice) * chartHeight / priceRange;
            lv_coord_t barBottomY = obj->coords.y1 + chartTop + (priceRange + minPrice) * chartHeight / priceRange - 1;
            a.y1 = barTopY;
            a.y2 = barBottomY;
            if (a.y1 > a.y2)
            {
                lv_coord_t temp = a.y1;
                a.y1 = a.y2;
                a.y2 = temp;
            }
            lv_draw_rect(dsc->draw_ctx, &draw_rect_dsc, &a);
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
                la.x1 = obj->coords.x1 + offset_x + quarter * (segmentWidth + segmentGap) + (segmentWidth - size.x) / 2;
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
                la.x1 = obj->coords.x1 + offset_x + quarter * (segmentWidth + segmentGap) + (segmentWidth - size.x) / 2;
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
        line_dsc.bg_opa = LV_OPA_50;
        line_dsc.bg_color = isDarkMode ? lv_color_hex(0x555555) : lv_color_hex(0xAAAAAA);
        
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
        
        // Zkontrolujeme, zda je to prediction series (průhledná)
        lv_chart_series_t** predSeries = (lv_chart_series_t**)lv_obj_get_user_data(obj);
        bool isPrediction = false;
        if (predSeries) {
            for (int i = 0; i < 3; i++) {
                if (predSeries[i] == ser) {
                    isPrediction = true;
                    break;
                }
            }
        }
        
        // Nastavíme průhlednost čáry pro predikce
        if (isPrediction && dsc->line_dsc) {
            dsc->line_dsc->opa = LV_OPA_40;
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
        draw_rect_dsc.bg_opa = isPrediction ? LV_OPA_20 : LV_OPA_50;  // Nižší průhlednost pro predikce
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
    lv_obj_t *intelligenceModeLabel = nullptr;  // Label pro zobrazení režimu inteligence
    lv_obj_t *productionPredictionBadge = nullptr;  // Label pro predikci výroby
    lv_obj_t *consumptionPredictionBadge = nullptr; // Label pro predikci spotřeby
    lv_obj_t *productionPredRow = nullptr;  // Řádek pro predikci výroby
    lv_obj_t *consumptionPredRow = nullptr; // Řádek pro predikci spotřeby
    lv_obj_t *inverterModeMenu = nullptr;   // Popup menu pro výběr režimu střídače
    lv_obj_t *inverterModeOverlay = nullptr; // Overlay za popup menu
    lv_obj_t *modeChangeSpinner = nullptr;  // Spinner overlay při změně režimu
    InverterModeChangeCallback modeChangeCallback = nullptr;

public:
    const int UI_REFRESH_PERIOD_MS = 5000;
    lv_timer_t *clocksTimer = nullptr;
    lv_obj_t *intelligenceButton = nullptr;  // Intelligence settings button
    
    DashboardUI(void (*onSettingsShow)(lv_event_t *), void (*onIntelligenceShow)(lv_event_t *) = nullptr)
    {
        // Initialize spot chart data
        spotChartData.priceResult = nullptr;
        spotChartData.hasIntelligencePlan = false;
        spotChartData.hasTomorrowPlan = false;
        for (int i = 0; i < QUARTERS_TWO_DAYS; i++) {
            spotChartData.intelligencePlan[i] = INVERTER_MODE_UNKNOWN;
        }
        
        lv_obj_add_event_cb(ui_Chart1, solar_chart_draw_event_cb, LV_EVENT_DRAW_PART_BEGIN, NULL);
        lv_obj_add_event_cb(ui_spotPriceContainer, electricity_price_draw_event_cb, LV_EVENT_DRAW_PART_END, NULL);
        lv_obj_add_event_cb(ui_settingsButton, onSettingsShow, LV_EVENT_RELEASED, NULL);
        
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
            lv_label_set_text(iconLabel, LV_SYMBOL_SETTINGS);  // Using settings icon, could use custom
            lv_obj_set_style_text_font(iconLabel, &lv_font_montserrat_24, 0);
            lv_obj_set_style_text_color(iconLabel, lv_color_hex(0x00AAFF), 0);
            lv_obj_center(iconLabel);
            
            lv_obj_add_event_cb(intelligenceButton, onIntelligenceShow, LV_EVENT_RELEASED, NULL);
        }

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

        // Create production prediction row (inserted between today and total in pvStats)
        // ui_Container34 is the column containing rows, ui_Container5 is the total row
        productionPredRow = lv_obj_create(ui_Container34);
        lv_obj_remove_style_all(productionPredRow);
        lv_obj_set_width(productionPredRow, lv_pct(100));
        lv_obj_set_height(productionPredRow, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(productionPredRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(productionPredRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(productionPredRow, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_column(productionPredRow, 4, 0);
        lv_obj_move_to_index(productionPredRow, 1);  // Move after today row (index 0)
        lv_obj_add_flag(productionPredRow, LV_OBJ_FLAG_HIDDEN);  // Hidden by default

        productionPredictionBadge = lv_label_create(productionPredRow);
        lv_obj_set_height(productionPredictionBadge, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(productionPredictionBadge, 3);
        lv_label_set_text(productionPredictionBadge, "~0");
        lv_obj_set_style_text_align(productionPredictionBadge, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_font(productionPredictionBadge, &ui_font_OpenSansExtraSmall, 0);

        lv_obj_t* productionPredUnit = lv_label_create(productionPredRow);
        lv_obj_set_height(productionPredUnit, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(productionPredUnit, 1);
        lv_label_set_text(productionPredUnit, "kWh");
        lv_obj_set_style_text_align(productionPredUnit, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_font(productionPredUnit, &ui_font_OpenSansExtraSmall, 0);

        // Create consumption prediction row (inserted between today and selfUse in loadStats)
        // ui_Container37 is the column containing rows
        consumptionPredRow = lv_obj_create(ui_Container37);
        lv_obj_remove_style_all(consumptionPredRow);
        lv_obj_set_width(consumptionPredRow, lv_pct(100));
        lv_obj_set_height(consumptionPredRow, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(consumptionPredRow, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(consumptionPredRow, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(consumptionPredRow, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_column(consumptionPredRow, 4, 0);
        lv_obj_move_to_index(consumptionPredRow, 1);  // Move after today row (index 0)
        lv_obj_add_flag(consumptionPredRow, LV_OBJ_FLAG_HIDDEN);  // Hidden by default

        consumptionPredictionBadge = lv_label_create(consumptionPredRow);
        lv_obj_set_height(consumptionPredictionBadge, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(consumptionPredictionBadge, 3);
        lv_label_set_text(consumptionPredictionBadge, "~0");
        lv_obj_set_style_text_align(consumptionPredictionBadge, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_font(consumptionPredictionBadge, &ui_font_OpenSansExtraSmall, 0);

        lv_obj_t* consumptionPredUnit = lv_label_create(consumptionPredRow);
        lv_obj_set_height(consumptionPredUnit, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(consumptionPredUnit, 1);
        lv_label_set_text(consumptionPredUnit, "kWh");
        lv_obj_set_style_text_align(consumptionPredUnit, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_font(consumptionPredUnit, &ui_font_OpenSansExtraSmall, 0);

        pvAnimator.setup(ui_LeftContainer, _ui_theme_color_pvColor);
        batteryAnimator.setup(ui_LeftContainer, _ui_theme_color_batteryColor);
        gridAnimator.setup(ui_LeftContainer, _ui_theme_color_gridColor);
        loadAnimator.setup(ui_LeftContainer, _ui_theme_color_loadColor);

        // remove demo chart series from designer
        while (lv_chart_get_series_next(ui_Chart1, NULL))
        {
            lv_chart_remove_series(ui_Chart1, lv_chart_get_series_next(ui_Chart1, NULL));
        }

        // Reálná data - plné barvy
        pvPowerSeries = lv_chart_add_series(ui_Chart1, lv_color_hex(_ui_theme_color_pvColor[0]), LV_CHART_AXIS_SECONDARY_Y);
        acPowerSeries = lv_chart_add_series(ui_Chart1, lv_color_hex(_ui_theme_color_loadColor[0]), LV_CHART_AXIS_SECONDARY_Y);
        socSeries = lv_chart_add_series(ui_Chart1, lv_color_hex(_ui_theme_color_batteryColor[0]), LV_CHART_AXIS_PRIMARY_Y);
        
        // Predikce - stejné barvy (průhlednost se aplikuje v draw callbacku)
        pvPredictionSeries = lv_chart_add_series(ui_Chart1, lv_color_hex(_ui_theme_color_pvColor[0]), LV_CHART_AXIS_SECONDARY_Y);
        acPredictionSeries = lv_chart_add_series(ui_Chart1, lv_color_hex(_ui_theme_color_loadColor[0]), LV_CHART_AXIS_SECONDARY_Y);
        socPredictionSeries = lv_chart_add_series(ui_Chart1, lv_color_hex(_ui_theme_color_batteryColor[0]), LV_CHART_AXIS_PRIMARY_Y);
        
        // Uložíme prediction series do user_data chartu pro přístup z draw callbacku
        static lv_chart_series_t* predictionSeriesArray[3];
        predictionSeriesArray[0] = pvPredictionSeries;
        predictionSeriesArray[1] = acPredictionSeries;
        predictionSeriesArray[2] = socPredictionSeries;
        lv_obj_set_user_data(ui_Chart1, predictionSeriesArray);
        
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
                                        static uint8_t step = 0;
                                        step++;
                                          // update time and date labels
                                          struct tm timeinfo;
                                          if (getLocalTime(&timeinfo,0))
                                          {
                                            // show label
                                            lv_obj_clear_flag(ui_clocksLabel, LV_OBJ_FLAG_HIDDEN);
                                            char timeStr[6];
                                            lv_snprintf(timeStr, sizeof(timeStr), step % 2 == 0 ? "%02d %02d" : "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
                                            lv_label_set_text(ui_clocksLabel, timeStr);
                                          }
                                          else
                                          {
                                            // hide label
                                            lv_obj_add_flag(ui_clocksLabel, LV_OBJ_FLAG_HIDDEN);
                                            lv_label_set_text(ui_clocksLabel, "--:--");
                                          } }, 1000, NULL);

        // Make inverter container clickable and add menu (only if intelligence is supported)
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
            {"Intelligence", INVERTER_MODE_UNKNOWN, true, lv_color_hex(0x00AAFF)},
            {"Self-Use", INVERTER_MODE_SELF_USE, false, lv_color_hex(0x4CAF50)},
            {"Charge Battery", INVERTER_MODE_CHARGE_FROM_GRID, false, lv_color_hex(0x2196F3)},
            {"Discharge", INVERTER_MODE_DISCHARGE_TO_GRID, false, lv_color_hex(0xFF9800)},
            {"Hold Battery", INVERTER_MODE_HOLD_BATTERY, false, lv_color_hex(0x9E9E9E)},
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
        
        const char* text = "";
        lv_color_t bgColor;
        lv_color_t textColor = lv_color_white();
        
        switch (mode) {
            case INVERTER_MODE_SELF_USE:
                text = "SELF USE";
                bgColor = lv_color_hex(0x4CAF50);  // Green
                break;
            case INVERTER_MODE_CHARGE_FROM_GRID:
                text = "CHARGING";
                bgColor = lv_color_hex(0x2196F3);  // Blue
                break;
            case INVERTER_MODE_DISCHARGE_TO_GRID:
                text = "SELLING";
                bgColor = lv_color_hex(0xFF9800);  // Orange
                break;
            case INVERTER_MODE_HOLD_BATTERY:
                text = "HOLD";
                bgColor = lv_color_hex(0x9E9E9E);  // Gray
                break;
            default:
                lv_obj_add_flag(intelligenceModeLabel, LV_OBJ_FLAG_HIDDEN);
                return;
        }
        
        lv_label_set_text(intelligenceModeLabel, text);
        lv_obj_set_style_bg_color(intelligenceModeLabel, bgColor, 0);
        lv_obj_set_style_shadow_color(intelligenceModeLabel, bgColor, 0);
        lv_obj_set_style_shadow_opa(intelligenceModeLabel, 255, 0);
        lv_obj_set_style_text_color(intelligenceModeLabel, textColor, 0);
    }

    /**
     * Update prediction rows with remaining production and consumption for today
     * @param remainingProductionKWh Remaining production for today in kWh (0 to hide)
     * @param remainingConsumptionKWh Remaining consumption for today in kWh (0 to hide)
     */
    void updatePredictionBadges(float remainingProductionKWh, float remainingConsumptionKWh) {
        if (productionPredRow != nullptr && productionPredictionBadge != nullptr) {
            if (remainingProductionKWh > 0) {
                char buf[20];
                snprintf(buf, sizeof(buf), "~%.1f", remainingProductionKWh);
                lv_label_set_text(productionPredictionBadge, buf);
                lv_obj_clear_flag(productionPredRow, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(productionPredRow, LV_OBJ_FLAG_HIDDEN);
            }
        }
        
        if (consumptionPredRow != nullptr && consumptionPredictionBadge != nullptr) {
            if (remainingConsumptionKWh > 0) {
                char buf[20];
                snprintf(buf, sizeof(buf), "~%.1f", remainingConsumptionKWh);
                lv_label_set_text(consumptionPredictionBadge, buf);
                lv_obj_clear_flag(consumptionPredRow, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(consumptionPredRow, LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    void show()
    {
        lv_scr_load(ui_Dashboard);

        // Hide buttons initially - they will appear on touch
        lv_obj_add_flag(ui_settingsButton, LV_OBJ_FLAG_HIDDEN);
        if (intelligenceButton != nullptr) {
            lv_obj_add_flag(intelligenceButton, LV_OBJ_FLAG_HIDDEN);
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
        }
        int previousGridPower = previousInverterData.gridPowerL1 + previousInverterData.gridPowerL2 + previousInverterData.gridPowerL3;
        int gridPower = inverterData.gridPowerL1 + inverterData.gridPowerL2 + inverterData.gridPowerL3;
        int previousInverterPower = previousInverterData.L1Power + previousInverterData.L2Power + previousInverterData.L3Power;
        int inverterPower = inverterData.L1Power + inverterData.L2Power + inverterData.L3Power;

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
        int totalPhasePower = inverterData.L1Power + inverterData.L2Power + inverterData.L3Power;
        int l1PercentUsage = totalPhasePower > 0 ? (100 * inverterData.L1Power) / totalPhasePower : 0;
        int l2PercentUsage = totalPhasePower > 0 ? (100 * inverterData.L2Power) / totalPhasePower : 0;
        int l3PercentUsage = totalPhasePower > 0 ? (100 * inverterData.L3Power) / totalPhasePower : 0;
        bool hasPhases = inverterData.L2Power > 0 || inverterData.L3Power > 0;

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

        // phases
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
     * Aktualizuje plán inteligence pro zobrazení v grafu spotových cen
     * @param plan Pole režimů pro dnešek a zítřek (QUARTERS_TWO_DAYS)
     * @param hasTomorrow Zda jsou k dispozici data pro zítřek
     */
    void updateIntelligencePlan(const InverterMode_t plan[QUARTERS_TWO_DAYS], bool hasTomorrow = false)
    {
        for (int i = 0; i < QUARTERS_TWO_DAYS; i++)
        {
            spotChartData.intelligencePlan[i] = plan[i];
        }
        spotChartData.hasIntelligencePlan = true;
        spotChartData.hasTomorrowPlan = hasTomorrow;
        lv_obj_invalidate(ui_spotPriceContainer);
    }

    /**
     * Vymaže plán inteligence z grafu
     */
    void clearIntelligencePlan()
    {
        spotChartData.hasIntelligencePlan = false;
        spotChartData.hasTomorrowPlan = false;
        lv_obj_invalidate(ui_spotPriceContainer);
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
    lv_chart_series_t *pvPredictionSeries;
    lv_chart_series_t *acPowerSeries;
    lv_chart_series_t *acPredictionSeries;
    lv_chart_series_t *socSeries;
    lv_chart_series_t *socPredictionSeries;
    
    void updateSolarChart(InverterData_t &inverterData, SolarChartDataProvider &solarChartDataProvider, bool isDarkMode)
    {
        // Reset series start points
        pvPowerSeries->start_point = 0;
        acPowerSeries->start_point = 0;
        socSeries->start_point = 0;
        if (pvPredictionSeries) pvPredictionSeries->start_point = 0;
        if (acPredictionSeries) acPredictionSeries->start_point = 0;
        if (socPredictionSeries) socPredictionSeries->start_point = 0;
        
        // Zjistíme jestli máme data na zítřek
        int totalQuarters = solarChartDataProvider.getTotalQuarters();
        
        // Nastavíme počet bodů podle dostupných dat (96 nebo 192)
        lv_chart_set_point_count(ui_Chart1, totalQuarters);
        
        float maxPower = 5000.0f;
        int currentQuarter = solarChartDataProvider.getCurrentQuarterIndex();
        
        // Projdeme všechny čtvrthodiny
        for (int i = 0; i < totalQuarters; i++)
        {
            SolarChartDataItem_t item = solarChartDataProvider.getQuarter(i);
            
            // Převod Wh za čtvrthodinu na průměrný výkon W pro zobrazení
            float pvPowerW = item.pvPowerWh * 4.0f;
            float loadPowerW = item.loadPowerWh * 4.0f;
            
            if (item.samples > 0) {
                if (item.isPrediction) {
                    // Predikce - do prediction series, reálné hodnoty na LV_CHART_POINT_NONE
                    lv_chart_set_value_by_id(ui_Chart1, pvPowerSeries, i, LV_CHART_POINT_NONE);
                    lv_chart_set_value_by_id(ui_Chart1, acPowerSeries, i, LV_CHART_POINT_NONE);
                    if (pvPredictionSeries) lv_chart_set_value_by_id(ui_Chart1, pvPredictionSeries, i, (lv_coord_t)pvPowerW);
                    if (acPredictionSeries) lv_chart_set_value_by_id(ui_Chart1, acPredictionSeries, i, (lv_coord_t)loadPowerW);
                    if (inverterData.hasBattery) {
                        lv_chart_set_value_by_id(ui_Chart1, socSeries, i, LV_CHART_POINT_NONE);
                        if (socPredictionSeries) lv_chart_set_value_by_id(ui_Chart1, socPredictionSeries, i, item.soc);
                    }
                } else {
                    // Reálná data
                    lv_chart_set_value_by_id(ui_Chart1, pvPowerSeries, i, (lv_coord_t)pvPowerW);
                    lv_chart_set_value_by_id(ui_Chart1, acPowerSeries, i, (lv_coord_t)loadPowerW);
                    if (pvPredictionSeries) lv_chart_set_value_by_id(ui_Chart1, pvPredictionSeries, i, LV_CHART_POINT_NONE);
                    if (acPredictionSeries) lv_chart_set_value_by_id(ui_Chart1, acPredictionSeries, i, LV_CHART_POINT_NONE);
                    if (inverterData.hasBattery) {
                        lv_chart_set_value_by_id(ui_Chart1, socSeries, i, item.soc);
                        if (socPredictionSeries) lv_chart_set_value_by_id(ui_Chart1, socPredictionSeries, i, LV_CHART_POINT_NONE);
                    }
                }
                maxPower = max(maxPower, max(pvPowerW, loadPowerW));
            } else {
                // Prázdný slot
                lv_chart_set_value_by_id(ui_Chart1, pvPowerSeries, i, LV_CHART_POINT_NONE);
                lv_chart_set_value_by_id(ui_Chart1, acPowerSeries, i, LV_CHART_POINT_NONE);
                if (pvPredictionSeries) lv_chart_set_value_by_id(ui_Chart1, pvPredictionSeries, i, LV_CHART_POINT_NONE);
                if (acPredictionSeries) lv_chart_set_value_by_id(ui_Chart1, acPredictionSeries, i, LV_CHART_POINT_NONE);
                if (inverterData.hasBattery) {
                    lv_chart_set_value_by_id(ui_Chart1, socSeries, i, LV_CHART_POINT_NONE);
                    if (socPredictionSeries) lv_chart_set_value_by_id(ui_Chart1, socPredictionSeries, i, LV_CHART_POINT_NONE);
                }
            }
        }
        
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