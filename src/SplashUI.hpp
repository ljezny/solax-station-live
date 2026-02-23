#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "BaseUI.hpp"
#include "utils/Localization.hpp"

// Image declaration (from SquareLine assets)
LV_IMG_DECLARE(ui_img_logo_png);

// Font declarations
LV_FONT_DECLARE(ui_font_OpenSansSmall);
LV_FONT_DECLARE(ui_font_OpenSansMedium);

// Checklist item states
enum SplashCheckState_t {
    CHECK_PENDING = 0,    // Waiting (gray)
    CHECK_IN_PROGRESS,    // In progress (white)
    CHECK_SUCCESS,        // Success (green checkmark)
    CHECK_ERROR           // Error (red X)
};

class SplashUI : public BaseUI
{
private:
    // Logo phase elements
    lv_obj_t* logoImage = nullptr;
    lv_obj_t* splashLabel = nullptr;
    lv_obj_t* fwVersionLabel = nullptr;
    lv_obj_t* espIdLabel = nullptr;
    
    // Two-column layout for checklist phase
    lv_obj_t* mainContainer = nullptr;    // Row container: logo | checklist
    lv_obj_t* smallLogo = nullptr;        // Smaller logo for checklist phase
    lv_obj_t* checklistContainer = nullptr;
    lv_obj_t* checkItems[6] = {nullptr};  // WiFi found, WiFi connected, Time sync, Inverter found, Contacting, Data loaded
    lv_obj_t* checkLabels[6] = {nullptr};
    lv_obj_t* statusLabel = nullptr;
    lv_obj_t* continueBtn = nullptr;
    bool checklistCreated = false;
    bool buttonPressed = false;
    
    static const int ITEM_COUNT = 6;
    
    static void btnClickHandler(lv_event_t* e) {
        SplashUI* self = (SplashUI*)lv_event_get_user_data(e);
        self->buttonPressed = true;
    }
    
    void createLogoPhase() {
        // Main logo centered
        logoImage = lv_img_create(screen);
        lv_img_set_src(logoImage, &ui_img_logo_png);
        lv_obj_set_width(logoImage, LV_SIZE_CONTENT);
        lv_obj_set_height(logoImage, LV_SIZE_CONTENT);
        lv_obj_set_align(logoImage, LV_ALIGN_CENTER);
        lv_obj_add_flag(logoImage, LV_OBJ_FLAG_ADV_HITTEST);
        lv_obj_clear_flag(logoImage, LV_OBJ_FLAG_SCROLLABLE);
        
        // Splash label (centered below logo)
        splashLabel = lv_label_create(screen);
        lv_obj_set_width(splashLabel, lv_pct(50));
        lv_obj_set_height(splashLabel, LV_SIZE_CONTENT);
        lv_obj_set_align(splashLabel, LV_ALIGN_CENTER);
        lv_obj_set_y(splashLabel, 100);  // Below logo
        lv_label_set_text(splashLabel, "");
        lv_obj_set_style_text_align(splashLabel, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_font(splashLabel, &ui_font_OpenSansMedium, LV_PART_MAIN);
        
        // Version label (bottom left)
        fwVersionLabel = lv_label_create(screen);
        lv_obj_set_width(fwVersionLabel, lv_pct(20));
        lv_obj_set_height(fwVersionLabel, LV_SIZE_CONTENT);
        lv_obj_set_align(fwVersionLabel, LV_ALIGN_BOTTOM_LEFT);
        lv_label_set_text(fwVersionLabel, "");
        lv_obj_add_flag(fwVersionLabel, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_font(fwVersionLabel, &ui_font_OpenSansSmall, LV_PART_MAIN);
        lv_obj_set_style_pad_all(fwVersionLabel, 8, LV_PART_MAIN);
        
        // ESP ID label (bottom right)
        espIdLabel = lv_label_create(screen);
        lv_obj_set_width(espIdLabel, lv_pct(30));
        lv_obj_set_height(espIdLabel, LV_SIZE_CONTENT);
        lv_obj_set_align(espIdLabel, LV_ALIGN_BOTTOM_RIGHT);
        lv_label_set_text(espIdLabel, "");
        lv_obj_add_flag(espIdLabel, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_align(espIdLabel, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_obj_set_style_text_font(espIdLabel, &ui_font_OpenSansSmall, LV_PART_MAIN);
        lv_obj_set_style_pad_all(espIdLabel, 8, LV_PART_MAIN);
    }
    
    void createChecklist() {
        if (checklistCreated) return;
        
        // Hide original large logo and splash label
        lv_obj_add_flag(logoImage, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(splashLabel, LV_OBJ_FLAG_HIDDEN);
        
        // Create main row container (logo left | checklist right)
        mainContainer = lv_obj_create(screen);
        lv_obj_set_size(mainContainer, 780, 400);
        lv_obj_center(mainContainer);
        lv_obj_set_style_bg_opa(mainContainer, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(mainContainer, 0, 0);
        lv_obj_set_style_pad_all(mainContainer, 0, 0);
        lv_obj_clear_flag(mainContainer, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(mainContainer, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(mainContainer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(mainContainer, 40, 0);
        
        // Left column: smaller logo (scaled down)
        lv_obj_t* logoContainer = lv_obj_create(mainContainer);
        lv_obj_set_size(logoContainer, 280, 380);
        lv_obj_set_style_bg_opa(logoContainer, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(logoContainer, 0, 0);
        lv_obj_clear_flag(logoContainer, LV_OBJ_FLAG_SCROLLABLE);
        
        smallLogo = lv_img_create(logoContainer);
        lv_img_set_src(smallLogo, &ui_img_logo_png);
        lv_obj_center(smallLogo);
        lv_img_set_zoom(smallLogo, 200);  // 200/256 = ~78% size
        
        // Right column: scrollable checklist container
        checklistContainer = lv_obj_create(mainContainer);
        lv_obj_set_size(checklistContainer, 420, 380);
        lv_obj_set_style_bg_opa(checklistContainer, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(checklistContainer, 0, 0);
        lv_obj_set_style_pad_all(checklistContainer, 10, 0);
        // Enable vertical scrolling
        lv_obj_set_scroll_dir(checklistContainer, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(checklistContainer, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_flex_flow(checklistContainer, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(checklistContainer, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_row(checklistContainer, 6, 0);
        
        // String IDs for each checklist item
        StringId_t itemStrings[ITEM_COUNT] = {
            STR_SPLASH_WIFI_FOUND,
            STR_SPLASH_WIFI_CONNECTED,
            STR_SPLASH_TIME_SYNCED,
            STR_SPLASH_INVERTER_FOUND,
            STR_SPLASH_CONTACTING_INVERTER,
            STR_SPLASH_DATA_LOADED
        };
        
        // Create each checklist item
        for (int i = 0; i < ITEM_COUNT; i++) {
            lv_obj_t* row = lv_obj_create(checklistContainer);
            lv_obj_set_size(row, 400, 28);
            lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(row, 0, 0);
            lv_obj_set_style_pad_all(row, 0, 0);
            lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(row, 10, 0);
            
            // Status icon (use Montserrat for symbols)
            checkItems[i] = lv_label_create(row);
            lv_obj_set_size(checkItems[i], 24, 28);
            lv_label_set_text(checkItems[i], "");
            lv_obj_set_style_text_font(checkItems[i], &lv_font_montserrat_20, 0);
            lv_obj_set_style_text_align(checkItems[i], LV_TEXT_ALIGN_CENTER, 0);
            
            // Item label
            checkLabels[i] = lv_label_create(row);
            lv_label_set_text(checkLabels[i], TR(itemStrings[i]));
            lv_obj_set_style_text_font(checkLabels[i], &ui_font_OpenSansMedium, 0);
            lv_obj_set_style_text_color(checkLabels[i], lv_color_hex(0x666666), 0);
        }
        
        // Status label for error messages
        statusLabel = lv_label_create(checklistContainer);
        lv_obj_set_width(statusLabel, 400);
        lv_label_set_text(statusLabel, "");
        lv_obj_set_style_text_font(statusLabel, &ui_font_OpenSansSmall, 0);
        lv_obj_set_style_text_color(statusLabel, lv_color_hex(0xFF6666), 0);
        lv_obj_set_style_text_align(statusLabel, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_pad_top(statusLabel, 10, 0);
        lv_label_set_long_mode(statusLabel, LV_LABEL_LONG_WRAP);
        
        // Continue button
        continueBtn = lv_btn_create(checklistContainer);
        lv_obj_set_size(continueBtn, 120, 40);
        lv_obj_set_style_bg_color(continueBtn, lv_color_hex(0x444444), 0);
        lv_obj_add_flag(continueBtn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(continueBtn, btnClickHandler, LV_EVENT_CLICKED, this);
        lv_obj_set_style_pad_top(continueBtn, 5, 0);
        
        lv_obj_t* btnLabel = lv_label_create(continueBtn);
        lv_label_set_text(btnLabel, "OK");
        lv_obj_center(btnLabel);
        lv_obj_set_style_text_font(btnLabel, &ui_font_OpenSansMedium, 0);
        
        checklistCreated = true;
    }

public:
    void show() override
    {
        // Clean up any previous state
        hide();
        
        // Create screen with flex column layout
        createScreen();
        lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(screen, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(screen, 32, LV_PART_MAIN);
        lv_obj_set_style_pad_column(screen, 0, LV_PART_MAIN);
        
        // Reset state
        checklistCreated = false;
        buttonPressed = false;
        mainContainer = nullptr;
        checklistContainer = nullptr;
        smallLogo = nullptr;
        statusLabel = nullptr;
        continueBtn = nullptr;
        for (int i = 0; i < ITEM_COUNT; i++) {
            checkItems[i] = nullptr;
            checkLabels[i] = nullptr;
        }
        
        // Create logo phase elements
        createLogoPhase();
        
        // Load the screen
        loadScreen();
    }
    
    void hide() override
    {
        // Reset all pointers before deleting screen
        logoImage = nullptr;
        splashLabel = nullptr;
        fwVersionLabel = nullptr;
        espIdLabel = nullptr;
        mainContainer = nullptr;
        checklistContainer = nullptr;
        smallLogo = nullptr;
        statusLabel = nullptr;
        continueBtn = nullptr;
        checklistCreated = false;
        for (int i = 0; i < ITEM_COUNT; i++) {
            checkItems[i] = nullptr;
            checkLabels[i] = nullptr;
        }
        
        // Call parent to delete screen
        BaseUI::hide();
    }
    
    // Show logo phase (during WiFi scan)
    void showLogo() {
        if (logoImage == nullptr) return;
        
        // Make sure original elements are visible and centered
        lv_obj_clear_flag(logoImage, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(splashLabel, LV_OBJ_FLAG_HIDDEN);
        // Hide version labels during logo phase
        lv_obj_add_flag(fwVersionLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(espIdLabel, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(splashLabel, "");
    }

    void update(String espId, String fwVersion)
    {
        if (fwVersionLabel != nullptr) {
            lv_label_set_text(fwVersionLabel, fwVersion.c_str());
        }
        if (espIdLabel != nullptr) {
            lv_label_set_text(espIdLabel, espId.c_str());
        }
    }

    void updateText(String text)
    {
        if (splashLabel != nullptr) {
            lv_label_set_text(splashLabel, text.c_str());
        }
    }
    
    void showChecklist() {
        createChecklist();
    }
    
    void setCheckState(int index, SplashCheckState_t state, const char* customText = nullptr) {
        if (!checklistCreated || index < 0 || index >= ITEM_COUNT) return;
        
        switch (state) {
            case CHECK_PENDING:
                lv_label_set_text(checkItems[index], "");
                lv_obj_set_style_text_color(checkLabels[index], lv_color_hex(0x666666), 0);
                break;
            case CHECK_IN_PROGRESS:
                lv_label_set_text(checkItems[index], LV_SYMBOL_RIGHT);
                lv_obj_set_style_text_color(checkItems[index], lv_color_hex(0xFFCC00), 0);  // Yellow
                lv_obj_set_style_text_color(checkLabels[index], lv_color_hex(0xFFCC00), 0);  // Yellow
                break;
            case CHECK_SUCCESS:
                lv_label_set_text(checkItems[index], LV_SYMBOL_OK);
                lv_obj_set_style_text_color(checkItems[index], lv_color_hex(0x00CC66), 0);
                lv_obj_set_style_text_color(checkLabels[index], lv_color_hex(0x00CC66), 0);
                break;
            case CHECK_ERROR:
                lv_label_set_text(checkItems[index], LV_SYMBOL_CLOSE);
                lv_obj_set_style_text_color(checkItems[index], lv_color_hex(0xFF4444), 0);
                lv_obj_set_style_text_color(checkLabels[index], lv_color_hex(0xFF4444), 0);
                break;
        }
        
        if (customText != nullptr) {
            lv_label_set_text(checkLabels[index], customText);
        }
        
        // Auto-scroll to show current item (with animation)
        if (checklistContainer != nullptr && index >= 0 && index < ITEM_COUNT) {
            lv_obj_t* row = lv_obj_get_parent(checkItems[index]);
            if (row != nullptr) {
                lv_obj_scroll_to_view(row, LV_ANIM_ON);
            }
        }
    }
    
    void setStatusText(const char* text) {
        if (statusLabel != nullptr) {
            lv_label_set_text(statusLabel, text);
            // Auto-scroll to show status text
            if (checklistContainer != nullptr) {
                lv_obj_scroll_to_view(statusLabel, LV_ANIM_ON);
            }
        }
    }
    
    void showContinueButton() {
        if (continueBtn != nullptr) {
            lv_obj_clear_flag(continueBtn, LV_OBJ_FLAG_HIDDEN);
            buttonPressed = false;
            // Auto-scroll to show the button
            if (checklistContainer != nullptr) {
                lv_obj_scroll_to_view(continueBtn, LV_ANIM_ON);
            }
        }
    }
    
    bool isButtonPressed() {
        return buttonPressed;
    }
    
    void resetButton() {
        buttonPressed = false;
    }
    
    // Checklist indices
    static const int CHECK_WIFI_FOUND = 0;
    static const int CHECK_WIFI_CONNECTED = 1;
    static const int CHECK_TIME_SYNC = 2;
    static const int CHECK_INVERTER_FOUND = 3;
    static const int CHECK_CONTACTING = 4;
    static const int CHECK_DATA_LOADED = 5;
};
