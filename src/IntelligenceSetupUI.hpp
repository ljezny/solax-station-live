#pragma once

#include <Arduino.h>
#include <RemoteLogger.hpp>
#include <lvgl.h>
#include "BaseUI.hpp"
#include <SolarIntelligence.h>
#include "utils/Localization.hpp"

// Font declarations
LV_FONT_DECLARE(ui_font_OpenSansSmall);
LV_FONT_DECLARE(ui_font_OpenSansMedium);
LV_FONT_DECLARE(ui_font_OpenSansMediumBold);
LV_FONT_DECLARE(ui_font_OpenSansExtraSmall);

// Forward declarations of event handlers
static void intelligenceSaveHandler(lv_event_t *e);
static void intelligenceCancelHandler(lv_event_t *e);
static void intelligenceResetHandler(lv_event_t *e);
static void intelligenceInputFocusHandler(lv_event_t *e);

class IntelligenceSetupUI : public BaseUI {
private:
    // Color scheme
    static constexpr uint32_t COLOR_BG_DARK_HEX = 0x1A1A2E;
    static constexpr uint32_t COLOR_CARD_BG_HEX = 0x16213E;
    static constexpr uint32_t COLOR_CARD_BORDER_HEX = 0x0F3460;
    static constexpr uint32_t COLOR_ACCENT_HEX = 0xFFA726;
    static constexpr uint32_t COLOR_GREEN_HEX = 0x4CAF50;
    static constexpr uint32_t COLOR_RED_HEX = 0xE63946;
    static constexpr uint32_t COLOR_TEXT_HEX = 0xEEEEEE;
    static constexpr uint32_t COLOR_TEXT_DIM_HEX = 0x888888;
    static constexpr uint32_t COLOR_INPUT_BG_HEX = 0x0F3460;
    static constexpr uint32_t COLOR_INPUT_BORDER_HEX = 0x3282B8;

    // UI element pointers
    lv_obj_t *mainContainer = nullptr;
    lv_obj_t *leftColumn = nullptr;
    lv_obj_t *middleColumn = nullptr;
    lv_obj_t *rightColumn = nullptr;
    lv_obj_t *enableSwitch = nullptr;
    lv_obj_t *enableLabel = nullptr;
    lv_obj_t *betaWarning = nullptr;
    
    // Left column inputs
    lv_obj_t *batteryCostLabel = nullptr;
    lv_obj_t *batteryCostInput = nullptr;
    lv_obj_t *minSocLabel = nullptr;
    lv_obj_t *minSocInput = nullptr;
    lv_obj_t *maxSocLabel = nullptr;
    lv_obj_t *maxSocInput = nullptr;
    lv_obj_t *batteryCapLabel = nullptr;
    lv_obj_t *batteryCapInput = nullptr;
    lv_obj_t *maxChargeLabel = nullptr;
    lv_obj_t *maxChargeInput = nullptr;
    lv_obj_t *maxDischargeLabel = nullptr;
    lv_obj_t *maxDischargeInput = nullptr;
    
    // Middle column
    lv_obj_t *buyKLabel = nullptr;
    lv_obj_t *buyKInput = nullptr;
    lv_obj_t *buyQLabel = nullptr;
    lv_obj_t *buyQInput = nullptr;
    lv_obj_t *buyFormulaLabel = nullptr;
    lv_obj_t *buyInfoLabel = nullptr;
    
    // Right column
    lv_obj_t *sellKLabel = nullptr;
    lv_obj_t *sellKInput = nullptr;
    lv_obj_t *sellQLabel = nullptr;
    lv_obj_t *sellQInput = nullptr;
    lv_obj_t *sellFormulaLabel = nullptr;
    lv_obj_t *sellInfoLabel = nullptr;
    
    // Buttons
    lv_obj_t *buttonContainer = nullptr;
    lv_obj_t *resetButton = nullptr;
    lv_obj_t *resetLabel = nullptr;
    lv_obj_t *saveButton = nullptr;
    lv_obj_t *saveLabel = nullptr;
    lv_obj_t *cancelButton = nullptr;
    lv_obj_t *cancelLabel = nullptr;
    
    // Keyboard
    lv_obj_t *keyboard = nullptr;

    // Helper: style input
    void styleInput(lv_obj_t* input) {
        lv_obj_set_style_bg_color(input, lv_color_hex(COLOR_INPUT_BG_HEX), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(input, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(input, lv_color_hex(COLOR_INPUT_BORDER_HEX), LV_PART_MAIN);
        lv_obj_set_style_border_width(input, 1, LV_PART_MAIN);
        lv_obj_set_style_border_opa(input, LV_OPA_70, LV_PART_MAIN);
        lv_obj_set_style_radius(input, 6, LV_PART_MAIN);
        lv_obj_set_style_text_color(input, lv_color_hex(COLOR_TEXT_HEX), LV_PART_MAIN);
        lv_obj_set_style_text_font(input, &ui_font_OpenSansExtraSmall, LV_PART_MAIN);
        lv_obj_set_style_pad_all(input, 8, LV_PART_MAIN);
        lv_obj_set_style_bg_color(input, lv_color_hex(COLOR_ACCENT_HEX), LV_PART_CURSOR);
    }

    // Helper: create input row
    lv_obj_t* createInputRow(lv_obj_t* parent, const char* labelText, lv_obj_t** labelOut, lv_obj_t** inputOut, const char* placeholder) {
        lv_obj_t* container = lv_obj_create(parent);
        lv_obj_remove_style_all(container);
        lv_obj_set_width(container, lv_pct(100));
        lv_obj_set_height(container, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
        
        *labelOut = lv_label_create(container);
        lv_obj_set_width(*labelOut, lv_pct(55));
        lv_label_set_text(*labelOut, labelText);
        lv_obj_set_style_text_color(*labelOut, lv_color_hex(COLOR_TEXT_HEX), 0);
        lv_obj_set_style_text_font(*labelOut, &ui_font_OpenSansExtraSmall, 0);
        
        *inputOut = lv_textarea_create(container);
        lv_obj_set_width(*inputOut, lv_pct(42));
        lv_obj_set_height(*inputOut, 36);
        lv_textarea_set_max_length(*inputOut, 10);
        lv_textarea_set_placeholder_text(*inputOut, placeholder);
        lv_textarea_set_one_line(*inputOut, true);
        lv_obj_add_flag(*inputOut, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        styleInput(*inputOut);
        
        return container;
    }

    // Helper: create card
    lv_obj_t* createCard(lv_obj_t* parent, const char* title, uint32_t accentColorHex) {
        lv_obj_t* card = lv_obj_create(parent);
        lv_obj_set_height(card, LV_SIZE_CONTENT);
        lv_obj_set_flex_grow(card, 1);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        
        lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_CARD_BG_HEX), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(card, lv_color_hex(accentColorHex), LV_PART_MAIN);
        lv_obj_set_style_border_width(card, 3, LV_PART_MAIN);
        lv_obj_set_style_border_side(card, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
        lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_all(card, 12, LV_PART_MAIN);
        lv_obj_set_style_pad_row(card, 10, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(card, 15, LV_PART_MAIN);
        lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_shadow_opa(card, LV_OPA_30, LV_PART_MAIN);
        lv_obj_set_style_shadow_ofs_y(card, 4, LV_PART_MAIN);
        
        lv_obj_t* titleLabel = lv_label_create(card);
        lv_label_set_text(titleLabel, title);
        lv_obj_set_style_text_font(titleLabel, &ui_font_OpenSansSmall, 0);
        lv_obj_set_style_text_color(titleLabel, lv_color_hex(accentColorHex), 0);
        lv_obj_set_style_pad_bottom(titleLabel, 4, 0);
        
        return card;
    }

    void createUI() {
        // Main screen
        lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_pad_all(screen, 12, LV_PART_MAIN);
        
        // Header
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
        lv_label_set_text(title, "Intelligence Settings");
        lv_obj_set_style_text_font(title, &ui_font_OpenSansMediumBold, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(COLOR_ACCENT_HEX), 0);
        
        // Enable switch container
        lv_obj_t* enableContainer = lv_obj_create(header);
        lv_obj_remove_style_all(enableContainer);
        lv_obj_set_size(enableContainer, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(enableContainer, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(enableContainer, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(enableContainer, 10, 0);
        lv_obj_clear_flag(enableContainer, LV_OBJ_FLAG_SCROLLABLE);
        
        enableLabel = lv_label_create(enableContainer);
        lv_label_set_text(enableLabel, "Enable");
        lv_obj_set_style_text_color(enableLabel, lv_color_hex(COLOR_TEXT_HEX), 0);
        lv_obj_set_style_text_font(enableLabel, &ui_font_OpenSansSmall, 0);
        
        enableSwitch = lv_switch_create(enableContainer);
        lv_obj_set_width(enableSwitch, 50);
        lv_obj_set_style_bg_color(enableSwitch, lv_color_hex(COLOR_INPUT_BG_HEX), LV_PART_MAIN);
        lv_obj_set_style_bg_color(enableSwitch, lv_color_hex(COLOR_GREEN_HEX), LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(enableSwitch, lv_color_hex(COLOR_GREEN_HEX), LV_PART_KNOB | LV_STATE_CHECKED);
        
        // Beta warning box
        betaWarning = lv_obj_create(screen);
        lv_obj_set_width(betaWarning, lv_pct(100));
        lv_obj_set_height(betaWarning, LV_SIZE_CONTENT);
        lv_obj_clear_flag(betaWarning, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(betaWarning, lv_color_hex(0x4A3000), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(betaWarning, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(betaWarning, lv_color_hex(COLOR_ACCENT_HEX), LV_PART_MAIN);
        lv_obj_set_style_border_width(betaWarning, 2, LV_PART_MAIN);
        lv_obj_set_style_border_opa(betaWarning, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(betaWarning, 6, LV_PART_MAIN);
        lv_obj_set_style_pad_all(betaWarning, 8, LV_PART_MAIN);
        
        lv_obj_t* warningLabel = lv_label_create(betaWarning);
        lv_label_set_text(warningLabel, "BETA: This feature is experimental and may affect your inverter settings. Use at your own risk.");
        lv_obj_set_width(warningLabel, lv_pct(100));
        lv_label_set_long_mode(warningLabel, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_color(warningLabel, lv_color_hex(COLOR_ACCENT_HEX), 0);
        lv_obj_set_style_text_font(warningLabel, &ui_font_OpenSansExtraSmall, 0);
        
        // Main container with 3 columns
        mainContainer = lv_obj_create(screen);
        lv_obj_remove_style_all(mainContainer);
        lv_obj_set_width(mainContainer, lv_pct(100));
        lv_obj_set_flex_grow(mainContainer, 1);
        lv_obj_set_flex_flow(mainContainer, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(mainContainer, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_add_flag(mainContainer, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(mainContainer, LV_DIR_VER);
        lv_obj_set_style_pad_column(mainContainer, 12, LV_PART_MAIN);
        
        // Left column - Battery
        leftColumn = createCard(mainContainer, "Battery", COLOR_GREEN_HEX);
        createInputRow(leftColumn, "Cost per kWh:", &batteryCostLabel, &batteryCostInput, "2.0");
        createInputRow(leftColumn, "Min SOC %:", &minSocLabel, &minSocInput, "10");
        createInputRow(leftColumn, "Max SOC %:", &maxSocLabel, &maxSocInput, "95");
        createInputRow(leftColumn, "Capacity kWh:", &batteryCapLabel, &batteryCapInput, "10");
        createInputRow(leftColumn, "Max charge kW:", &maxChargeLabel, &maxChargeInput, "5");
        createInputRow(leftColumn, "Max disch. kW:", &maxDischargeLabel, &maxDischargeInput, "5");
        
        // Middle column - Buy Price
        middleColumn = createCard(mainContainer, "Buy Price", COLOR_ACCENT_HEX);
        buyFormulaLabel = lv_label_create(middleColumn);
        lv_label_set_text(buyFormulaLabel, "price = K * spot + Q");
        lv_obj_set_style_text_color(buyFormulaLabel, lv_color_hex(COLOR_TEXT_DIM_HEX), 0);
        lv_obj_set_style_text_font(buyFormulaLabel, &ui_font_OpenSansExtraSmall, 0);
        createInputRow(middleColumn, "K (multiplier):", &buyKLabel, &buyKInput, "1.21");
        createInputRow(middleColumn, "Q (fixed):", &buyQLabel, &buyQInput, "2.5");
        buyInfoLabel = lv_label_create(middleColumn);
        lv_label_set_text(buyInfoLabel, "K=1.21 for 21% VAT\nQ = distribution fees");
        lv_obj_set_style_text_color(buyInfoLabel, lv_color_hex(COLOR_TEXT_DIM_HEX), 0);
        lv_obj_set_style_text_font(buyInfoLabel, &ui_font_OpenSansExtraSmall, 0);
        
        // Right column - Sell Price
        rightColumn = createCard(mainContainer, "Sell Price", COLOR_RED_HEX);
        sellFormulaLabel = lv_label_create(rightColumn);
        lv_label_set_text(sellFormulaLabel, "price = K * spot + Q");
        lv_obj_set_style_text_color(sellFormulaLabel, lv_color_hex(COLOR_TEXT_DIM_HEX), 0);
        lv_obj_set_style_text_font(sellFormulaLabel, &ui_font_OpenSansExtraSmall, 0);
        createInputRow(rightColumn, "K (multiplier):", &sellKLabel, &sellKInput, "0.9");
        createInputRow(rightColumn, "Q (fixed):", &sellQLabel, &sellQInput, "0.0");
        sellInfoLabel = lv_label_create(rightColumn);
        lv_label_set_text(sellInfoLabel, "Usually K<1 (lower buyback)\nQ can be negative");
        lv_obj_set_style_text_color(sellInfoLabel, lv_color_hex(COLOR_TEXT_DIM_HEX), 0);
        lv_obj_set_style_text_font(sellInfoLabel, &ui_font_OpenSansExtraSmall, 0);
        
        // Button container
        buttonContainer = lv_obj_create(screen);
        lv_obj_remove_style_all(buttonContainer);
        lv_obj_set_width(buttonContainer, lv_pct(100));
        lv_obj_set_height(buttonContainer, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(buttonContainer, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(buttonContainer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_clear_flag(buttonContainer, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_column(buttonContainer, 20, LV_PART_MAIN);
        lv_obj_set_style_pad_top(buttonContainer, 10, LV_PART_MAIN);
        
        // Reset button
        resetButton = lv_btn_create(buttonContainer);
        lv_obj_set_width(resetButton, 140);
        lv_obj_set_height(resetButton, 45);
        lv_obj_set_style_bg_color(resetButton, lv_color_hex(COLOR_CARD_BG_HEX), LV_PART_MAIN);
        lv_obj_set_style_border_color(resetButton, lv_color_hex(COLOR_ACCENT_HEX), LV_PART_MAIN);
        lv_obj_set_style_border_width(resetButton, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(resetButton, 8, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(resetButton, 0, LV_PART_MAIN);
        
        resetLabel = lv_label_create(resetButton);
        lv_obj_set_align(resetLabel, LV_ALIGN_CENTER);
        lv_label_set_text(resetLabel, "Reset");
        lv_obj_set_style_text_color(resetLabel, lv_color_hex(COLOR_ACCENT_HEX), 0);
        lv_obj_set_style_text_font(resetLabel, &ui_font_OpenSansSmall, 0);
        
        // Cancel button
        cancelButton = lv_btn_create(buttonContainer);
        lv_obj_set_width(cancelButton, 140);
        lv_obj_set_height(cancelButton, 45);
        lv_obj_set_style_bg_color(cancelButton, lv_color_hex(COLOR_CARD_BG_HEX), LV_PART_MAIN);
        lv_obj_set_style_border_color(cancelButton, lv_color_hex(COLOR_TEXT_DIM_HEX), LV_PART_MAIN);
        lv_obj_set_style_border_width(cancelButton, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(cancelButton, 8, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(cancelButton, 0, LV_PART_MAIN);
        
        cancelLabel = lv_label_create(cancelButton);
        lv_obj_set_align(cancelLabel, LV_ALIGN_CENTER);
        lv_label_set_text(cancelLabel, "Cancel");
        lv_obj_set_style_text_color(cancelLabel, lv_color_hex(COLOR_TEXT_HEX), 0);
        lv_obj_set_style_text_font(cancelLabel, &ui_font_OpenSansSmall, 0);
        
        // Save button
        saveButton = lv_btn_create(buttonContainer);
        lv_obj_set_width(saveButton, 140);
        lv_obj_set_height(saveButton, 45);
        lv_obj_set_style_bg_color(saveButton, lv_color_hex(COLOR_GREEN_HEX), LV_PART_MAIN);
        lv_obj_set_style_radius(saveButton, 8, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(saveButton, 10, LV_PART_MAIN);
        lv_obj_set_style_shadow_color(saveButton, lv_color_hex(COLOR_GREEN_HEX), LV_PART_MAIN);
        lv_obj_set_style_shadow_opa(saveButton, LV_OPA_30, LV_PART_MAIN);
        
        saveLabel = lv_label_create(saveButton);
        lv_obj_set_align(saveLabel, LV_ALIGN_CENTER);
        lv_label_set_text(saveLabel, "Save");
        lv_obj_set_style_text_color(saveLabel, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(saveLabel, &ui_font_OpenSansSmall, 0);
        
        // Keyboard
        keyboard = lv_keyboard_create(screen);
        lv_obj_set_height(keyboard, 200);
        lv_obj_set_width(keyboard, lv_pct(100));
        lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_NUMBER);
        lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_FLOATING);
        lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_bg_color(keyboard, lv_color_hex(COLOR_CARD_BG_HEX), LV_PART_MAIN);
        lv_obj_set_style_bg_color(keyboard, lv_color_hex(COLOR_INPUT_BG_HEX), LV_PART_ITEMS);
        lv_obj_set_style_text_color(keyboard, lv_color_hex(COLOR_TEXT_HEX), LV_PART_ITEMS);
    }

public:
    bool resultSaved = false;
    bool resultCancelled = false;
    bool requestClearPredictions = false;

    IntelligenceSetupUI() {}

    void show() override {
        hide();
        
        resultSaved = false;
        resultCancelled = false;
        
        createScreen();
        createUI();
        
        // Add event handlers
        lv_obj_add_event_cb(saveButton, intelligenceSaveHandler, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(cancelButton, intelligenceCancelHandler, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(resetButton, intelligenceResetHandler, LV_EVENT_CLICKED, this);
        
        // Focus handlers for all inputs
        lv_obj_t* inputs[] = {batteryCostInput, minSocInput, maxSocInput, buyKInput, buyQInput, 
                              sellKInput, sellQInput, batteryCapInput, maxChargeInput, maxDischargeInput};
        for (auto input : inputs) {
            lv_obj_add_event_cb(input, intelligenceInputFocusHandler, LV_EVENT_FOCUSED, this);
            lv_obj_add_event_cb(input, intelligenceInputFocusHandler, LV_EVENT_DEFOCUSED, this);
        }
        
        // Load settings
        SolarIntelligenceSettings_t settings = IntelligenceSettingsStorage::load();
        loadSettingsToUI(settings);
        
        updateLocalizedTexts();
        loadScreen();
    }

    void hide() override {
        // Reset all pointers
        mainContainer = nullptr;
        leftColumn = nullptr;
        middleColumn = nullptr;
        rightColumn = nullptr;
        enableSwitch = nullptr;
        enableLabel = nullptr;
        betaWarning = nullptr;
        batteryCostLabel = nullptr;
        batteryCostInput = nullptr;
        minSocLabel = nullptr;
        minSocInput = nullptr;
        maxSocLabel = nullptr;
        maxSocInput = nullptr;
        batteryCapLabel = nullptr;
        batteryCapInput = nullptr;
        maxChargeLabel = nullptr;
        maxChargeInput = nullptr;
        maxDischargeLabel = nullptr;
        maxDischargeInput = nullptr;
        buyKLabel = nullptr;
        buyKInput = nullptr;
        buyQLabel = nullptr;
        buyQInput = nullptr;
        buyFormulaLabel = nullptr;
        buyInfoLabel = nullptr;
        sellKLabel = nullptr;
        sellKInput = nullptr;
        sellQLabel = nullptr;
        sellQInput = nullptr;
        sellFormulaLabel = nullptr;
        sellInfoLabel = nullptr;
        buttonContainer = nullptr;
        resetButton = nullptr;
        resetLabel = nullptr;
        saveButton = nullptr;
        saveLabel = nullptr;
        cancelButton = nullptr;
        cancelLabel = nullptr;
        keyboard = nullptr;
        
        BaseUI::hide();
    }

    void onSaveClick() {
        SolarIntelligenceSettings_t settings = readSettingsFromUI();
        LOGD("Saving intelligence settings: batCost=%.2f, minSoc=%d, maxSoc=%d", 
              settings.batteryCostPerKwh, settings.minSocPercent, settings.maxSocPercent);
        IntelligenceSettingsStorage::save(settings);
        resultSaved = true;
    }

    void onCancelClick() {
        resultCancelled = true;
        LOGD("Intelligence setup cancelled");
    }

    void onResetClick() {
        SolarIntelligenceSettings_t defaults = SolarIntelligenceSettings_t::getDefault();
        loadSettingsToUI(defaults);
        requestClearPredictions = true;
        LOGD("Intelligence settings reset to defaults");
    }

    void onInputFocus(lv_obj_t* obj, lv_event_code_t code) {
        if (!keyboard || !mainContainer) return;
        
        if (code == LV_EVENT_FOCUSED) {
            lv_keyboard_set_textarea(keyboard, obj);
            lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
            lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
            
            lv_coord_t kbHeight = 220;
            lv_area_t inputArea;
            lv_obj_get_coords(obj, &inputArea);
            lv_coord_t scrHeight = lv_obj_get_height(screen);
            lv_coord_t visibleBottom = scrHeight - kbHeight;
            
            if (inputArea.y2 > visibleBottom) {
                lv_coord_t scrollY = inputArea.y2 - visibleBottom + 30;
                lv_obj_scroll_by(mainContainer, 0, -scrollY, LV_ANIM_ON);
            }
        } else if (code == LV_EVENT_DEFOCUSED) {
            lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
            lv_obj_scroll_to_y(mainContainer, 0, LV_ANIM_ON);
        }
    }

private:
    void updateLocalizedTexts() {
        if (!initialized) return;
        
        // Header
        lv_obj_t* header = lv_obj_get_child(screen, 0);
        if (header) {
            lv_obj_t* titleLabel = lv_obj_get_child(header, 0);
            if (titleLabel) lv_label_set_text(titleLabel, TR(STR_INTELLIGENCE_SETTINGS));
            
            lv_obj_t* enableContainer = lv_obj_get_child(header, 1);
            if (enableContainer && enableLabel) {
                lv_label_set_text(enableLabel, TR(STR_ENABLE));
            }
        }
        
        // Beta warning
        if (betaWarning) {
            lv_obj_t* warningLabel = lv_obj_get_child(betaWarning, 0);
            if (warningLabel) lv_label_set_text(warningLabel, TR(STR_BETA_WARNING));
        }
        
        // Card titles
        if (leftColumn) {
            lv_obj_t* cardTitle = lv_obj_get_child(leftColumn, 0);
            if (cardTitle) lv_label_set_text(cardTitle, TR(STR_BATTERY));
        }
        if (middleColumn) {
            lv_obj_t* cardTitle = lv_obj_get_child(middleColumn, 0);
            if (cardTitle) lv_label_set_text(cardTitle, TR(STR_BUY_PRICE));
        }
        if (rightColumn) {
            lv_obj_t* cardTitle = lv_obj_get_child(rightColumn, 0);
            if (cardTitle) lv_label_set_text(cardTitle, TR(STR_SELL_PRICE));
        }
        
        // Buttons
        if (resetLabel) lv_label_set_text(resetLabel, TR(STR_RESET));
        if (cancelLabel) lv_label_set_text(cancelLabel, TR(STR_CANCEL));
        if (saveLabel) lv_label_set_text(saveLabel, TR(STR_SAVE));
        
        // Input labels
        if (batteryCostLabel) lv_label_set_text(batteryCostLabel, TR(STR_COST_PER_KWH));
        if (minSocLabel) lv_label_set_text(minSocLabel, TR(STR_MIN_SOC_PERCENT));
        if (maxSocLabel) lv_label_set_text(maxSocLabel, TR(STR_MAX_SOC_PERCENT));
        if (batteryCapLabel) lv_label_set_text(batteryCapLabel, TR(STR_CAPACITY_KWH));
        if (maxChargeLabel) lv_label_set_text(maxChargeLabel, TR(STR_MAX_CHARGE_KW));
        if (maxDischargeLabel) lv_label_set_text(maxDischargeLabel, TR(STR_MAX_DISCHARGE_KW));
        if (buyKLabel) lv_label_set_text(buyKLabel, TR(STR_K_MULTIPLIER));
        if (buyQLabel) lv_label_set_text(buyQLabel, TR(STR_Q_FIXED));
        if (sellKLabel) lv_label_set_text(sellKLabel, TR(STR_K_MULTIPLIER));
        if (sellQLabel) lv_label_set_text(sellQLabel, TR(STR_Q_FIXED));
        if (buyInfoLabel) lv_label_set_text(buyInfoLabel, TR(STR_BUY_INFO));
        if (sellInfoLabel) lv_label_set_text(sellInfoLabel, TR(STR_SELL_INFO));
        if (buyFormulaLabel) lv_label_set_text(buyFormulaLabel, TR(STR_PRICE_FORMULA));
        if (sellFormulaLabel) lv_label_set_text(sellFormulaLabel, TR(STR_PRICE_FORMULA));
    }

    void loadSettingsToUI(const SolarIntelligenceSettings_t& settings) {
        if (!enableSwitch) return;
        
        LOGD("Loading settings to UI");
        
        if (settings.enabled) {
            lv_obj_add_state(enableSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(enableSwitch, LV_STATE_CHECKED);
        }
        
        char buf[16];
        snprintf(buf, sizeof(buf), "%.2f", settings.batteryCostPerKwh);
        lv_textarea_set_text(batteryCostInput, buf);
        
        snprintf(buf, sizeof(buf), "%d", settings.minSocPercent);
        lv_textarea_set_text(minSocInput, buf);
        
        snprintf(buf, sizeof(buf), "%d", settings.maxSocPercent);
        lv_textarea_set_text(maxSocInput, buf);
        
        snprintf(buf, sizeof(buf), "%.2f", settings.buyK);
        lv_textarea_set_text(buyKInput, buf);
        
        snprintf(buf, sizeof(buf), "%.2f", settings.buyQ);
        lv_textarea_set_text(buyQInput, buf);
        
        snprintf(buf, sizeof(buf), "%.2f", settings.sellK);
        lv_textarea_set_text(sellKInput, buf);
        
        snprintf(buf, sizeof(buf), "%.2f", settings.sellQ);
        lv_textarea_set_text(sellQInput, buf);
        
        snprintf(buf, sizeof(buf), "%.1f", settings.batteryCapacityKwh);
        lv_textarea_set_text(batteryCapInput, buf);
        
        snprintf(buf, sizeof(buf), "%.1f", settings.maxChargePowerKw);
        lv_textarea_set_text(maxChargeInput, buf);
        
        snprintf(buf, sizeof(buf), "%.1f", settings.maxDischargePowerKw);
        lv_textarea_set_text(maxDischargeInput, buf);
    }

    SolarIntelligenceSettings_t readSettingsFromUI() {
        SolarIntelligenceSettings_t settings;
        
        settings.enabled = lv_obj_has_state(enableSwitch, LV_STATE_CHECKED);
        settings.batteryCostPerKwh = atof(lv_textarea_get_text(batteryCostInput));
        settings.minSocPercent = constrain(atoi(lv_textarea_get_text(minSocInput)), 0, 100);
        settings.maxSocPercent = constrain(atoi(lv_textarea_get_text(maxSocInput)), settings.minSocPercent, 100);
        settings.buyK = atof(lv_textarea_get_text(buyKInput));
        settings.buyQ = atof(lv_textarea_get_text(buyQInput));
        settings.sellK = atof(lv_textarea_get_text(sellKInput));
        settings.sellQ = atof(lv_textarea_get_text(sellQInput));
        settings.batteryCapacityKwh = atof(lv_textarea_get_text(batteryCapInput));
        if (settings.batteryCapacityKwh <= 0) settings.batteryCapacityKwh = 10.0f;
        settings.maxChargePowerKw = atof(lv_textarea_get_text(maxChargeInput));
        if (settings.maxChargePowerKw <= 0) settings.maxChargePowerKw = 8.0f;
        settings.maxDischargePowerKw = atof(lv_textarea_get_text(maxDischargeInput));
        if (settings.maxDischargePowerKw <= 0) settings.maxDischargePowerKw = 8.0f;
        
        return settings;
    }
};

// Event handlers
static void intelligenceSaveHandler(lv_event_t *e) {
    IntelligenceSetupUI *ui = (IntelligenceSetupUI *)lv_event_get_user_data(e);
    if (ui) ui->onSaveClick();
}

static void intelligenceCancelHandler(lv_event_t *e) {
    IntelligenceSetupUI *ui = (IntelligenceSetupUI *)lv_event_get_user_data(e);
    if (ui) ui->onCancelClick();
}

static void intelligenceResetHandler(lv_event_t *e) {
    IntelligenceSetupUI *ui = (IntelligenceSetupUI *)lv_event_get_user_data(e);
    if (ui) ui->onResetClick();
}

static void intelligenceInputFocusHandler(lv_event_t *e) {
    IntelligenceSetupUI *ui = (IntelligenceSetupUI *)lv_event_get_user_data(e);
    if (ui) ui->onInputFocus(lv_event_get_target(e), lv_event_get_code(e));
}
