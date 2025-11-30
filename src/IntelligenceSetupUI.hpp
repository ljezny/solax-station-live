#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "ui/ui.h"
#include "ui/ui_IntelligenceSetup.h"
#include "utils/IntelligenceSettings.hpp"

// Forward declarations of event handlers
static void intelligenceSaveHandler(lv_event_t *e);
static void intelligenceCancelHandler(lv_event_t *e);
static void intelligenceInputFocusHandler(lv_event_t *e);

class IntelligenceSetupUI {
public:
    bool resultSaved = false;
    bool resultCancelled = false;
    
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
        
        // Add event handlers
        lv_obj_add_event_cb(ui_intelligenceSaveButton, intelligenceSaveHandler, LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(ui_intelligenceCancelButton, intelligenceCancelHandler, LV_EVENT_CLICKED, this);
        
        // Focus handlers for keyboard
        lv_obj_add_event_cb(ui_intelligenceBatteryCostInput, intelligenceInputFocusHandler, LV_EVENT_FOCUSED, this);
        lv_obj_add_event_cb(ui_intelligenceMinSocInput, intelligenceInputFocusHandler, LV_EVENT_FOCUSED, this);
        lv_obj_add_event_cb(ui_intelligenceMaxSocInput, intelligenceInputFocusHandler, LV_EVENT_FOCUSED, this);
        lv_obj_add_event_cb(ui_intelligenceBuyKInput, intelligenceInputFocusHandler, LV_EVENT_FOCUSED, this);
        lv_obj_add_event_cb(ui_intelligenceBuyQInput, intelligenceInputFocusHandler, LV_EVENT_FOCUSED, this);
        lv_obj_add_event_cb(ui_intelligenceSellKInput, intelligenceInputFocusHandler, LV_EVENT_FOCUSED, this);
        lv_obj_add_event_cb(ui_intelligenceSellQInput, intelligenceInputFocusHandler, LV_EVENT_FOCUSED, this);
        
        // Defocus handlers to hide keyboard
        lv_obj_add_event_cb(ui_intelligenceBatteryCostInput, intelligenceInputFocusHandler, LV_EVENT_DEFOCUSED, this);
        lv_obj_add_event_cb(ui_intelligenceMinSocInput, intelligenceInputFocusHandler, LV_EVENT_DEFOCUSED, this);
        lv_obj_add_event_cb(ui_intelligenceMaxSocInput, intelligenceInputFocusHandler, LV_EVENT_DEFOCUSED, this);
        lv_obj_add_event_cb(ui_intelligenceBuyKInput, intelligenceInputFocusHandler, LV_EVENT_DEFOCUSED, this);
        lv_obj_add_event_cb(ui_intelligenceBuyQInput, intelligenceInputFocusHandler, LV_EVENT_DEFOCUSED, this);
        lv_obj_add_event_cb(ui_intelligenceSellKInput, intelligenceInputFocusHandler, LV_EVENT_DEFOCUSED, this);
        lv_obj_add_event_cb(ui_intelligenceSellQInput, intelligenceInputFocusHandler, LV_EVENT_DEFOCUSED, this);
    }
    
    void onSaveClick() {
        IntelligenceSettings_t settings = readSettingsFromUI();
        IntelligenceSettingsStorage::save(settings);
        resultSaved = true;
        log_d("Intelligence settings saved");
    }
    
    void onCancelClick() {
        resultCancelled = true;
        log_d("Intelligence setup cancelled");
    }
    
    void onInputFocus(lv_obj_t* obj, lv_event_code_t code) {
        if (code == LV_EVENT_FOCUSED) {
            // Show keyboard and attach to focused input
            lv_keyboard_set_textarea(ui_intelligenceKeyboard, obj);
            lv_obj_clear_flag(ui_intelligenceKeyboard, LV_OBJ_FLAG_HIDDEN);
            
            // Scroll to make sure input is visible
            lv_obj_scroll_to_view(obj, LV_ANIM_ON);
        } else if (code == LV_EVENT_DEFOCUSED) {
            // Hide keyboard
            lv_obj_add_flag(ui_intelligenceKeyboard, LV_OBJ_FLAG_HIDDEN);
        }
    }
    
private:
    void loadSettingsToUI(const IntelligenceSettings_t& settings) {
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

static void intelligenceInputFocusHandler(lv_event_t *e) {
    IntelligenceSetupUI *ui = (IntelligenceSetupUI *)lv_event_get_user_data(e);
    if (ui) {
        ui->onInputFocus(lv_event_get_target(e), lv_event_get_code(e));
    }
}
