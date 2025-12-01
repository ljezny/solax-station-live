// Intelligence Setup Screen
// LVGL version: 8.3.11
// Project name: SolaxStationLive_Project

#include "ui.h"

// Color scheme matching dashboard
#define COLOR_BG_DARK       lv_color_hex(0x1A1A2E)
#define COLOR_CARD_BG       lv_color_hex(0x16213E)
#define COLOR_CARD_BORDER   lv_color_hex(0x0F3460)
#define COLOR_ACCENT        lv_color_hex(0xFFA726)  // Orange
#define COLOR_GREEN         lv_color_hex(0x4CAF50)
#define COLOR_RED           lv_color_hex(0xE63946)
#define COLOR_TEXT          lv_color_hex(0xEEEEEE)
#define COLOR_TEXT_DIM      lv_color_hex(0x888888)
#define COLOR_INPUT_BG      lv_color_hex(0x0F3460)
#define COLOR_INPUT_BORDER  lv_color_hex(0x3282B8)

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
lv_obj_t *ui_intelligenceResetButton = NULL;
lv_obj_t *ui_intelligenceResetLabel = NULL;
lv_obj_t *ui_intelligenceSaveButton = NULL;
lv_obj_t *ui_intelligenceSaveLabel = NULL;
lv_obj_t *ui_intelligenceCancelButton = NULL;
lv_obj_t *ui_intelligenceCancelLabel = NULL;

// Keyboard
lv_obj_t *ui_intelligenceKeyboard = NULL;

// Helper function to style a textarea input
static void style_input(lv_obj_t* input) {
    lv_obj_set_style_bg_color(input, COLOR_INPUT_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(input, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(input, COLOR_INPUT_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(input, 1, LV_PART_MAIN);
    lv_obj_set_style_border_opa(input, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_radius(input, 6, LV_PART_MAIN);
    lv_obj_set_style_text_color(input, COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_pad_all(input, 8, LV_PART_MAIN);
    // Cursor style
    lv_obj_set_style_bg_color(input, COLOR_ACCENT, LV_PART_CURSOR);
}

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
    lv_obj_set_width(*labelOut, lv_pct(55));
    lv_label_set_text(*labelOut, labelText);
    lv_obj_set_style_text_color(*labelOut, COLOR_TEXT, 0);
    
    *inputOut = lv_textarea_create(container);
    lv_obj_set_width(*inputOut, lv_pct(42));
    lv_obj_set_height(*inputOut, 36);
    lv_textarea_set_max_length(*inputOut, 10);
    lv_textarea_set_placeholder_text(*inputOut, placeholder);
    lv_textarea_set_one_line(*inputOut, true);
    lv_obj_add_flag(*inputOut, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    style_input(*inputOut);
    
    return container;
}

// Helper function to create a styled card/column
static lv_obj_t* create_card(lv_obj_t* parent, const char* title, lv_color_t accentColor) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    
    // Card styling
    lv_obj_set_style_bg_color(card, COLOR_CARD_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, COLOR_CARD_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(card, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, accentColor, LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 3, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_row(card, 10, LV_PART_MAIN);
    
    // Shadow
    lv_obj_set_style_shadow_width(card, 15, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(card, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_shadow_ofs_y(card, 4, LV_PART_MAIN);
    
    // Title
    lv_obj_t* titleLabel = lv_label_create(card);
    lv_label_set_text(titleLabel, title);
    lv_obj_set_style_text_font(titleLabel, &ui_font_OpenSansSmall, 0);
    lv_obj_set_style_text_color(titleLabel, accentColor, 0);
    lv_obj_set_style_pad_bottom(titleLabel, 4, 0);
    
    return card;
}

void ui_IntelligenceSetup_screen_init(void)
{
    // Main screen with dark background (matching dark mode dashboard)
    ui_IntelligenceSetup = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_IntelligenceSetup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(ui_IntelligenceSetup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui_IntelligenceSetup, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    // Dark solid background (same as dark mode dashboard)
    lv_obj_set_style_bg_color(ui_IntelligenceSetup, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui_IntelligenceSetup, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ui_IntelligenceSetup, 12, LV_PART_MAIN);
    
    // Header with title and enable switch
    lv_obj_t* header = lv_obj_create(ui_IntelligenceSetup);
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
    lv_obj_set_style_text_color(title, COLOR_ACCENT, 0);
    
    // Enable switch container
    lv_obj_t* enableContainer = lv_obj_create(header);
    lv_obj_remove_style_all(enableContainer);
    lv_obj_set_size(enableContainer, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(enableContainer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(enableContainer, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(enableContainer, 10, 0);
    lv_obj_clear_flag(enableContainer, LV_OBJ_FLAG_SCROLLABLE);
    
    ui_intelligenceEnableLabel = lv_label_create(enableContainer);
    lv_label_set_text(ui_intelligenceEnableLabel, "Enable");
    lv_obj_set_style_text_color(ui_intelligenceEnableLabel, COLOR_TEXT, 0);
    
    ui_intelligenceEnableSwitch = lv_switch_create(enableContainer);
    lv_obj_set_width(ui_intelligenceEnableSwitch, 50);
    lv_obj_set_style_bg_color(ui_intelligenceEnableSwitch, COLOR_INPUT_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui_intelligenceEnableSwitch, COLOR_GREEN, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(ui_intelligenceEnableSwitch, COLOR_GREEN, LV_PART_KNOB | LV_STATE_CHECKED);
    
    // Main container with 3 columns
    ui_intelligenceMainContainer = lv_obj_create(ui_IntelligenceSetup);
    lv_obj_remove_style_all(ui_intelligenceMainContainer);
    lv_obj_set_width(ui_intelligenceMainContainer, lv_pct(100));
    lv_obj_set_flex_grow(ui_intelligenceMainContainer, 1);
    lv_obj_set_flex_flow(ui_intelligenceMainContainer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui_intelligenceMainContainer, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(ui_intelligenceMainContainer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(ui_intelligenceMainContainer, LV_DIR_VER);
    lv_obj_set_style_pad_column(ui_intelligenceMainContainer, 12, LV_PART_MAIN);
    
    // ===== LEFT COLUMN - Battery Settings (Green accent) =====
    ui_intelligenceLeftColumn = create_card(ui_intelligenceMainContainer, "Battery", COLOR_GREEN);
    
    // Battery cost input
    create_input_row(ui_intelligenceLeftColumn, "Cost per kWh:", 
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
    
    // ===== MIDDLE COLUMN - Buy coefficients (Orange accent) =====
    ui_intelligenceMiddleColumn = create_card(ui_intelligenceMainContainer, "Buy Price", COLOR_ACCENT);
    
    // Formula label
    lv_obj_t* buyFormulaLabel = lv_label_create(ui_intelligenceMiddleColumn);
    lv_label_set_text(buyFormulaLabel, "price = K * spot + Q");
    lv_obj_set_style_text_color(buyFormulaLabel, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(buyFormulaLabel, &ui_font_OpenSansExtraSmall, 0);
    
    // Buy K input
    create_input_row(ui_intelligenceMiddleColumn, "K (multiplier):", 
                     &ui_intelligenceBuyKLabel, &ui_intelligenceBuyKInput, "1.21");
    
    // Buy Q input
    create_input_row(ui_intelligenceMiddleColumn, "Q (fixed):", 
                     &ui_intelligenceBuyQLabel, &ui_intelligenceBuyQInput, "2.5");
    
    // Info label
    lv_obj_t* buyInfoLabel = lv_label_create(ui_intelligenceMiddleColumn);
    lv_label_set_text(buyInfoLabel, "K=1.21 for 21% VAT\nQ = distribution fees");
    lv_obj_set_style_text_color(buyInfoLabel, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(buyInfoLabel, &ui_font_OpenSansExtraSmall, 0);
    
    // ===== RIGHT COLUMN - Sell coefficients (Red accent) =====
    ui_intelligenceRightColumn = create_card(ui_intelligenceMainContainer, "Sell Price", COLOR_RED);
    
    // Formula label
    lv_obj_t* sellFormulaLabel = lv_label_create(ui_intelligenceRightColumn);
    lv_label_set_text(sellFormulaLabel, "price = K * spot + Q");
    lv_obj_set_style_text_color(sellFormulaLabel, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(sellFormulaLabel, &ui_font_OpenSansExtraSmall, 0);
    
    // Sell K input
    create_input_row(ui_intelligenceRightColumn, "K (multiplier):", 
                     &ui_intelligenceSellKLabel, &ui_intelligenceSellKInput, "0.9");
    
    // Sell Q input
    create_input_row(ui_intelligenceRightColumn, "Q (fixed):", 
                     &ui_intelligenceSellQLabel, &ui_intelligenceSellQInput, "0.0");
    
    // Info label
    lv_obj_t* sellInfoLabel = lv_label_create(ui_intelligenceRightColumn);
    lv_label_set_text(sellInfoLabel, "Usually K<1 (lower buyback)\nQ can be negative");
    lv_obj_set_style_text_color(sellInfoLabel, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(sellInfoLabel, &ui_font_OpenSansExtraSmall, 0);
    
    // ===== BUTTON CONTAINER =====
    ui_intelligenceButtonContainer = lv_obj_create(ui_IntelligenceSetup);
    lv_obj_remove_style_all(ui_intelligenceButtonContainer);
    lv_obj_set_width(ui_intelligenceButtonContainer, lv_pct(100));
    lv_obj_set_height(ui_intelligenceButtonContainer, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ui_intelligenceButtonContainer, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui_intelligenceButtonContainer, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(ui_intelligenceButtonContainer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_column(ui_intelligenceButtonContainer, 20, LV_PART_MAIN);
    lv_obj_set_style_pad_top(ui_intelligenceButtonContainer, 10, LV_PART_MAIN);
    
    // Reset button
    ui_intelligenceResetButton = lv_btn_create(ui_intelligenceButtonContainer);
    lv_obj_set_width(ui_intelligenceResetButton, 140);
    lv_obj_set_height(ui_intelligenceResetButton, 45);
    lv_obj_add_flag(ui_intelligenceResetButton, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(ui_intelligenceResetButton, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_intelligenceResetButton, COLOR_CARD_BG, LV_PART_MAIN);
    lv_obj_set_style_border_color(ui_intelligenceResetButton, COLOR_ACCENT, LV_PART_MAIN);
    lv_obj_set_style_border_width(ui_intelligenceResetButton, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(ui_intelligenceResetButton, 8, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(ui_intelligenceResetButton, 0, LV_PART_MAIN);
    
    ui_intelligenceResetLabel = lv_label_create(ui_intelligenceResetButton);
    lv_obj_set_align(ui_intelligenceResetLabel, LV_ALIGN_CENTER);
    lv_label_set_text(ui_intelligenceResetLabel, "Reset");
    lv_obj_set_style_text_color(ui_intelligenceResetLabel, COLOR_ACCENT, 0);
    
    // Cancel button
    ui_intelligenceCancelButton = lv_btn_create(ui_intelligenceButtonContainer);
    lv_obj_set_width(ui_intelligenceCancelButton, 140);
    lv_obj_set_height(ui_intelligenceCancelButton, 45);
    lv_obj_add_flag(ui_intelligenceCancelButton, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(ui_intelligenceCancelButton, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_intelligenceCancelButton, COLOR_CARD_BG, LV_PART_MAIN);
    lv_obj_set_style_border_color(ui_intelligenceCancelButton, COLOR_TEXT_DIM, LV_PART_MAIN);
    lv_obj_set_style_border_width(ui_intelligenceCancelButton, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(ui_intelligenceCancelButton, 8, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(ui_intelligenceCancelButton, 0, LV_PART_MAIN);
    
    ui_intelligenceCancelLabel = lv_label_create(ui_intelligenceCancelButton);
    lv_obj_set_align(ui_intelligenceCancelLabel, LV_ALIGN_CENTER);
    lv_label_set_text(ui_intelligenceCancelLabel, "Cancel");
    lv_obj_set_style_text_color(ui_intelligenceCancelLabel, COLOR_TEXT, 0);
    
    // Save button
    ui_intelligenceSaveButton = lv_btn_create(ui_intelligenceButtonContainer);
    lv_obj_set_width(ui_intelligenceSaveButton, 140);
    lv_obj_set_height(ui_intelligenceSaveButton, 45);
    lv_obj_add_flag(ui_intelligenceSaveButton, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(ui_intelligenceSaveButton, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_intelligenceSaveButton, COLOR_GREEN, LV_PART_MAIN);
    lv_obj_set_style_radius(ui_intelligenceSaveButton, 8, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(ui_intelligenceSaveButton, 10, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(ui_intelligenceSaveButton, COLOR_GREEN, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(ui_intelligenceSaveButton, LV_OPA_30, LV_PART_MAIN);
    
    ui_intelligenceSaveLabel = lv_label_create(ui_intelligenceSaveButton);
    lv_obj_set_align(ui_intelligenceSaveLabel, LV_ALIGN_CENTER);
    lv_label_set_text(ui_intelligenceSaveLabel, "Save");
    lv_obj_set_style_text_color(ui_intelligenceSaveLabel, lv_color_hex(0xFFFFFF), 0);
    
    // ===== KEYBOARD =====
    ui_intelligenceKeyboard = lv_keyboard_create(ui_IntelligenceSetup);
    lv_obj_set_height(ui_intelligenceKeyboard, 200);
    lv_obj_set_width(ui_intelligenceKeyboard, lv_pct(100));
    lv_keyboard_set_mode(ui_intelligenceKeyboard, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_add_flag(ui_intelligenceKeyboard, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_FLOATING);
    lv_obj_align(ui_intelligenceKeyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    // Style keyboard
    lv_obj_set_style_bg_color(ui_intelligenceKeyboard, COLOR_CARD_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui_intelligenceKeyboard, COLOR_INPUT_BG, LV_PART_ITEMS);
    lv_obj_set_style_text_color(ui_intelligenceKeyboard, COLOR_TEXT, LV_PART_ITEMS);
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
    ui_intelligenceResetButton = NULL;
    ui_intelligenceResetLabel = NULL;
    ui_intelligenceSaveButton = NULL;
    ui_intelligenceSaveLabel = NULL;
    ui_intelligenceCancelButton = NULL;
    ui_intelligenceCancelLabel = NULL;
    ui_intelligenceKeyboard = NULL;
}
