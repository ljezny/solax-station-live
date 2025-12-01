#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "ui/ui.h"
#include "ui/ui_IntelligenceSetup.h"
#include "utils/IntelligenceSettings.hpp"

// Forward declarations of event handlers
static void intelligenceSaveHandler(lv_event_t *e);
static void intelligenceCancelHandler(lv_event_t *e);
static void intelligenceResetHandler(lv_event_t *e);
static void intelligenceInputFocusHandler(lv_event_t *e);

class IntelligenceSetupUI {
public:
    bool resultSaved = false;
    bool resultCancelled = false;
    
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
        
        lv_scr_load(ui_IntelligenceSetup);
        
        // Load current settings
        IntelligenceSettings_t settings = IntelligenceSettingsStorage::load();
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
        IntelligenceSettings_t settings = readSettingsFromUI();
        log_d("Saving intelligence settings: batCost=%.2f, minSoc=%d, maxSoc=%d, buyK=%.2f, buyQ=%.2f, sellK=%.2f, sellQ=%.2f, cap=%.1f, chg=%.1f, dis=%.1f",
              settings.batteryCostPerKwh, settings.minSocPercent, settings.maxSocPercent,
              settings.buyK, settings.buyQ, settings.sellK, settings.sellQ,
              settings.batteryCapacityKwh, settings.maxChargePowerKw, settings.maxDischargePowerKw);
        IntelligenceSettingsStorage::save(settings);
        resultSaved = true;
        log_d("Intelligence settings saved");
    }
    
    void onCancelClick() {
        resultCancelled = true;
        log_d("Intelligence setup cancelled");
    }
    
    void onResetClick() {
        // Reset settings to defaults and reload UI
        IntelligenceSettings_t defaults = IntelligenceSettings_t::getDefault();
        loadSettingsToUI(defaults);
        log_d("Intelligence settings reset to defaults");
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
    void loadSettingsToUI(const IntelligenceSettings_t& settings) {
        log_d("Loading settings to UI: batCost=%.2f, minSoc=%d, maxSoc=%d",
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
    
    IntelligenceSettings_t readSettingsFromUI() {
        IntelligenceSettings_t settings;
        
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
        if (settings.maxChargePowerKw <= 0) settings.maxChargePowerKw = 5.0f;
        if (settings.maxDischargePowerKw <= 0) settings.maxDischargePowerKw = 5.0f;
        
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
