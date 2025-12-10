// WiFi Setup Screen - Redesigned
// LVGL version: 8.3.11
// Project name: SolaxStationLive_Project

#include "ui.h"

// Color scheme matching dashboard
#define COLOR_BG_DARK       lv_color_hex(0x1A1A2E)
#define COLOR_CARD_BG       lv_color_hex(0x16213E)
#define COLOR_CARD_BORDER   lv_color_hex(0x0F3460)
#define COLOR_ACCENT        lv_color_hex(0xFFA726)  // Orange
#define COLOR_GREEN         lv_color_hex(0x4CAF50)
#define COLOR_BLUE          lv_color_hex(0x3282B8)
#define COLOR_PURPLE        lv_color_hex(0x9B59B6)
#define COLOR_TEXT          lv_color_hex(0xEEEEEE)
#define COLOR_TEXT_DIM      lv_color_hex(0x888888)
#define COLOR_INPUT_BG      lv_color_hex(0x0F3460)
#define COLOR_INPUT_BORDER  lv_color_hex(0x3282B8)

lv_obj_t *ui_WifiSetup = NULL;
lv_obj_t *ui_Container12 = NULL;
lv_obj_t *ui_Container16 = NULL;
lv_obj_t *ui_Label1 = NULL;
lv_obj_t *ui_wifiDropdown = NULL;
lv_obj_t *ui_wifiPassword = NULL;
lv_obj_t *ui_wifiSetupCompleteButton = NULL;
lv_obj_t *ui_Label3 = NULL;
lv_obj_t *ui_Container15 = NULL;
lv_obj_t *ui_Label5 = NULL;
lv_obj_t *ui_connectionTypeDropdown = NULL;
lv_obj_t *ui_inverterIP = NULL;
lv_obj_t *ui_inverterSN = NULL;
lv_obj_t *ui_Container21 = NULL;
lv_obj_t *ui_Label2 = NULL;
lv_obj_t *ui_spotProviderDropdown = NULL;
lv_obj_t *ui_timeZoneDropdown = NULL;
lv_obj_t *ui_languageDropdown = NULL;
lv_obj_t *ui_keyboard = NULL;

// Helper function to style a dropdown
static void style_dropdown(lv_obj_t* dropdown) {
    lv_obj_set_style_bg_color(dropdown, COLOR_INPUT_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dropdown, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(dropdown, COLOR_INPUT_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(dropdown, 1, LV_PART_MAIN);
    lv_obj_set_style_border_opa(dropdown, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_radius(dropdown, 6, LV_PART_MAIN);
    lv_obj_set_style_text_color(dropdown, COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(dropdown, &ui_font_OpenSansExtraSmall, LV_PART_MAIN);
    lv_obj_set_style_pad_all(dropdown, 10, LV_PART_MAIN);
    // Disable default symbol (would show square because our font doesn't have it)
    lv_dropdown_set_symbol(dropdown, NULL);
}

// Helper function to style a textarea input
static void style_textarea(lv_obj_t* input) {
    lv_obj_set_style_bg_color(input, COLOR_INPUT_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(input, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(input, COLOR_INPUT_BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_width(input, 1, LV_PART_MAIN);
    lv_obj_set_style_border_opa(input, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_radius(input, 6, LV_PART_MAIN);
    lv_obj_set_style_text_color(input, COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(input, &ui_font_OpenSansExtraSmall, LV_PART_MAIN);
    lv_obj_set_style_pad_all(input, 10, LV_PART_MAIN);
    // Cursor style
    lv_obj_set_style_bg_color(input, COLOR_ACCENT, LV_PART_CURSOR);
    // Placeholder style
    lv_obj_set_style_text_color(input, COLOR_TEXT_DIM, LV_PART_TEXTAREA_PLACEHOLDER);
}

// Helper function to create a styled card
static lv_obj_t* create_card(lv_obj_t* parent, const char* iconAndTitle, lv_color_t accentColor) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    
    // Card styling
    lv_obj_set_style_bg_color(card, COLOR_CARD_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, accentColor, LV_PART_MAIN);
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
    lv_obj_set_style_text_color(titleLabel, accentColor, 0);
    lv_obj_set_style_pad_bottom(titleLabel, 4, 0);
    
    return card;
}

void ui_WifiSetup_screen_init(void)
{
    // Main screen with dark background (matching dark mode dashboard)
    ui_WifiSetup = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_WifiSetup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(ui_WifiSetup, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui_WifiSetup, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    // Dark solid background (same as dark mode dashboard)
    lv_obj_set_style_bg_color(ui_WifiSetup, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ui_WifiSetup, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ui_WifiSetup, 12, LV_PART_MAIN);
    
    // Header with title
    lv_obj_t* header = lv_obj_create(ui_WifiSetup);
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
    lv_obj_set_style_text_color(title, COLOR_ACCENT, 0);
    
    // OK button in header
    ui_wifiSetupCompleteButton = lv_btn_create(header);
    lv_obj_set_width(ui_wifiSetupCompleteButton, 120);
    lv_obj_set_height(ui_wifiSetupCompleteButton, 40);
    lv_obj_add_flag(ui_wifiSetupCompleteButton, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    lv_obj_clear_flag(ui_wifiSetupCompleteButton, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(ui_wifiSetupCompleteButton, COLOR_GREEN, LV_PART_MAIN);
    lv_obj_set_style_radius(ui_wifiSetupCompleteButton, 8, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(ui_wifiSetupCompleteButton, 10, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(ui_wifiSetupCompleteButton, COLOR_GREEN, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(ui_wifiSetupCompleteButton, LV_OPA_30, LV_PART_MAIN);
    
    ui_Label3 = lv_label_create(ui_wifiSetupCompleteButton);
    lv_obj_set_align(ui_Label3, LV_ALIGN_CENTER);
    lv_label_set_text(ui_Label3, "Connect");
    lv_obj_set_style_text_color(ui_Label3, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ui_Label3, &ui_font_OpenSansSmall, 0);
    
    // Main container with 3 columns
    ui_Container12 = lv_obj_create(ui_WifiSetup);
    lv_obj_remove_style_all(ui_Container12);
    lv_obj_set_width(ui_Container12, lv_pct(100));
    lv_obj_set_flex_grow(ui_Container12, 1);
    lv_obj_set_flex_flow(ui_Container12, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui_Container12, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(ui_Container12, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_column(ui_Container12, 12, LV_PART_MAIN);
    
    // ===== LEFT COLUMN - WiFi (Blue accent) =====
    ui_Container16 = create_card(ui_Container12, "WiFi Network", COLOR_BLUE);
    
    // Keep reference for the card's title label
    ui_Label1 = lv_obj_get_child(ui_Container16, 0);
    
    // WiFi dropdown
    ui_wifiDropdown = lv_dropdown_create(ui_Container16);
    lv_dropdown_set_options(ui_wifiDropdown, "Scanning...");
    lv_obj_set_width(ui_wifiDropdown, lv_pct(100));
    lv_obj_set_height(ui_wifiDropdown, LV_SIZE_CONTENT);
    lv_obj_add_flag(ui_wifiDropdown, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    style_dropdown(ui_wifiDropdown);
    
    // Password label
    lv_obj_t* pwdLabel = lv_label_create(ui_Container16);
    lv_label_set_text(pwdLabel, "Password:");
    lv_obj_set_style_text_color(pwdLabel, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(pwdLabel, &ui_font_OpenSansExtraSmall, 0);
    
    // WiFi password input
    ui_wifiPassword = lv_textarea_create(ui_Container16);
    lv_obj_set_width(ui_wifiPassword, lv_pct(100));
    lv_obj_set_height(ui_wifiPassword, LV_SIZE_CONTENT);
    lv_textarea_set_max_length(ui_wifiPassword, 32);
    lv_textarea_set_placeholder_text(ui_wifiPassword, "WiFi password");
    lv_textarea_set_one_line(ui_wifiPassword, true);
    lv_textarea_set_password_mode(ui_wifiPassword, true);
    style_textarea(ui_wifiPassword);
    
    // ===== MIDDLE COLUMN - Inverter (Orange accent) =====
    ui_Container15 = create_card(ui_Container12, "Inverter", COLOR_ACCENT);
    
    // Keep reference
    ui_Label5 = lv_obj_get_child(ui_Container15, 0);
    
    // Connection type dropdown
    ui_connectionTypeDropdown = lv_dropdown_create(ui_Container15);
    lv_dropdown_set_options(ui_connectionTypeDropdown, "SOLAX\nGoodWe\nSofar Solar\nVictron\nDEYE\nGrowatt");
    lv_obj_set_width(ui_connectionTypeDropdown, lv_pct(100));
    lv_obj_set_height(ui_connectionTypeDropdown, LV_SIZE_CONTENT);
    lv_obj_add_flag(ui_connectionTypeDropdown, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    style_dropdown(ui_connectionTypeDropdown);
    
    // IP label
    lv_obj_t* ipLabel = lv_label_create(ui_Container15);
    lv_label_set_text(ipLabel, "IP Address:");
    lv_obj_set_style_text_color(ipLabel, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(ipLabel, &ui_font_OpenSansExtraSmall, 0);
    
    // Inverter IP input
    ui_inverterIP = lv_textarea_create(ui_Container15);
    lv_obj_set_width(ui_inverterIP, lv_pct(100));
    lv_obj_set_height(ui_inverterIP, LV_SIZE_CONTENT);
    lv_textarea_set_max_length(ui_inverterIP, 16);
    lv_textarea_set_placeholder_text(ui_inverterIP, "192.168.x.x");
    lv_textarea_set_one_line(ui_inverterIP, true);
    style_textarea(ui_inverterIP);
    
    // SN label
    lv_obj_t* snLabel = lv_label_create(ui_Container15);
    lv_label_set_text(snLabel, "Serial Number:");
    lv_obj_set_style_text_color(snLabel, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(snLabel, &ui_font_OpenSansExtraSmall, 0);
    
    // Inverter SN input
    ui_inverterSN = lv_textarea_create(ui_Container15);
    lv_obj_set_width(ui_inverterSN, lv_pct(100));
    lv_obj_set_height(ui_inverterSN, LV_SIZE_CONTENT);
    lv_textarea_set_max_length(ui_inverterSN, 16);
    lv_textarea_set_placeholder_text(ui_inverterSN, "SN1234567890");
    lv_textarea_set_one_line(ui_inverterSN, true);
    style_textarea(ui_inverterSN);
    
    // ===== RIGHT COLUMN - Spot Price (Purple accent) =====
    ui_Container21 = create_card(ui_Container12, "Spot Price", COLOR_PURPLE);
    
    // Keep reference
    ui_Label2 = lv_obj_get_child(ui_Container21, 0);
    
    // Provider label
    lv_obj_t* providerLabel = lv_label_create(ui_Container21);
    lv_label_set_text(providerLabel, "Price Provider:");
    lv_obj_set_style_text_color(providerLabel, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(providerLabel, &ui_font_OpenSansExtraSmall, 0);
    
    // Spot provider dropdown
    ui_spotProviderDropdown = lv_dropdown_create(ui_Container21);
    lv_dropdown_set_options(ui_spotProviderDropdown, "None\nOTE CZ\nOTSK");
    lv_obj_set_width(ui_spotProviderDropdown, lv_pct(100));
    lv_obj_set_height(ui_spotProviderDropdown, LV_SIZE_CONTENT);
    lv_obj_add_flag(ui_spotProviderDropdown, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    style_dropdown(ui_spotProviderDropdown);
    
    // Timezone label
    lv_obj_t* tzLabel = lv_label_create(ui_Container21);
    lv_label_set_text(tzLabel, "Time Zone:");
    lv_obj_set_style_text_color(tzLabel, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(tzLabel, &ui_font_OpenSansExtraSmall, 0);
    
    // Timezone dropdown
    ui_timeZoneDropdown = lv_dropdown_create(ui_Container21);
    lv_dropdown_set_options(ui_timeZoneDropdown, "Europe/Prague");
    lv_obj_set_width(ui_timeZoneDropdown, lv_pct(100));
    lv_obj_set_height(ui_timeZoneDropdown, LV_SIZE_CONTENT);
    lv_obj_add_flag(ui_timeZoneDropdown, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    style_dropdown(ui_timeZoneDropdown);
    
    // Language label
    lv_obj_t* langLabel = lv_label_create(ui_Container21);
    lv_label_set_text(langLabel, "Language:");
    lv_obj_set_style_text_color(langLabel, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(langLabel, &ui_font_OpenSansExtraSmall, 0);
    
    // Language dropdown
    ui_languageDropdown = lv_dropdown_create(ui_Container21);
    lv_dropdown_set_options(ui_languageDropdown, "English\nDeutsch\nČeština");
    lv_obj_set_width(ui_languageDropdown, lv_pct(100));
    lv_obj_set_height(ui_languageDropdown, LV_SIZE_CONTENT);
    lv_obj_add_flag(ui_languageDropdown, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    style_dropdown(ui_languageDropdown);
    
    // Info text
    lv_obj_t* infoLabel = lv_label_create(ui_Container21);
    lv_label_set_text(infoLabel, "Enable spot prices for\nintelligent battery control");
    lv_obj_set_style_text_color(infoLabel, COLOR_TEXT_DIM, 0);
    lv_obj_set_style_text_font(infoLabel, &ui_font_OpenSansExtraSmall, 0);
    
    // ===== KEYBOARD =====
    ui_keyboard = lv_keyboard_create(ui_WifiSetup);
    lv_obj_set_height(ui_keyboard, 200);
    lv_obj_set_width(ui_keyboard, lv_pct(100));
    lv_keyboard_set_textarea(ui_keyboard, NULL);  // No textarea initially
    // Style keyboard
    lv_obj_set_style_bg_color(ui_keyboard, COLOR_CARD_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui_keyboard, COLOR_INPUT_BG, LV_PART_ITEMS);
    lv_obj_set_style_text_color(ui_keyboard, COLOR_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_border_width(ui_keyboard, 0, LV_PART_MAIN);
    // Floating keyboard - overlay on content
    lv_obj_add_flag(ui_keyboard, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_align(ui_keyboard, LV_ALIGN_BOTTOM_MID);
    // Hide keyboard initially
    lv_obj_add_flag(ui_keyboard, LV_OBJ_FLAG_HIDDEN);
}

void ui_WifiSetup_screen_destroy(void)
{
    if (ui_WifiSetup) lv_obj_del(ui_WifiSetup);

    // NULL screen variables
    ui_WifiSetup = NULL;
    ui_Container12 = NULL;
    ui_Container16 = NULL;
    ui_Label1 = NULL;
    ui_wifiDropdown = NULL;
    ui_wifiPassword = NULL;
    ui_wifiSetupCompleteButton = NULL;
    ui_Label3 = NULL;
    ui_Container15 = NULL;
    ui_Label5 = NULL;
    ui_connectionTypeDropdown = NULL;
    ui_inverterIP = NULL;
    ui_inverterSN = NULL;
    ui_Container21 = NULL;
    ui_Label2 = NULL;
    ui_spotProviderDropdown = NULL;
    ui_timeZoneDropdown = NULL;
    ui_languageDropdown = NULL;
    ui_keyboard = NULL;
}
