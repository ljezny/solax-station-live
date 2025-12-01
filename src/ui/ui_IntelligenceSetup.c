// Intelligence Setup Screen
// LVGL version: 8.3.11
// Project name: SolaxStationLive_Project

#include "ui.h"

lv_obj_t *ui_IntelligenceSetup = NULL;
lv_obj_t *ui_intelligenceMainContainer = NULL;

// Left column - Enable & Battery
lv_obj_t *ui_intelligenceLeftColumn = NULL;
lv_obj_t *ui_intelligenceEnableSwitch = NULL;
lv_obj_t *ui_intelligenceEnableLabel = NULL;
lv_obj_t *ui_intelligenceBatteryCostLabel = NULL;
lv_obj_t *ui_intelligenceBatteryCostInput = NULL;
lv_obj_t *ui_intelligenceMinSocLabel = NULL;
lv_obj_t *ui_intelligenceMinSocInput = NULL;
lv_obj_t *ui_intelligenceMaxSocLabel = NULL;
lv_obj_t *ui_intelligenceMaxSocInput = NULL;
lv_obj_t *ui_intelligenceBatteryCapLabel = NULL;
lv_obj_t *ui_intelligenceBatteryCapInput = NULL;
lv_obj_t *ui_intelligenceMaxChargeLabel = NULL;
lv_obj_t *ui_intelligenceMaxChargeInput = NULL;
lv_obj_t *ui_intelligenceMaxDischargeLabel = NULL;
lv_obj_t *ui_intelligenceMaxDischargeInput = NULL;

// Middle column - Buy coefficients
lv_obj_t *ui_intelligenceMiddleColumn = NULL;
lv_obj_t *ui_intelligenceBuyLabel = NULL;
lv_obj_t *ui_intelligenceBuyKLabel = NULL;
lv_obj_t *ui_intelligenceBuyKInput = NULL;
lv_obj_t *ui_intelligenceBuyQLabel = NULL;
lv_obj_t *ui_intelligenceBuyQInput = NULL;

// Right column - Sell coefficients
lv_obj_t *ui_intelligenceRightColumn = NULL;
lv_obj_t *ui_intelligenceSellLabel = NULL;
lv_obj_t *ui_intelligenceSellKLabel = NULL;
lv_obj_t *ui_intelligenceSellKInput = NULL;
lv_obj_t *ui_intelligenceSellQLabel = NULL;
lv_obj_t *ui_intelligenceSellQInput = NULL;

// Bottom - Buttons
lv_obj_t *ui_intelligenceButtonContainer = NULL;
lv_obj_t *ui_intelligenceSaveButton = NULL;
lv_obj_t *ui_intelligenceSaveLabel = NULL;
lv_obj_t *ui_intelligenceCancelButton = NULL;
lv_obj_t *ui_intelligenceCancelLabel = NULL;

// Keyboard
lv_obj_t *ui_intelligenceKeyboard = NULL;

// Helper function to create a labeled input row
static lv_obj_t* create_input_row(lv_obj_t* parent, const char* labelText, lv_obj_t** labelOut, lv_obj_t** inputOut, const char* placeholder) {
    lv_obj_t* container = lv_obj_create(parent);
    lv_obj_remove_style_all(container);
    lv_obj_set_width(container, lv_pct(100));
    lv_obj_set_height(container, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    
    *labelOut = lv_label_create(container);
    lv_obj_set_width(*labelOut, lv_pct(50));
    lv_label_set_text(*labelOut, labelText);
    
    *inputOut = lv_textarea_create(container);
    lv_obj_set_width(*inputOut, lv_pct(45));
    lv_obj_set_height(*inputOut, LV_SIZE_CONTENT);
    lv_textarea_set_max_length(*inputOut, 10);
    lv_textarea_set_placeholder_text(*inputOut, placeholder);
    lv_textarea_set_one_line(*inputOut, true);
    lv_obj_add_flag(*inputOut, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    
    return container;
}

void ui_IntelligenceSetup_screen_init(void)
{
    // Main screen
    ui_IntelligenceSetup = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_IntelligenceSetup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(ui_IntelligenceSetup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui_IntelligenceSetup, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(ui_IntelligenceSetup, 8, LV_PART_MAIN);
    
    // Title
    lv_obj_t* title = lv_label_create(ui_IntelligenceSetup);
    lv_label_set_text(title, "Intelligence Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_pad_bottom(title, 8, 0);
    
    // Main container with 3 columns
    ui_intelligenceMainContainer = lv_obj_create(ui_IntelligenceSetup);
    lv_obj_remove_style_all(ui_intelligenceMainContainer);
    lv_obj_set_width(ui_intelligenceMainContainer, lv_pct(100));
    lv_obj_set_flex_grow(ui_intelligenceMainContainer, 1);
    lv_obj_set_flex_flow(ui_intelligenceMainContainer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui_intelligenceMainContainer, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(ui_intelligenceMainContainer, LV_OBJ_FLAG_SCROLLABLE);  // Enable scrolling
    lv_obj_set_scroll_dir(ui_intelligenceMainContainer, LV_DIR_VER);  // Vertical scroll only
    lv_obj_set_style_pad_column(ui_intelligenceMainContainer, 16, LV_PART_MAIN);
    
    // ===== LEFT COLUMN - Enable & Battery =====
    ui_intelligenceLeftColumn = lv_obj_create(ui_intelligenceMainContainer);
    lv_obj_remove_style_all(ui_intelligenceLeftColumn);
    lv_obj_set_height(ui_intelligenceLeftColumn, LV_SIZE_CONTENT);  // Content height for scrolling
    lv_obj_set_flex_grow(ui_intelligenceLeftColumn, 1);
    lv_obj_set_flex_flow(ui_intelligenceLeftColumn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui_intelligenceLeftColumn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(ui_intelligenceLeftColumn, 8, LV_PART_MAIN);
    
    // Section title
    lv_obj_t* leftTitle = lv_label_create(ui_intelligenceLeftColumn);
    lv_label_set_text(leftTitle, "General");
    lv_obj_set_style_text_font(leftTitle, &lv_font_montserrat_16, 0);
    
    // Enable switch row
    lv_obj_t* enableContainer = lv_obj_create(ui_intelligenceLeftColumn);
    lv_obj_remove_style_all(enableContainer);
    lv_obj_set_width(enableContainer, lv_pct(100));
    lv_obj_set_height(enableContainer, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(enableContainer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(enableContainer, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(enableContainer, LV_OBJ_FLAG_SCROLLABLE);
    
    ui_intelligenceEnableLabel = lv_label_create(enableContainer);
    lv_label_set_text(ui_intelligenceEnableLabel, "Enable");
    
    ui_intelligenceEnableSwitch = lv_switch_create(enableContainer);
    lv_obj_set_width(ui_intelligenceEnableSwitch, 50);
    
    // Battery cost input
    create_input_row(ui_intelligenceLeftColumn, "Battery cost/kWh:", 
                     &ui_intelligenceBatteryCostLabel, &ui_intelligenceBatteryCostInput, "2.0");
    
    // Min SOC input
    create_input_row(ui_intelligenceLeftColumn, "Min SOC %:", 
                     &ui_intelligenceMinSocLabel, &ui_intelligenceMinSocInput, "10");
    
    // Max SOC input
    create_input_row(ui_intelligenceLeftColumn, "Max SOC %:", 
                     &ui_intelligenceMaxSocLabel, &ui_intelligenceMaxSocInput, "95");
    
    // Battery capacity input
    create_input_row(ui_intelligenceLeftColumn, "Capacity kWh:", 
                     &ui_intelligenceBatteryCapLabel, &ui_intelligenceBatteryCapInput, "10");
    
    // Max charge power input
    create_input_row(ui_intelligenceLeftColumn, "Max charge kW:", 
                     &ui_intelligenceMaxChargeLabel, &ui_intelligenceMaxChargeInput, "5");
    
    // Max discharge power input
    create_input_row(ui_intelligenceLeftColumn, "Max disch. kW:", 
                     &ui_intelligenceMaxDischargeLabel, &ui_intelligenceMaxDischargeInput, "5");
    
    // ===== MIDDLE COLUMN - Buy coefficients =====
    ui_intelligenceMiddleColumn = lv_obj_create(ui_intelligenceMainContainer);
    lv_obj_remove_style_all(ui_intelligenceMiddleColumn);
    lv_obj_set_height(ui_intelligenceMiddleColumn, lv_pct(100));
    lv_obj_set_flex_grow(ui_intelligenceMiddleColumn, 1);
    lv_obj_set_flex_flow(ui_intelligenceMiddleColumn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui_intelligenceMiddleColumn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(ui_intelligenceMiddleColumn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_row(ui_intelligenceMiddleColumn, 8, LV_PART_MAIN);
    
    // Section title
    ui_intelligenceBuyLabel = lv_label_create(ui_intelligenceMiddleColumn);
    lv_label_set_text(ui_intelligenceBuyLabel, "Buy: price = K*spot + Q");
    lv_obj_set_style_text_font(ui_intelligenceBuyLabel, &lv_font_montserrat_16, 0);
    
    // Buy K input
    create_input_row(ui_intelligenceMiddleColumn, "K (multiplier):", 
                     &ui_intelligenceBuyKLabel, &ui_intelligenceBuyKInput, "1.21");
    
    // Buy Q input
    create_input_row(ui_intelligenceMiddleColumn, "Q (fixed cost):", 
                     &ui_intelligenceBuyQLabel, &ui_intelligenceBuyQInput, "2.5");
    
    // Info label
    lv_obj_t* buyInfoLabel = lv_label_create(ui_intelligenceMiddleColumn);
    lv_label_set_text(buyInfoLabel, "Example: 1.21 for 21% VAT\nQ = distribution fees per kWh");
    lv_obj_set_style_text_color(buyInfoLabel, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_text_font(buyInfoLabel, &lv_font_montserrat_12, 0);
    
    // ===== RIGHT COLUMN - Sell coefficients =====
    ui_intelligenceRightColumn = lv_obj_create(ui_intelligenceMainContainer);
    lv_obj_remove_style_all(ui_intelligenceRightColumn);
    lv_obj_set_height(ui_intelligenceRightColumn, lv_pct(100));
    lv_obj_set_flex_grow(ui_intelligenceRightColumn, 1);
    lv_obj_set_flex_flow(ui_intelligenceRightColumn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui_intelligenceRightColumn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(ui_intelligenceRightColumn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_row(ui_intelligenceRightColumn, 8, LV_PART_MAIN);
    
    // Section title
    ui_intelligenceSellLabel = lv_label_create(ui_intelligenceRightColumn);
    lv_label_set_text(ui_intelligenceSellLabel, "Sell: price = K*spot + Q");
    lv_obj_set_style_text_font(ui_intelligenceSellLabel, &lv_font_montserrat_16, 0);
    
    // Sell K input
    create_input_row(ui_intelligenceRightColumn, "K (multiplier):", 
                     &ui_intelligenceSellKLabel, &ui_intelligenceSellKInput, "0.9");
    
    // Sell Q input
    create_input_row(ui_intelligenceRightColumn, "Q (fixed cost):", 
                     &ui_intelligenceSellQLabel, &ui_intelligenceSellQInput, "0.0");
    
    // Info label
    lv_obj_t* sellInfoLabel = lv_label_create(ui_intelligenceRightColumn);
    lv_label_set_text(sellInfoLabel, "Usually K < 1 (lower buyback)\nQ can be negative (fee)");
    lv_obj_set_style_text_color(sellInfoLabel, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_style_text_font(sellInfoLabel, &lv_font_montserrat_12, 0);
    
    // ===== BUTTON CONTAINER =====
    ui_intelligenceButtonContainer = lv_obj_create(ui_IntelligenceSetup);
    lv_obj_remove_style_all(ui_intelligenceButtonContainer);
    lv_obj_set_width(ui_intelligenceButtonContainer, lv_pct(100));
    lv_obj_set_height(ui_intelligenceButtonContainer, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ui_intelligenceButtonContainer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui_intelligenceButtonContainer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(ui_intelligenceButtonContainer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_column(ui_intelligenceButtonContainer, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_top(ui_intelligenceButtonContainer, 8, LV_PART_MAIN);
    
    // Cancel button
    ui_intelligenceCancelButton = lv_btn_create(ui_intelligenceButtonContainer);
    lv_obj_set_width(ui_intelligenceCancelButton, 150);
    lv_obj_set_height(ui_intelligenceCancelButton, 50);
    lv_obj_add_flag(ui_intelligenceCancelButton, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(ui_intelligenceCancelButton, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_intelligenceCancelButton, lv_palette_main(LV_PALETTE_GREY), LV_PART_MAIN);
    
    ui_intelligenceCancelLabel = lv_label_create(ui_intelligenceCancelButton);
    lv_obj_set_align(ui_intelligenceCancelLabel, LV_ALIGN_CENTER);
    lv_label_set_text(ui_intelligenceCancelLabel, "Cancel");
    
    // Save button
    ui_intelligenceSaveButton = lv_btn_create(ui_intelligenceButtonContainer);
    lv_obj_set_width(ui_intelligenceSaveButton, 150);
    lv_obj_set_height(ui_intelligenceSaveButton, 50);
    lv_obj_add_flag(ui_intelligenceSaveButton, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(ui_intelligenceSaveButton, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_intelligenceSaveButton, lv_color_hex(0xFFAA00), LV_PART_MAIN);
    
    ui_intelligenceSaveLabel = lv_label_create(ui_intelligenceSaveButton);
    lv_obj_set_align(ui_intelligenceSaveLabel, LV_ALIGN_CENTER);
    lv_label_set_text(ui_intelligenceSaveLabel, "Save");
    
    // ===== KEYBOARD =====
    ui_intelligenceKeyboard = lv_keyboard_create(ui_IntelligenceSetup);
    lv_obj_set_height(ui_intelligenceKeyboard, 200);
    lv_obj_set_width(ui_intelligenceKeyboard, lv_pct(100));
    lv_keyboard_set_mode(ui_intelligenceKeyboard, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_add_flag(ui_intelligenceKeyboard, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_FLOATING);  // Hidden and floating
    lv_obj_align(ui_intelligenceKeyboard, LV_ALIGN_BOTTOM_MID, 0, 0);  // Align to bottom
}

void ui_IntelligenceSetup_screen_destroy(void)
{
    if (ui_IntelligenceSetup) lv_obj_del(ui_IntelligenceSetup);
    
    // NULL screen variables
    ui_IntelligenceSetup = NULL;
    ui_intelligenceMainContainer = NULL;
    ui_intelligenceLeftColumn = NULL;
    ui_intelligenceEnableSwitch = NULL;
    ui_intelligenceEnableLabel = NULL;
    ui_intelligenceBatteryCostLabel = NULL;
    ui_intelligenceBatteryCostInput = NULL;
    ui_intelligenceMinSocLabel = NULL;
    ui_intelligenceMinSocInput = NULL;
    ui_intelligenceMaxSocLabel = NULL;
    ui_intelligenceMaxSocInput = NULL;
    ui_intelligenceBatteryCapLabel = NULL;
    ui_intelligenceBatteryCapInput = NULL;
    ui_intelligenceMaxChargeLabel = NULL;
    ui_intelligenceMaxChargeInput = NULL;
    ui_intelligenceMaxDischargeLabel = NULL;
    ui_intelligenceMaxDischargeInput = NULL;
    ui_intelligenceMiddleColumn = NULL;
    ui_intelligenceBuyLabel = NULL;
    ui_intelligenceBuyKLabel = NULL;
    ui_intelligenceBuyKInput = NULL;
    ui_intelligenceBuyQLabel = NULL;
    ui_intelligenceBuyQInput = NULL;
    ui_intelligenceRightColumn = NULL;
    ui_intelligenceSellLabel = NULL;
    ui_intelligenceSellKLabel = NULL;
    ui_intelligenceSellKInput = NULL;
    ui_intelligenceSellQLabel = NULL;
    ui_intelligenceSellQInput = NULL;
    ui_intelligenceButtonContainer = NULL;
    ui_intelligenceSaveButton = NULL;
    ui_intelligenceSaveLabel = NULL;
    ui_intelligenceCancelButton = NULL;
    ui_intelligenceCancelLabel = NULL;
    ui_intelligenceKeyboard = NULL;
}
