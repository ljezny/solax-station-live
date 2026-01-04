#pragma once

#include <Arduino.h>
#include <RemoteLogger.hpp>
#include <lvgl.h>
#include "ui/ui.h"
#include "ui/ui_IntelligenceSetup.h"
#include <SolarIntelligence.h>
#include "utils/Localization.hpp"

// Forward declarations of event handlers
static void intelligenceSaveHandler(lv_event_t *e);
static void intelligenceCancelHandler(lv_event_t *e);
static void intelligenceResetHandler(lv_event_t *e);
static void intelligenceInputFocusHandler(lv_event_t *e);

class IntelligenceSetupUI {
public:
    bool resultSaved = false;
    bool resultCancelled = false;
    bool requestClearPredictions = false;  // Flag pro smazání predikcí (zpracuje app.cpp)
    
private:
    bool eventHandlersAdded = false;
    
public:
    IntelligenceSetupUI() {
    }
    
    void show() {
        resultSaved = false;
        resultCancelled = false;
        
        // Initialize screen if not done
        if (ui_IntelligenceSetup == NULL) {
            ui_IntelligenceSetup_screen_init();
        }
        
        // Update localized texts
        updateLocalizedTexts();
        
        lv_scr_load(ui_IntelligenceSetup);
        
        // Load current settings
        SolarIntelligenceSettings_t settings = IntelligenceSettingsStorage::load();
        loadSettingsToUI(settings);
        
        // Add event handlers only once
        if (!eventHandlersAdded) {
            lv_obj_add_event_cb(ui_intelligenceSaveButton, intelligenceSaveHandler, LV_EVENT_CLICKED, this);
            lv_obj_add_event_cb(ui_intelligenceCancelButton, intelligenceCancelHandler, LV_EVENT_CLICKED, this);
            lv_obj_add_event_cb(ui_intelligenceResetButton, intelligenceResetHandler, LV_EVENT_CLICKED, this);
            
            // Focus handlers for keyboard
            lv_obj_add_event_cb(ui_intelligenceBatteryCostInput, intelligenceInputFocusHandler, LV_EVENT_FOCUSED, this);
            lv_obj_add_event_cb(ui_intelligenceMinSocInput, intelligenceInputFocusHandler, LV_EVENT_FOCUSED, this);
            lv_obj_add_event_cb(ui_intelligenceMaxSocInput, intelligenceInputFocusHandler, LV_EVENT_FOCUSED, this);
            lv_obj_add_event_cb(ui_intelligenceBuyKInput, intelligenceInputFocusHandler, LV_EVENT_FOCUSED, this);
            lv_obj_add_event_cb(ui_intelligenceBuyQInput, intelligenceInputFocusHandler, LV_EVENT_FOCUSED, this);
            lv_obj_add_event_cb(ui_intelligenceSellKInput, intelligenceInputFocusHandler, LV_EVENT_FOCUSED, this);
            lv_obj_add_event_cb(ui_intelligenceSellQInput, intelligenceInputFocusHandler, LV_EVENT_FOCUSED, this);
            lv_obj_add_event_cb(ui_intelligenceBatteryCapInput, intelligenceInputFocusHandler, LV_EVENT_FOCUSED, this);
            lv_obj_add_event_cb(ui_intelligenceMaxChargeInput, intelligenceInputFocusHandler, LV_EVENT_FOCUSED, this);
            lv_obj_add_event_cb(ui_intelligenceMaxDischargeInput, intelligenceInputFocusHandler, LV_EVENT_FOCUSED, this);
            
            // Defocus handlers to hide keyboard
            lv_obj_add_event_cb(ui_intelligenceBatteryCostInput, intelligenceInputFocusHandler, LV_EVENT_DEFOCUSED, this);
            lv_obj_add_event_cb(ui_intelligenceMinSocInput, intelligenceInputFocusHandler, LV_EVENT_DEFOCUSED, this);
            lv_obj_add_event_cb(ui_intelligenceMaxSocInput, intelligenceInputFocusHandler, LV_EVENT_DEFOCUSED, this);
            lv_obj_add_event_cb(ui_intelligenceBuyKInput, intelligenceInputFocusHandler, LV_EVENT_DEFOCUSED, this);
            lv_obj_add_event_cb(ui_intelligenceBuyQInput, intelligenceInputFocusHandler, LV_EVENT_DEFOCUSED, this);
            lv_obj_add_event_cb(ui_intelligenceSellKInput, intelligenceInputFocusHandler, LV_EVENT_DEFOCUSED, this);
            lv_obj_add_event_cb(ui_intelligenceSellQInput, intelligenceInputFocusHandler, LV_EVENT_DEFOCUSED, this);
            lv_obj_add_event_cb(ui_intelligenceBatteryCapInput, intelligenceInputFocusHandler, LV_EVENT_DEFOCUSED, this);
            lv_obj_add_event_cb(ui_intelligenceMaxChargeInput, intelligenceInputFocusHandler, LV_EVENT_DEFOCUSED, this);
            lv_obj_add_event_cb(ui_intelligenceMaxDischargeInput, intelligenceInputFocusHandler, LV_EVENT_DEFOCUSED, this);
            
            eventHandlersAdded = true;
        }
    }
    
    void onSaveClick() {
        SolarIntelligenceSettings_t settings = readSettingsFromUI();
        LOGD("Saving intelligence settings: batCost=%.2f, minSoc=%d, maxSoc=%d, buyK=%.2f, buyQ=%.2f, sellK=%.2f, sellQ=%.2f, cap=%.1f, chg=%.1f, dis=%.1f",
              settings.batteryCostPerKwh, settings.minSocPercent, settings.maxSocPercent,
              settings.buyK, settings.buyQ, settings.sellK, settings.sellQ,
              settings.batteryCapacityKwh, settings.maxChargePowerKw, settings.maxDischargePowerKw);
        IntelligenceSettingsStorage::save(settings);
        resultSaved = true;
        LOGD("Intelligence settings saved");
    }
    
    void onCancelClick() {
        resultCancelled = true;
        LOGD("Intelligence setup cancelled");
    }
    
    void onResetClick() {
        // Reset settings to defaults and reload UI
        SolarIntelligenceSettings_t defaults = SolarIntelligenceSettings_t::getDefault();
        loadSettingsToUI(defaults);
        
        // Nastavit flag pro smazání predikcí (zpracuje app.cpp)
        requestClearPredictions = true;
        
        LOGD("Intelligence settings reset to defaults, predictions will be cleared");
    }
    
    void onInputFocus(lv_obj_t* obj, lv_event_code_t code) {
        if (code == LV_EVENT_FOCUSED) {
            // Show keyboard and attach to focused input
            lv_keyboard_set_textarea(ui_intelligenceKeyboard, obj);
            lv_obj_clear_flag(ui_intelligenceKeyboard, LV_OBJ_FLAG_HIDDEN);
            lv_obj_align(ui_intelligenceKeyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
            
            // Scroll the main container to make the input visible above keyboard
            // Keyboard height is 200px, add some margin
            lv_coord_t kbHeight = 220;
            
            // Get input position relative to main container
            lv_obj_t* mainContainer = ui_intelligenceMainContainer;
            lv_area_t inputArea;
            lv_obj_get_coords(obj, &inputArea);
            
            lv_area_t containerArea;
            lv_obj_get_coords(mainContainer, &containerArea);
            
            // Calculate visible area (screen height minus keyboard)
            lv_coord_t scrHeight = lv_obj_get_height(ui_IntelligenceSetup);
            lv_coord_t visibleBottom = scrHeight - kbHeight;
            
            // If input bottom is below visible area, scroll
            if (inputArea.y2 > visibleBottom) {
                lv_coord_t scrollY = inputArea.y2 - visibleBottom + 30;  // 30px extra margin
                lv_obj_scroll_by(mainContainer, 0, -scrollY, LV_ANIM_ON);
            }
        } else if (code == LV_EVENT_DEFOCUSED) {
            // Hide keyboard
            lv_obj_add_flag(ui_intelligenceKeyboard, LV_OBJ_FLAG_HIDDEN);
            
            // Scroll back to top
            lv_obj_scroll_to_y(ui_intelligenceMainContainer, 0, LV_ANIM_ON);
        }
    }
    
private:
    void updateLocalizedTexts() {
        // Find and update labels dynamically
        // Header title - first child of header (which is first child of screen)
        lv_obj_t* header = lv_obj_get_child(ui_IntelligenceSetup, 0);
        if (header) {
            lv_obj_t* titleLabel = lv_obj_get_child(header, 0);
            if (titleLabel) lv_label_set_text(titleLabel, TR(STR_INTELLIGENCE_SETTINGS));
            
            // Enable container is second child of header
            lv_obj_t* enableContainer = lv_obj_get_child(header, 1);
            if (enableContainer) {
                lv_obj_t* enableLabel = lv_obj_get_child(enableContainer, 0);
                if (enableLabel) lv_label_set_text(enableLabel, TR(STR_ENABLE));
            }
        }
        
        // Beta warning - update text from localization
        if (ui_intelligenceBetaWarning) {
            lv_obj_t* warningLabel = lv_obj_get_child(ui_intelligenceBetaWarning, 0);
            if (warningLabel) lv_label_set_text(warningLabel, TR(STR_BETA_WARNING));
        }
        
        // Left column (Battery) - card title is first child
        if (ui_intelligenceLeftColumn) {
            lv_obj_t* cardTitle = lv_obj_get_child(ui_intelligenceLeftColumn, 0);
            if (cardTitle) lv_label_set_text(cardTitle, TR(STR_BATTERY));
        }
        
        // Middle column (Buy Price)
        if (ui_intelligenceMiddleColumn) {
            lv_obj_t* cardTitle = lv_obj_get_child(ui_intelligenceMiddleColumn, 0);
            if (cardTitle) lv_label_set_text(cardTitle, TR(STR_BUY_PRICE));
            // Formula label is second child
            lv_obj_t* formulaLabel = lv_obj_get_child(ui_intelligenceMiddleColumn, 1);
            if (formulaLabel) lv_label_set_text(formulaLabel, TR(STR_PRICE_FORMULA));
        }
        
        // Right column (Sell Price)
        if (ui_intelligenceRightColumn) {
            lv_obj_t* cardTitle = lv_obj_get_child(ui_intelligenceRightColumn, 0);
            if (cardTitle) lv_label_set_text(cardTitle, TR(STR_SELL_PRICE));
            // Formula label is second child
            lv_obj_t* formulaLabel = lv_obj_get_child(ui_intelligenceRightColumn, 1);
            if (formulaLabel) lv_label_set_text(formulaLabel, TR(STR_PRICE_FORMULA));
        }
        
        // Button labels
        if (ui_intelligenceResetLabel) lv_label_set_text(ui_intelligenceResetLabel, TR(STR_RESET));
        if (ui_intelligenceCancelLabel) lv_label_set_text(ui_intelligenceCancelLabel, TR(STR_CANCEL));
        if (ui_intelligenceSaveLabel) lv_label_set_text(ui_intelligenceSaveLabel, TR(STR_SAVE));
        
        // Input labels
        if (ui_intelligenceBatteryCostLabel) lv_label_set_text(ui_intelligenceBatteryCostLabel, TR(STR_COST_PER_KWH));
        if (ui_intelligenceMinSocLabel) lv_label_set_text(ui_intelligenceMinSocLabel, TR(STR_MIN_SOC_PERCENT));
        if (ui_intelligenceMaxSocLabel) lv_label_set_text(ui_intelligenceMaxSocLabel, TR(STR_MAX_SOC_PERCENT));
        if (ui_intelligenceBatteryCapLabel) lv_label_set_text(ui_intelligenceBatteryCapLabel, TR(STR_CAPACITY_KWH));
        if (ui_intelligenceMaxChargeLabel) lv_label_set_text(ui_intelligenceMaxChargeLabel, TR(STR_MAX_CHARGE_KW));
        if (ui_intelligenceMaxDischargeLabel) lv_label_set_text(ui_intelligenceMaxDischargeLabel, TR(STR_MAX_DISCHARGE_KW));
        if (ui_intelligenceBuyKLabel) lv_label_set_text(ui_intelligenceBuyKLabel, TR(STR_K_MULTIPLIER));
        if (ui_intelligenceBuyQLabel) lv_label_set_text(ui_intelligenceBuyQLabel, TR(STR_Q_FIXED));
        if (ui_intelligenceSellKLabel) lv_label_set_text(ui_intelligenceSellKLabel, TR(STR_K_MULTIPLIER));
        if (ui_intelligenceSellQLabel) lv_label_set_text(ui_intelligenceSellQLabel, TR(STR_Q_FIXED));
        
        // Info labels
        if (ui_intelligenceBuyInfoLabel) lv_label_set_text(ui_intelligenceBuyInfoLabel, TR(STR_BUY_INFO));
        if (ui_intelligenceSellInfoLabel) lv_label_set_text(ui_intelligenceSellInfoLabel, TR(STR_SELL_INFO));
        if (ui_intelligenceBuyFormulaLabel) lv_label_set_text(ui_intelligenceBuyFormulaLabel, TR(STR_PRICE_FORMULA));
        if (ui_intelligenceSellFormulaLabel) lv_label_set_text(ui_intelligenceSellFormulaLabel, TR(STR_PRICE_FORMULA));
    }
    
    void loadSettingsToUI(const SolarIntelligenceSettings_t& settings) {
        LOGD("Loading settings to UI: batCost=%.2f, minSoc=%d, maxSoc=%d",
              settings.batteryCostPerKwh, settings.minSocPercent, settings.maxSocPercent);
        
        // Enable switch
        if (settings.enabled) {
            lv_obj_add_state(ui_intelligenceEnableSwitch, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(ui_intelligenceEnableSwitch, LV_STATE_CHECKED);
        }
        
        // Battery cost
        char buf[16];
        snprintf(buf, sizeof(buf), "%.2f", settings.batteryCostPerKwh);
        lv_textarea_set_text(ui_intelligenceBatteryCostInput, buf);
        
        // Min/Max SOC
        snprintf(buf, sizeof(buf), "%d", settings.minSocPercent);
        lv_textarea_set_text(ui_intelligenceMinSocInput, buf);
        
        snprintf(buf, sizeof(buf), "%d", settings.maxSocPercent);
        lv_textarea_set_text(ui_intelligenceMaxSocInput, buf);
        
        // Buy coefficients
        snprintf(buf, sizeof(buf), "%.2f", settings.buyK);
        lv_textarea_set_text(ui_intelligenceBuyKInput, buf);
        
        snprintf(buf, sizeof(buf), "%.2f", settings.buyQ);
        lv_textarea_set_text(ui_intelligenceBuyQInput, buf);
        
        // Sell coefficients
        snprintf(buf, sizeof(buf), "%.2f", settings.sellK);
        lv_textarea_set_text(ui_intelligenceSellKInput, buf);
        
        snprintf(buf, sizeof(buf), "%.2f", settings.sellQ);
        lv_textarea_set_text(ui_intelligenceSellQInput, buf);
        
        // Battery parameters
        snprintf(buf, sizeof(buf), "%.1f", settings.batteryCapacityKwh);
        lv_textarea_set_text(ui_intelligenceBatteryCapInput, buf);
        
        snprintf(buf, sizeof(buf), "%.1f", settings.maxChargePowerKw);
        lv_textarea_set_text(ui_intelligenceMaxChargeInput, buf);
        
        snprintf(buf, sizeof(buf), "%.1f", settings.maxDischargePowerKw);
        lv_textarea_set_text(ui_intelligenceMaxDischargeInput, buf);
    }
    
    SolarIntelligenceSettings_t readSettingsFromUI() {
        SolarIntelligenceSettings_t settings;
        
        // Enable switch
        settings.enabled = lv_obj_has_state(ui_intelligenceEnableSwitch, LV_STATE_CHECKED);
        
        // Battery cost
        settings.batteryCostPerKwh = atof(lv_textarea_get_text(ui_intelligenceBatteryCostInput));
        
        // Min/Max SOC
        settings.minSocPercent = atoi(lv_textarea_get_text(ui_intelligenceMinSocInput));
        settings.maxSocPercent = atoi(lv_textarea_get_text(ui_intelligenceMaxSocInput));
        
        // Validate SOC values
        settings.minSocPercent = constrain(settings.minSocPercent, 0, 100);
        settings.maxSocPercent = constrain(settings.maxSocPercent, settings.minSocPercent, 100);
        
        // Buy coefficients
        settings.buyK = atof(lv_textarea_get_text(ui_intelligenceBuyKInput));
        settings.buyQ = atof(lv_textarea_get_text(ui_intelligenceBuyQInput));
        
        // Sell coefficients
        settings.sellK = atof(lv_textarea_get_text(ui_intelligenceSellKInput));
        settings.sellQ = atof(lv_textarea_get_text(ui_intelligenceSellQInput));
        
        // Battery parameters
        settings.batteryCapacityKwh = atof(lv_textarea_get_text(ui_intelligenceBatteryCapInput));
        settings.maxChargePowerKw = atof(lv_textarea_get_text(ui_intelligenceMaxChargeInput));
        settings.maxDischargePowerKw = atof(lv_textarea_get_text(ui_intelligenceMaxDischargeInput));
        
        // Validate battery parameters
        if (settings.batteryCapacityKwh <= 0) settings.batteryCapacityKwh = 10.0f;
        if (settings.maxChargePowerKw <= 0) settings.maxChargePowerKw = 8.0f;
        if (settings.maxDischargePowerKw <= 0) settings.maxDischargePowerKw = 8.0f;
        
        return settings;
    }
};

// Event handler implementations
static void intelligenceSaveHandler(lv_event_t *e) {
    IntelligenceSetupUI *ui = (IntelligenceSetupUI *)lv_event_get_user_data(e);
    if (ui) {
        ui->onSaveClick();
    }
}

static void intelligenceCancelHandler(lv_event_t *e) {
    IntelligenceSetupUI *ui = (IntelligenceSetupUI *)lv_event_get_user_data(e);
    if (ui) {
        ui->onCancelClick();
    }
}

static void intelligenceResetHandler(lv_event_t *e) {
    IntelligenceSetupUI *ui = (IntelligenceSetupUI *)lv_event_get_user_data(e);
    if (ui) {
        ui->onResetClick();
    }
}

static void intelligenceInputFocusHandler(lv_event_t *e) {
    IntelligenceSetupUI *ui = (IntelligenceSetupUI *)lv_event_get_user_data(e);
    if (ui) {
        ui->onInputFocus(lv_event_get_target(e), lv_event_get_code(e));
    }
}
