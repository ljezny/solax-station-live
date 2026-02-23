#pragma once

#include <Arduino.h>
#include <RemoteLogger.hpp>
#include <lvgl.h>
#include "BaseUI.hpp"
#include "Inverters/WiFiDiscovery.hpp"
#include "Spot/ElectricityPriceLoader.hpp"
#include "utils/Localization.hpp"
#include "utils/BacklightResolver.hpp"

// Font declarations
LV_FONT_DECLARE(ui_font_OpenSansSmall);
LV_FONT_DECLARE(ui_font_OpenSansMedium);
LV_FONT_DECLARE(ui_font_OpenSansMediumBold);
LV_FONT_DECLARE(ui_font_OpenSansExtraSmall);

// External reference to backlight resolver (must be provided by app.cpp)
extern BacklightResolver backlightResolver;

// Forward declarations of event handlers
static void wifiSetupCompleteHandler(lv_event_t *e);
static void wifiRollerHandler(lv_event_t *e);
static void spotRollerHandler(lv_event_t *e);
static void timezoneRollerHandler(lv_event_t *e);
static void languageRollerHandler(lv_event_t *e);
static void displayTimeoutHandler(lv_event_t *e);
static void connectionTypeHandler(lv_event_t *e);
static void onFocusHandler(lv_event_t *e);
static void onTextChangedHandler(lv_event_t *e);

class WiFiSetupUI : public BaseUI
{
private:
    // Color scheme matching dashboard
    static constexpr uint32_t COLOR_BG_DARK_HEX = 0x1A1A2E;
    static constexpr uint32_t COLOR_CARD_BG_HEX = 0x16213E;
    static constexpr uint32_t COLOR_CARD_BORDER_HEX = 0x0F3460;
    static constexpr uint32_t COLOR_ACCENT_HEX = 0xFFA726;  // Orange
    static constexpr uint32_t COLOR_GREEN_HEX = 0x4CAF50;
    static constexpr uint32_t COLOR_BLUE_HEX = 0x3282B8;
    static constexpr uint32_t COLOR_PURPLE_HEX = 0x9B59B6;
    static constexpr uint32_t COLOR_TEXT_HEX = 0xEEEEEE;
    static constexpr uint32_t COLOR_TEXT_DIM_HEX = 0x888888;
    static constexpr uint32_t COLOR_INPUT_BG_HEX = 0x0F3460;
    static constexpr uint32_t COLOR_INPUT_BORDER_HEX = 0x3282B8;

    // UI element pointers
    lv_obj_t *container12 = nullptr;
    lv_obj_t *container16 = nullptr;
    lv_obj_t *label1 = nullptr;
    lv_obj_t *wifiDropdown = nullptr;
    lv_obj_t *wifiPassword = nullptr;
    lv_obj_t *wifiSetupCompleteButton = nullptr;
    lv_obj_t *label3 = nullptr;
    lv_obj_t *container15 = nullptr;
    lv_obj_t *label5 = nullptr;
    lv_obj_t *connectionTypeDropdown = nullptr;
    lv_obj_t *inverterIP = nullptr;
    lv_obj_t *inverterSN = nullptr;
    lv_obj_t *container21 = nullptr;
    lv_obj_t *containerGeneral = nullptr;
    lv_obj_t *label2 = nullptr;
    lv_obj_t *spotProviderDropdown = nullptr;
    lv_obj_t *timeZoneDropdown = nullptr;
    lv_obj_t *languageDropdown = nullptr;
    lv_obj_t *displayTimeoutDropdown = nullptr;
    lv_obj_t *keyboard = nullptr;

    // Helper function to style a dropdown
    void styleDropdown(lv_obj_t* dropdown) {
        lv_obj_set_style_bg_color(dropdown, lv_color_hex(COLOR_INPUT_BG_HEX), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(dropdown, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(dropdown, lv_color_hex(COLOR_INPUT_BORDER_HEX), LV_PART_MAIN);
        lv_obj_set_style_border_width(dropdown, 1, LV_PART_MAIN);
        lv_obj_set_style_border_opa(dropdown, LV_OPA_70, LV_PART_MAIN);
        lv_obj_set_style_radius(dropdown, 6, LV_PART_MAIN);
        lv_obj_set_style_text_color(dropdown, lv_color_hex(COLOR_TEXT_HEX), LV_PART_MAIN);
        lv_obj_set_style_text_font(dropdown, &ui_font_OpenSansExtraSmall, LV_PART_MAIN);
        lv_obj_set_style_pad_all(dropdown, 10, LV_PART_MAIN);
        // Disable default symbol (would show square because our font doesn't have it)
        lv_dropdown_set_symbol(dropdown, NULL);
        
        // Open dropdown to create list, style it, then close
        lv_dropdown_open(dropdown);
        lv_obj_t* list = lv_dropdown_get_list(dropdown);
        if (list) {
            lv_obj_set_style_text_font(list, &ui_font_OpenSansExtraSmall, LV_PART_MAIN);
            lv_obj_set_style_text_font(list, &ui_font_OpenSansExtraSmall, LV_PART_SELECTED);
        }
        lv_dropdown_close(dropdown);
    }

    // Helper function to style a textarea input
    void styleTextarea(lv_obj_t* input) {
        lv_obj_set_style_bg_color(input, lv_color_hex(COLOR_INPUT_BG_HEX), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(input, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(input, lv_color_hex(COLOR_INPUT_BORDER_HEX), LV_PART_MAIN);
        lv_obj_set_style_border_width(input, 1, LV_PART_MAIN);
        lv_obj_set_style_border_opa(input, LV_OPA_70, LV_PART_MAIN);
        lv_obj_set_style_radius(input, 6, LV_PART_MAIN);
        lv_obj_set_style_text_color(input, lv_color_hex(COLOR_TEXT_HEX), LV_PART_MAIN);
        lv_obj_set_style_text_font(input, &ui_font_OpenSansExtraSmall, LV_PART_MAIN);
        lv_obj_set_style_pad_all(input, 10, LV_PART_MAIN);
        // Cursor style
        lv_obj_set_style_bg_color(input, lv_color_hex(COLOR_ACCENT_HEX), LV_PART_CURSOR);
        // Placeholder style
        lv_obj_set_style_text_color(input, lv_color_hex(COLOR_TEXT_DIM_HEX), LV_PART_TEXTAREA_PLACEHOLDER);
    }

    // Helper function to create a styled card
    lv_obj_t* createCard(lv_obj_t* parent, const char* iconAndTitle, uint32_t accentColorHex) {
        lv_obj_t* card = lv_obj_create(parent);
        lv_obj_set_height(card, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(card, 1);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        
        // Card styling
        lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_CARD_BG_HEX), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, lv_color_hex(accentColorHex), LV_PART_MAIN);
        lv_obj_set_style_border_width(card, 3, LV_PART_MAIN);
        lv_obj_set_style_border_side(card, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
        lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_all(card, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_row(card, 8, LV_PART_MAIN);
        
        // Shadow
        lv_obj_set_style_shadow_width(card, 15, LV_PART_MAIN);
        lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_shadow_opa(card, LV_OPA_30, LV_PART_MAIN);
        lv_obj_set_style_shadow_ofs_y(card, 4, LV_PART_MAIN);
        
        // Title
        lv_obj_t* titleLabel = lv_label_create(card);
        lv_label_set_text(titleLabel, iconAndTitle);
        lv_obj_set_style_text_font(titleLabel, &ui_font_OpenSansSmall, 0);
        lv_obj_set_style_text_color(titleLabel, lv_color_hex(accentColorHex), 0);
        lv_obj_set_style_pad_bottom(titleLabel, 4, 0);
        
        return card;
    }

    void createUI() {
        // Main screen with dark background
        lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_pad_all(screen, 12, LV_PART_MAIN);
        
        // Header with title
        lv_obj_t* header = lv_obj_create(screen);
        lv_obj_remove_style_all(header);
        lv_obj_set_width(header, lv_pct(100));
        lv_obj_set_height(header, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_bottom(header, 8, 0);
        
        // Title
        lv_obj_t* title = lv_label_create(header);
        lv_label_set_text(title, "Setup");
        lv_obj_set_style_text_font(title, &ui_font_OpenSansMediumBold, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(COLOR_ACCENT_HEX), 0);
        
        // OK button in header
        wifiSetupCompleteButton = lv_btn_create(header);
        lv_obj_set_width(wifiSetupCompleteButton, 120);
        lv_obj_set_height(wifiSetupCompleteButton, 40);
        lv_obj_add_flag(wifiSetupCompleteButton, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_obj_clear_flag(wifiSetupCompleteButton, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(wifiSetupCompleteButton, lv_color_hex(COLOR_GREEN_HEX), LV_PART_MAIN);
        lv_obj_set_style_radius(wifiSetupCompleteButton, 8, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(wifiSetupCompleteButton, 10, LV_PART_MAIN);
        lv_obj_set_style_shadow_color(wifiSetupCompleteButton, lv_color_hex(COLOR_GREEN_HEX), LV_PART_MAIN);
        lv_obj_set_style_shadow_opa(wifiSetupCompleteButton, LV_OPA_30, LV_PART_MAIN);
        
        label3 = lv_label_create(wifiSetupCompleteButton);
        lv_obj_set_align(label3, LV_ALIGN_CENTER);
        lv_label_set_text(label3, "Connect");
        lv_obj_set_style_text_color(label3, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(label3, &ui_font_OpenSansSmall, 0);
        
        // Main container with 3 columns
        container12 = lv_obj_create(screen);
        lv_obj_remove_style_all(container12);
        lv_obj_set_width(container12, lv_pct(100));
        lv_obj_set_flex_grow(container12, 1);
        lv_obj_set_flex_flow(container12, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(container12, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_add_flag(container12, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(container12, LV_DIR_VER);
        lv_obj_set_style_pad_column(container12, 12, LV_PART_MAIN);
        
        // ===== LEFT COLUMN - WiFi (Blue accent) =====
        container16 = createCard(container12, "WiFi Network", COLOR_BLUE_HEX);
        label1 = lv_obj_get_child(container16, 0);
        
        // WiFi dropdown
        wifiDropdown = lv_dropdown_create(container16);
        lv_dropdown_set_options(wifiDropdown, "Scanning...");
        lv_obj_set_width(wifiDropdown, lv_pct(100));
        lv_obj_set_height(wifiDropdown, LV_SIZE_CONTENT);
        lv_obj_add_flag(wifiDropdown, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        styleDropdown(wifiDropdown);
        
        // Password label
        lv_obj_t* pwdLabel = lv_label_create(container16);
        lv_label_set_text(pwdLabel, "Password:");
        lv_obj_set_style_text_color(pwdLabel, lv_color_hex(COLOR_TEXT_DIM_HEX), 0);
        lv_obj_set_style_text_font(pwdLabel, &ui_font_OpenSansExtraSmall, 0);
        
        // WiFi password input
        wifiPassword = lv_textarea_create(container16);
        lv_obj_set_width(wifiPassword, lv_pct(100));
        lv_obj_set_height(wifiPassword, LV_SIZE_CONTENT);
        lv_textarea_set_max_length(wifiPassword, 32);
        lv_textarea_set_placeholder_text(wifiPassword, "WiFi password");
        lv_textarea_set_one_line(wifiPassword, true);
        lv_textarea_set_password_mode(wifiPassword, true);
        styleTextarea(wifiPassword);
        
        // ===== MIDDLE COLUMN - Inverter (Orange accent) =====
        container15 = createCard(container12, "Inverter", COLOR_ACCENT_HEX);
        label5 = lv_obj_get_child(container15, 0);
        
        // Connection type dropdown
        connectionTypeDropdown = lv_dropdown_create(container15);
        lv_dropdown_set_options(connectionTypeDropdown, "SOLAX\nGoodWe\nSofar Solar\nVictron\nDEYE\nGrowatt");
        lv_obj_set_width(connectionTypeDropdown, lv_pct(100));
        lv_obj_set_height(connectionTypeDropdown, LV_SIZE_CONTENT);
        lv_obj_add_flag(connectionTypeDropdown, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        styleDropdown(connectionTypeDropdown);
        
        // IP label
        lv_obj_t* ipLabel = lv_label_create(container15);
        lv_label_set_text(ipLabel, "IP Address:");
        lv_obj_set_style_text_color(ipLabel, lv_color_hex(COLOR_TEXT_DIM_HEX), 0);
        lv_obj_set_style_text_font(ipLabel, &ui_font_OpenSansExtraSmall, 0);
        
        // Inverter IP input
        inverterIP = lv_textarea_create(container15);
        lv_obj_set_width(inverterIP, lv_pct(100));
        lv_obj_set_height(inverterIP, LV_SIZE_CONTENT);
        lv_textarea_set_max_length(inverterIP, 16);
        lv_textarea_set_placeholder_text(inverterIP, "192.168.x.x");
        lv_textarea_set_one_line(inverterIP, true);
        styleTextarea(inverterIP);
        
        // SN label
        lv_obj_t* snLabel = lv_label_create(container15);
        lv_label_set_text(snLabel, "Serial Number:");
        lv_obj_set_style_text_color(snLabel, lv_color_hex(COLOR_TEXT_DIM_HEX), 0);
        lv_obj_set_style_text_font(snLabel, &ui_font_OpenSansExtraSmall, 0);
        
        // Inverter SN input
        inverterSN = lv_textarea_create(container15);
        lv_obj_set_width(inverterSN, lv_pct(100));
        lv_obj_set_height(inverterSN, LV_SIZE_CONTENT);
        lv_textarea_set_max_length(inverterSN, 16);
        lv_textarea_set_placeholder_text(inverterSN, "SN1234567890");
        lv_textarea_set_one_line(inverterSN, true);
        styleTextarea(inverterSN);
        
        // ===== RIGHT COLUMN - Container for two cards =====
        lv_obj_t* rightColumn = lv_obj_create(container12);
        lv_obj_remove_style_all(rightColumn);
        lv_obj_set_flex_grow(rightColumn, 1);
        lv_obj_set_height(rightColumn, lv_pct(100));
        lv_obj_set_flex_flow(rightColumn, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(rightColumn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_clear_flag(rightColumn, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_row(rightColumn, 8, LV_PART_MAIN);
        
        // ===== GENERAL CARD (Green accent) =====
        containerGeneral = lv_obj_create(rightColumn);
        lv_obj_set_width(containerGeneral, lv_pct(100));
        lv_obj_set_height(containerGeneral, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(containerGeneral, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(containerGeneral, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_clear_flag(containerGeneral, LV_OBJ_FLAG_SCROLLABLE);
        
        // Card styling for General
        lv_obj_set_style_bg_color(containerGeneral, lv_color_hex(COLOR_CARD_BG_HEX), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(containerGeneral, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(containerGeneral, lv_color_hex(COLOR_GREEN_HEX), LV_PART_MAIN);
        lv_obj_set_style_border_width(containerGeneral, 3, LV_PART_MAIN);
        lv_obj_set_style_border_side(containerGeneral, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
        lv_obj_set_style_radius(containerGeneral, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_all(containerGeneral, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_row(containerGeneral, 4, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(containerGeneral, 15, LV_PART_MAIN);
        lv_obj_set_style_shadow_color(containerGeneral, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_shadow_opa(containerGeneral, LV_OPA_30, LV_PART_MAIN);
        lv_obj_set_style_shadow_ofs_y(containerGeneral, 4, LV_PART_MAIN);
        
        // General title
        lv_obj_t* generalTitle = lv_label_create(containerGeneral);
        lv_label_set_text(generalTitle, "General");
        lv_obj_set_style_text_font(generalTitle, &ui_font_OpenSansSmall, 0);
        lv_obj_set_style_text_color(generalTitle, lv_color_hex(COLOR_GREEN_HEX), 0);
        lv_obj_set_style_pad_bottom(generalTitle, 2, 0);
        
        // Timezone label
        lv_obj_t* tzLabelGen = lv_label_create(containerGeneral);
        lv_label_set_text(tzLabelGen, "Time Zone:");
        lv_obj_set_style_text_color(tzLabelGen, lv_color_hex(COLOR_TEXT_DIM_HEX), 0);
        lv_obj_set_style_text_font(tzLabelGen, &ui_font_OpenSansExtraSmall, 0);
        
        // Timezone dropdown
        timeZoneDropdown = lv_dropdown_create(containerGeneral);
        lv_dropdown_set_options(timeZoneDropdown, "Europe/Prague");
        lv_obj_set_width(timeZoneDropdown, lv_pct(100));
        lv_obj_set_height(timeZoneDropdown, LV_SIZE_CONTENT);
        lv_obj_add_flag(timeZoneDropdown, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        styleDropdown(timeZoneDropdown);
        
        // Language label
        lv_obj_t* langLabelGen = lv_label_create(containerGeneral);
        lv_label_set_text(langLabelGen, "Language:");
        lv_obj_set_style_text_color(langLabelGen, lv_color_hex(COLOR_TEXT_DIM_HEX), 0);
        lv_obj_set_style_text_font(langLabelGen, &ui_font_OpenSansExtraSmall, 0);
        
        // Language dropdown
        languageDropdown = lv_dropdown_create(containerGeneral);
        lv_dropdown_set_options(languageDropdown, "English\nDeutsch\nČeština");
        lv_obj_set_width(languageDropdown, lv_pct(100));
        lv_obj_set_height(languageDropdown, LV_SIZE_CONTENT);
        lv_obj_add_flag(languageDropdown, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        styleDropdown(languageDropdown);
        
        // Display timeout label
        lv_obj_t* timeoutLabel = lv_label_create(containerGeneral);
        lv_label_set_text(timeoutLabel, "Display off:");
        lv_obj_set_style_text_color(timeoutLabel, lv_color_hex(COLOR_TEXT_DIM_HEX), 0);
        lv_obj_set_style_text_font(timeoutLabel, &ui_font_OpenSansExtraSmall, 0);
        
        // Display timeout dropdown
        displayTimeoutDropdown = lv_dropdown_create(containerGeneral);
        lv_dropdown_set_options(displayTimeoutDropdown, "Never\n5 min\n15 min\n30 min\n60 min");
        lv_obj_set_width(displayTimeoutDropdown, lv_pct(100));
        lv_obj_set_height(displayTimeoutDropdown, LV_SIZE_CONTENT);
        lv_obj_add_flag(displayTimeoutDropdown, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        styleDropdown(displayTimeoutDropdown);
        
        // ===== SPOT PRICE CARD (Purple accent) =====
        container21 = lv_obj_create(rightColumn);
        lv_obj_set_width(container21, lv_pct(100));
        lv_obj_set_height(container21, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(container21, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(container21, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_clear_flag(container21, LV_OBJ_FLAG_SCROLLABLE);
        
        // Card styling for Spot Price
        lv_obj_set_style_bg_color(container21, lv_color_hex(COLOR_CARD_BG_HEX), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(container21, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(container21, lv_color_hex(COLOR_PURPLE_HEX), LV_PART_MAIN);
        lv_obj_set_style_border_width(container21, 3, LV_PART_MAIN);
        lv_obj_set_style_border_side(container21, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
        lv_obj_set_style_radius(container21, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_all(container21, 10, LV_PART_MAIN);
        lv_obj_set_style_pad_row(container21, 4, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(container21, 15, LV_PART_MAIN);
        lv_obj_set_style_shadow_color(container21, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_shadow_opa(container21, LV_OPA_30, LV_PART_MAIN);
        lv_obj_set_style_shadow_ofs_y(container21, 4, LV_PART_MAIN);
        
        // Spot Price title
        label2 = lv_label_create(container21);
        lv_label_set_text(label2, "Spot Price");
        lv_obj_set_style_text_font(label2, &ui_font_OpenSansSmall, 0);
        lv_obj_set_style_text_color(label2, lv_color_hex(COLOR_PURPLE_HEX), 0);
        lv_obj_set_style_pad_bottom(label2, 2, 0);
        
        // Spot provider dropdown
        spotProviderDropdown = lv_dropdown_create(container21);
        lv_dropdown_set_options(spotProviderDropdown, "None\nOTE CZ\nOTSK");
        lv_obj_set_width(spotProviderDropdown, lv_pct(100));
        lv_obj_set_height(spotProviderDropdown, LV_SIZE_CONTENT);
        lv_obj_add_flag(spotProviderDropdown, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        styleDropdown(spotProviderDropdown);
        
        // ===== KEYBOARD =====
        keyboard = lv_keyboard_create(screen);
        lv_obj_set_height(keyboard, 200);
        lv_obj_set_width(keyboard, lv_pct(100));
        lv_keyboard_set_textarea(keyboard, NULL);
        // Style keyboard
        lv_obj_set_style_bg_color(keyboard, lv_color_hex(COLOR_CARD_BG_HEX), LV_PART_MAIN);
        lv_obj_set_style_bg_color(keyboard, lv_color_hex(COLOR_INPUT_BG_HEX), LV_PART_ITEMS);
        lv_obj_set_style_text_color(keyboard, lv_color_hex(COLOR_TEXT_HEX), LV_PART_ITEMS);
        lv_obj_set_style_border_width(keyboard, 0, LV_PART_MAIN);
        // Floating keyboard - overlay on content
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_FLOATING);
        lv_obj_set_align(keyboard, LV_ALIGN_BOTTOM_MID);
        // Hide keyboard initially
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }

public:
    WiFiDiscovery &dongleDiscovery;
    WiFiDiscoveryResult_t result;
    bool setupComplete = false;

    const ConnectionType_t connectionTypes[7] = {
        CONNECTION_TYPE_NONE,
        CONNECTION_TYPE_SOLAX,
        CONNECTION_TYPE_GOODWE,
        CONNECTION_TYPE_SOFAR,
        CONNECTION_TYPE_VICTRON,
        CONNECTION_TYPE_DEYE,
        CONNECTION_TYPE_GROWATT
    };

    WiFiSetupUI(WiFiDiscovery &dongleDiscovery) : dongleDiscovery(dongleDiscovery)
    {
    }

    void show() override
    {
        // Clean up previous state
        hide();
        
        result = WiFiDiscoveryResult_t();
        setupComplete = false;

        // Create screen
        createScreen();
        
        // Create UI elements
        createUI();
        
        // Add event callbacks
        lv_obj_add_event_cb(wifiSetupCompleteButton, wifiSetupCompleteHandler, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(wifiDropdown, wifiRollerHandler, LV_EVENT_ALL, this);
        lv_obj_add_event_cb(connectionTypeDropdown, connectionTypeHandler, LV_EVENT_ALL, this);
        lv_obj_add_event_cb(spotProviderDropdown, spotRollerHandler, LV_EVENT_ALL, this);
        lv_obj_add_event_cb(timeZoneDropdown, timezoneRollerHandler, LV_EVENT_ALL, this);
        lv_obj_add_event_cb(languageDropdown, languageRollerHandler, LV_EVENT_ALL, this);
        lv_obj_add_event_cb(displayTimeoutDropdown, displayTimeoutHandler, LV_EVENT_ALL, this);
        lv_obj_add_event_cb(wifiPassword, onFocusHandler, LV_EVENT_FOCUSED, this);
        lv_obj_add_event_cb(inverterIP, onFocusHandler, LV_EVENT_FOCUSED, this);
        lv_obj_add_event_cb(inverterSN, onFocusHandler, LV_EVENT_FOCUSED, this);
        lv_obj_add_event_cb(wifiPassword, onFocusHandler, LV_EVENT_DEFOCUSED, this);
        lv_obj_add_event_cb(inverterIP, onFocusHandler, LV_EVENT_DEFOCUSED, this);
        lv_obj_add_event_cb(inverterSN, onFocusHandler, LV_EVENT_DEFOCUSED, this);
        lv_obj_add_event_cb(wifiPassword, onTextChangedHandler, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_add_event_cb(inverterIP, onTextChangedHandler, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_add_event_cb(inverterSN, onTextChangedHandler, LV_EVENT_VALUE_CHANGED, this);

        // Populate dropdowns
        String connectionTypeOptions = "";
        for (int i = 0; i < sizeof(connectionTypes) / sizeof(ConnectionType_t); i++)
        {
            String typeName = dongleDiscovery.getDongleTypeName(connectionTypes[i]);
            if (!typeName.isEmpty())
            {
                connectionTypeOptions += typeName;
                connectionTypeOptions += "\n";
            }
        }
        lv_dropdown_set_options(connectionTypeDropdown, connectionTypeOptions.c_str());
        
        String wifiOptions = "";
        for (int i = 0; i < DONGLE_DISCOVERY_MAX_RESULTS; i++)
        {
            if (dongleDiscovery.discoveries[i].ssid.isEmpty())
            {
                break;
            }
            wifiOptions += dongleDiscovery.discoveries[i].ssid;
            wifiOptions += " (";
            wifiOptions += dongleDiscovery.discoveries[i].signalPercent;
            wifiOptions += "%)";
            wifiOptions += "\n";
        }
        lv_dropdown_set_options(wifiDropdown, wifiOptions.c_str());
        lv_dropdown_set_selected(wifiDropdown, 0);
        onWiFiRollerChanged();

        String lastConnectedSSID = dongleDiscovery.loadLastConnectedSSID();
        if (!lastConnectedSSID.isEmpty())
        {
            for (int i = 0; i < DONGLE_DISCOVERY_MAX_RESULTS; i++)
            {
                if (dongleDiscovery.discoveries[i].ssid == lastConnectedSSID)
                {
                    lv_dropdown_set_selected(wifiDropdown, i);
                    onWiFiRollerChanged();
                    break;
                }
            }
        }

        ElectricityPriceLoader priceLoader;
        String spotProviders = "";
        for (int i = BZN_NONE; i < BZN_COUNT; i++)
        {
            spotProviders += priceLoader.getProviderCaption((ElectricityPriceProvider_t)i) + "\n";
        }
        lv_dropdown_set_options(spotProviderDropdown, spotProviders.c_str());
        lv_dropdown_set_selected(spotProviderDropdown, priceLoader.getStoredElectricityPriceProvider());

        String timezones = "";
        for (int i = 0; i < TIMEZONE_COUNT; i++)
        {
            timezones += String(TIMEZONES[i].name) + "\n";
        }
        lv_dropdown_set_options(timeZoneDropdown, timezones.c_str());
        String storedTimeZone = priceLoader.getStoredTimeZone();
        for (int i = 0; i < TIMEZONE_COUNT; i++)
        {
            if (storedTimeZone.equals(TIMEZONES[i].tz))
            {
                lv_dropdown_set_selected(timeZoneDropdown, i);
                break;
            }
        }

        // Language dropdown
        lv_dropdown_set_options(languageDropdown, Localization::getLanguageOptions().c_str());
        lv_dropdown_set_selected(languageDropdown, Localization::getLanguage());

        // Display timeout dropdown
        int timeoutMinutes = backlightResolver.getDisplayOffTimeout();
        int timeoutIndex = 0;
        switch (timeoutMinutes) {
            case 5:  timeoutIndex = 1; break;
            case 15: timeoutIndex = 2; break;
            case 30: timeoutIndex = 3; break;
            case 60: timeoutIndex = 4; break;
            default: timeoutIndex = 0; break;
        }
        lv_dropdown_set_selected(displayTimeoutDropdown, timeoutIndex);

        updateLocalizedTexts();
        setCompleteButtonVisibility();
        
        // Load screen
        loadScreen();
    }

    void hide() override
    {
        // Reset all pointers
        container12 = nullptr;
        container16 = nullptr;
        label1 = nullptr;
        wifiDropdown = nullptr;
        wifiPassword = nullptr;
        wifiSetupCompleteButton = nullptr;
        label3 = nullptr;
        container15 = nullptr;
        label5 = nullptr;
        connectionTypeDropdown = nullptr;
        inverterIP = nullptr;
        inverterSN = nullptr;
        container21 = nullptr;
        containerGeneral = nullptr;
        label2 = nullptr;
        spotProviderDropdown = nullptr;
        timeZoneDropdown = nullptr;
        languageDropdown = nullptr;
        displayTimeoutDropdown = nullptr;
        keyboard = nullptr;
        
        BaseUI::hide();
    }

    void updateLocalizedTexts()
    {
        if (!initialized) return;
        
        // Header title
        lv_obj_t* header = lv_obj_get_child(screen, 0);
        if (header) {
            lv_obj_t* titleLabel = lv_obj_get_child(header, 0);
            if (titleLabel) lv_label_set_text(titleLabel, TR(STR_SETUP));
        }
        
        // Connect button label
        if (label3) lv_label_set_text(label3, TR(STR_CONNECT));
        
        // Card titles
        if (label1) lv_label_set_text(label1, TR(STR_WIFI_NETWORK));
        if (label5) lv_label_set_text(label5, TR(STR_INVERTER));
        if (label2) lv_label_set_text(label2, TR(STR_SPOT_PRICE));
        
        // WiFi card labels
        if (container16) {
            lv_obj_t* pwdLabel = lv_obj_get_child(container16, 2);
            if (pwdLabel) lv_label_set_text(pwdLabel, TR(STR_PASSWORD));
        }
        
        // Inverter card labels
        if (container15) {
            lv_obj_t* ipLabel = lv_obj_get_child(container15, 2);
            if (ipLabel) lv_label_set_text(ipLabel, TR(STR_IP_ADDRESS));
            lv_obj_t* snLabel = lv_obj_get_child(container15, 4);
            if (snLabel) lv_label_set_text(snLabel, TR(STR_SERIAL_NUMBER));
        }
        
        // General card
        if (containerGeneral) {
            lv_obj_t* generalTitle = lv_obj_get_child(containerGeneral, 0);
            if (generalTitle) lv_label_set_text(generalTitle, TR(STR_GENERAL));
            lv_obj_t* tzLabel = lv_obj_get_child(containerGeneral, 1);
            if (tzLabel) lv_label_set_text(tzLabel, TR(STR_TIMEZONE));
            lv_obj_t* langLabel = lv_obj_get_child(containerGeneral, 3);
            if (langLabel) lv_label_set_text(langLabel, TR(STR_LANGUAGE));
            lv_obj_t* timeoutLabel = lv_obj_get_child(containerGeneral, 5);
            if (timeoutLabel) lv_label_set_text(timeoutLabel, TR(STR_DISPLAY_TIMEOUT));
        }
        
        // Update display timeout dropdown options
        String timeoutOptions = String(TR(STR_NEVER)) + "\n5 min\n15 min\n30 min\n60 min";
        int currentSelection = lv_dropdown_get_selected(displayTimeoutDropdown);
        lv_dropdown_set_options(displayTimeoutDropdown, timeoutOptions.c_str());
        lv_dropdown_set_selected(displayTimeoutDropdown, currentSelection);
    }

    void setCompleteButtonVisibility()
    {
        if (!wifiSetupCompleteButton || !wifiDropdown) return;
        
        bool isVisible = false;
        int selectedIndex = lv_dropdown_get_selected(wifiDropdown);
        if (selectedIndex >= 0 && selectedIndex < DONGLE_DISCOVERY_MAX_RESULTS)
        {
            isVisible = dongleDiscovery.isValid(dongleDiscovery.discoveries[selectedIndex]);
        }
        if (isVisible)
        {
            lv_obj_clear_flag(wifiSetupCompleteButton, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(wifiSetupCompleteButton, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void onCompleteClick()
    {
        if (!wifiDropdown) return;
        
        int selectedIndex = lv_dropdown_get_selected(wifiDropdown);
        LOGD("WiFi setup OK clicked, selectedIndex=%d", selectedIndex);
        if (selectedIndex >= 0 && selectedIndex < DONGLE_DISCOVERY_MAX_RESULTS)
        {
            result = dongleDiscovery.discoveries[selectedIndex];
            LOGD("Result type=%d, ssid=%s", result.type, result.ssid.c_str());
            
            // Make sure type is set correctly from dropdown
            if (connectionTypeDropdown) {
                int connectionTypeIndex = lv_dropdown_get_selected(connectionTypeDropdown);
                if (connectionTypeIndex >= 0 && connectionTypeIndex < sizeof(connectionTypes) / sizeof(ConnectionType_t))
                {
                    result.type = connectionTypes[connectionTypeIndex];
                    LOGD("Connection type from dropdown: index=%d, type=%d", connectionTypeIndex, result.type);
                }
            }
            
            setupComplete = true;
            LOGD("Setup complete flag set to true");
        }
    }

    void onWiFiRollerChanged()
    {
        if (!wifiDropdown || !wifiPassword || !inverterIP || !inverterSN || !connectionTypeDropdown) return;
        
        int selectedIndex = lv_dropdown_get_selected(wifiDropdown);
        if (selectedIndex >= 0 && selectedIndex < DONGLE_DISCOVERY_MAX_RESULTS)
        {
            lv_textarea_set_text(wifiPassword, String(dongleDiscovery.discoveries[selectedIndex].password).c_str());
            lv_textarea_set_text(inverterIP, String(dongleDiscovery.discoveries[selectedIndex].inverterIP).c_str());
            lv_textarea_set_text(inverterSN, String(dongleDiscovery.discoveries[selectedIndex].sn).c_str());
            lv_dropdown_set_selected(connectionTypeDropdown, dongleDiscovery.discoveries[selectedIndex].type);
        }
        setCompleteButtonVisibility();
    }

    void onConnectionTypeChanged()
    {
        if (!connectionTypeDropdown || !wifiDropdown) return;
        
        int connectionTypeSelectedIndex = lv_dropdown_get_selected(connectionTypeDropdown);
        if (connectionTypeSelectedIndex >= 0 && connectionTypeSelectedIndex < sizeof(connectionTypes) / sizeof(ConnectionType_t))
        {
            int wifiSelectedIndex = lv_dropdown_get_selected(wifiDropdown);
            if (wifiSelectedIndex >= 0 && wifiSelectedIndex < DONGLE_DISCOVERY_MAX_RESULTS)
            {
                dongleDiscovery.discoveries[wifiSelectedIndex].type = (ConnectionType_t)connectionTypeSelectedIndex;
            }
        }
        setCompleteButtonVisibility();
    }

    void onLanguageChanged()
    {
        if (!languageDropdown) return;
        
        int selectedIndex = lv_dropdown_get_selected(languageDropdown);
        if (selectedIndex >= 0 && selectedIndex < LANG_COUNT)
        {
            Localization::setLanguage((Language_t)selectedIndex);
            LOGD("Language changed to: %d", selectedIndex);
        }
    }

    void onFocusChanged(lv_obj_t *obj, lv_event_code_t code)
    {
        if (!keyboard || !container12) return;
        
        if (code == LV_EVENT_FOCUSED) {
            lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
            lv_keyboard_set_textarea(keyboard, obj);
            lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
            
            if (obj == wifiPassword)
            {
                lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
            }
            else if (obj == inverterIP)
            {
                lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_NUMBER);
            }
            else if (obj == inverterSN)
            {
                lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
            }
            
            // Auto-scroll
            lv_coord_t kbHeight = 220;
            lv_area_t inputArea;
            lv_obj_get_coords(obj, &inputArea);
            lv_coord_t scrHeight = lv_obj_get_height(screen);
            lv_coord_t visibleBottom = scrHeight - kbHeight;
            
            if (inputArea.y2 > visibleBottom) {
                lv_coord_t scrollY = inputArea.y2 - visibleBottom + 30;
                lv_obj_scroll_by(container12, 0, -scrollY, LV_ANIM_ON);
            }
        } else if (code == LV_EVENT_DEFOCUSED) {
            lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
            lv_obj_scroll_to_y(container12, 0, LV_ANIM_ON);
        }
        
        setCompleteButtonVisibility();
    }

    void onTextChanged(lv_obj_t *obj)
    {
        if (!wifiDropdown) return;
        
        int selectedIndex = lv_dropdown_get_selected(wifiDropdown);
        if (selectedIndex < 0 || selectedIndex >= DONGLE_DISCOVERY_MAX_RESULTS) return;
        
        if (obj == wifiPassword)
        {
            dongleDiscovery.discoveries[selectedIndex].password = lv_textarea_get_text(wifiPassword);
        }
        else if (obj == inverterIP)
        {
            dongleDiscovery.discoveries[selectedIndex].inverterIP = lv_textarea_get_text(inverterIP);
        }
        else if (obj == inverterSN)
        {
            LOGD("Inverter SN changed: %s", lv_textarea_get_text(inverterSN));
            dongleDiscovery.discoveries[selectedIndex].sn = lv_textarea_get_text(inverterSN);
        }
        setCompleteButtonVisibility();
    }
    
    // Getters for event handlers to access UI elements
    lv_obj_t* getSpotProviderDropdown() { return spotProviderDropdown; }
    lv_obj_t* getTimeZoneDropdown() { return timeZoneDropdown; }
    lv_obj_t* getDisplayTimeoutDropdown() { return displayTimeoutDropdown; }
};

// Event handlers (static functions with access to instance via user_data)
static void wifiSetupCompleteHandler(lv_event_t *e)
{
    WiFiSetupUI *ui = (WiFiSetupUI *)lv_event_get_user_data(e);
    if (ui) ui->onCompleteClick();
}

static void wifiRollerHandler(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED)
    {
        WiFiSetupUI *ui = (WiFiSetupUI *)lv_event_get_user_data(e);
        if (ui) ui->onWiFiRollerChanged();
    }
}

static void spotRollerHandler(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED)
    {
        WiFiSetupUI *ui = (WiFiSetupUI *)lv_event_get_user_data(e);
        if (ui && ui->getSpotProviderDropdown()) {
            int selectedIndex = lv_dropdown_get_selected(ui->getSpotProviderDropdown());
            LOGD("Spot provider changed to index: %d", selectedIndex);
            ElectricityPriceLoader priceLoader;
            priceLoader.storeElectricityPriceProvider((ElectricityPriceProvider_t)selectedIndex);
        }
    }
}

static void timezoneRollerHandler(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED)
    {
        WiFiSetupUI *ui = (WiFiSetupUI *)lv_event_get_user_data(e);
        if (ui && ui->getTimeZoneDropdown()) {
            int selectedIndex = lv_dropdown_get_selected(ui->getTimeZoneDropdown());
            if (selectedIndex >= 0 && selectedIndex < TIMEZONE_COUNT)
            {
                LOGD("Timezone changed to index: %d", selectedIndex);
                ElectricityPriceLoader priceLoader;
                priceLoader.storeTimeZone(String(TIMEZONES[selectedIndex].tz));
            }
        }
    }
}

static void connectionTypeHandler(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED)
    {
        WiFiSetupUI *ui = (WiFiSetupUI *)lv_event_get_user_data(e);
        if (ui) ui->onConnectionTypeChanged();
    }
}

static void languageRollerHandler(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED)
    {
        WiFiSetupUI *ui = (WiFiSetupUI *)lv_event_get_user_data(e);
        if (ui) ui->onLanguageChanged();
    }
}

static void displayTimeoutHandler(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED)
    {
        WiFiSetupUI *ui = (WiFiSetupUI *)lv_event_get_user_data(e);
        if (ui && ui->getDisplayTimeoutDropdown()) {
            int selectedIndex = lv_dropdown_get_selected(ui->getDisplayTimeoutDropdown());
            int timeoutMinutes = 0;
            switch (selectedIndex) {
                case 1: timeoutMinutes = 5;  break;
                case 2: timeoutMinutes = 15; break;
                case 3: timeoutMinutes = 30; break;
                case 4: timeoutMinutes = 60; break;
                default: timeoutMinutes = 0; break;
            }
            log_d("Display timeout changed to: %d minutes", timeoutMinutes);
            backlightResolver.setDisplayOffTimeout(timeoutMinutes);
        }
    }
}

static void onFocusHandler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED || code == LV_EVENT_DEFOCUSED)
    {
        lv_obj_t *obj = lv_event_get_target(e);
        WiFiSetupUI *ui = (WiFiSetupUI *)lv_event_get_user_data(e);
        if (ui) ui->onFocusChanged(obj, code);
    }
}

static void onTextChangedHandler(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED)
    {
        lv_obj_t *obj = lv_event_get_target(e);
        WiFiSetupUI *ui = (WiFiSetupUI *)lv_event_get_user_data(e);
        if (ui) ui->onTextChanged(obj);
    }
}
