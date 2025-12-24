#pragma once

#include <Arduino.h>
#include <RemoteLogger.hpp>
#include <lvgl.h>
#include "ui/ui.h"
#include "Inverters/WiFiDiscovery.hpp"
#include "Spot/ElectricityPriceLoader.hpp"
#include "utils/Localization.hpp"
#include "utils/BacklightResolver.hpp"
static void wifiSetupCompleteHandler(lv_event_t *e);
static void wifiRollerHandler(lv_event_t *e);
static void spotRollerHandler(lv_event_t *e);
static void timezoneRollerHandler(lv_event_t *e);
static void languageRollerHandler(lv_event_t *e);
static void displayTimeoutHandler(lv_event_t *e);
static void connectionTypeHandler(lv_event_t *e);
static void onFocusHandler(lv_event_t *e);
static void onTextChangedHandler(lv_event_t *e);

// External reference to backlight resolver (must be provided by app.cpp)
extern BacklightResolver backlightResolver;

class WiFiSetupUI
{
public:
    WiFiDiscovery &dongleDiscovery;
    WiFiDiscoveryResult_t result;

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

    void show()
    {
        result = WiFiDiscoveryResult_t();

        lv_scr_load(ui_WifiSetup);

        lv_obj_add_event_cb(ui_wifiSetupCompleteButton, wifiSetupCompleteHandler, LV_EVENT_ALL, this);
        lv_obj_add_event_cb(ui_wifiDropdown, wifiRollerHandler, LV_EVENT_ALL, this);
        lv_obj_add_event_cb(ui_connectionTypeDropdown, connectionTypeHandler, LV_EVENT_ALL, this);
        lv_obj_add_event_cb(ui_spotProviderDropdown, spotRollerHandler, LV_EVENT_ALL, this);
        lv_obj_add_event_cb(ui_timeZoneDropdown, timezoneRollerHandler, LV_EVENT_ALL, this);
        lv_obj_add_event_cb(ui_languageDropdown, languageRollerHandler, LV_EVENT_ALL, this);
        lv_obj_add_event_cb(ui_displayTimeoutDropdown, displayTimeoutHandler, LV_EVENT_ALL, this);
        lv_obj_add_event_cb(ui_wifiPassword, onFocusHandler, LV_EVENT_FOCUSED, this);
        lv_obj_add_event_cb(ui_inverterIP, onFocusHandler, LV_EVENT_FOCUSED, this);
        lv_obj_add_event_cb(ui_inverterSN, onFocusHandler, LV_EVENT_FOCUSED, this);
        lv_obj_add_event_cb(ui_wifiPassword, onTextChangedHandler, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_add_event_cb(ui_inverterIP, onTextChangedHandler, LV_EVENT_VALUE_CHANGED, this);
        lv_obj_add_event_cb(ui_inverterSN, onTextChangedHandler, LV_EVENT_VALUE_CHANGED, this);

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
        lv_dropdown_set_options(ui_connectionTypeDropdown, connectionTypeOptions.c_str());
        // dongleDiscovery.preferedInverterWifiDongleIndex = lv_roller_get_selected(ui_wifiDongleRoller);
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
        lv_dropdown_set_options(ui_wifiDropdown, wifiOptions.c_str());
        lv_dropdown_set_selected(ui_wifiDropdown, 0);
        onWiFiRollerChanged();

        String lastConnectedSSID = dongleDiscovery.loadLastConnectedSSID();
        if (!lastConnectedSSID.isEmpty())
        {
            for (int i = 0; i < DONGLE_DISCOVERY_MAX_RESULTS; i++)
            {
                if (dongleDiscovery.discoveries[i].ssid == lastConnectedSSID)
                {
                    lv_dropdown_set_selected(ui_wifiDropdown, i);
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
        lv_dropdown_set_options(ui_spotProviderDropdown, spotProviders.c_str());
        lv_dropdown_set_selected(ui_spotProviderDropdown, priceLoader.getStoredElectricityPriceProvider());

        String timezones = "";
        for (int i = 0; i < TIMEZONE_COUNT; i++)
        {
            timezones += String(TIMEZONES[i].name) + "\n";
        }
        lv_dropdown_set_options(ui_timeZoneDropdown, timezones.c_str());
        String storedTimeZone = priceLoader.getStoredTimeZone();
        for (int i = 0; i < TIMEZONE_COUNT; i++)
        {
            if (storedTimeZone.equals(TIMEZONES[i].tz))
            {
                lv_dropdown_set_selected(ui_timeZoneDropdown, i);
                break;
            }
        }

        // Language dropdown
        lv_dropdown_set_options(ui_languageDropdown, Localization::getLanguageOptions().c_str());
        lv_dropdown_set_selected(ui_languageDropdown, Localization::getLanguage());

        // Display timeout dropdown - map timeout value to index
        // Options: "Never\n5 min\n15 min\n30 min\n60 min" -> indices 0,1,2,3,4
        int timeoutMinutes = backlightResolver.getDisplayOffTimeout();
        int timeoutIndex = 0;  // Default "Never"
        switch (timeoutMinutes) {
            case 5:  timeoutIndex = 1; break;
            case 15: timeoutIndex = 2; break;
            case 30: timeoutIndex = 3; break;
            case 60: timeoutIndex = 4; break;
            default: timeoutIndex = 0; break;  // Never
        }
        lv_dropdown_set_selected(ui_displayTimeoutDropdown, timeoutIndex);

        // Update localized texts
        updateLocalizedTexts();
        
        setCompleteButtonVisibility();
    }

    void updateLocalizedTexts()
    {
        // Header title - find through traversal
        lv_obj_t* header = lv_obj_get_child(ui_WifiSetup, 0);
        if (header) {
            lv_obj_t* titleLabel = lv_obj_get_child(header, 0);
            if (titleLabel) lv_label_set_text(titleLabel, TR(STR_SETUP));
        }
        
        // Connect button label
        if (ui_Label3) lv_label_set_text(ui_Label3, TR(STR_CONNECT));
        
        // Card titles (stored as first child of each card)
        if (ui_Label1) lv_label_set_text(ui_Label1, TR(STR_WIFI_NETWORK));
        if (ui_Label5) lv_label_set_text(ui_Label5, TR(STR_INVERTER));
        if (ui_Label2) lv_label_set_text(ui_Label2, TR(STR_SPOT_PRICE));
        
        // WiFi card labels: child 0=title, 1=dropdown, 2=pwdLabel, 3=input
        if (ui_Container16) {
            lv_obj_t* pwdLabel = lv_obj_get_child(ui_Container16, 2);
            if (pwdLabel) lv_label_set_text(pwdLabel, TR(STR_PASSWORD));
        }
        
        // Inverter card labels: child 0=title, 1=dropdown, 2=ipLabel, 3=input, 4=snLabel, 5=input
        if (ui_Container15) {
            lv_obj_t* ipLabel = lv_obj_get_child(ui_Container15, 2);
            if (ipLabel) lv_label_set_text(ipLabel, TR(STR_IP_ADDRESS));
            lv_obj_t* snLabel = lv_obj_get_child(ui_Container15, 4);
            if (snLabel) lv_label_set_text(snLabel, TR(STR_SERIAL_NUMBER));
        }
        
        // General card: child 0=title, 1=tzLabel, 2=tz dropdown, 3=langLabel, 4=lang dropdown, 5=timeoutLabel, 6=timeout dropdown
        if (ui_ContainerGeneral) {
            lv_obj_t* generalTitle = lv_obj_get_child(ui_ContainerGeneral, 0);
            if (generalTitle) lv_label_set_text(generalTitle, TR(STR_GENERAL));
            lv_obj_t* tzLabel = lv_obj_get_child(ui_ContainerGeneral, 1);
            if (tzLabel) lv_label_set_text(tzLabel, TR(STR_TIMEZONE));
            lv_obj_t* langLabel = lv_obj_get_child(ui_ContainerGeneral, 3);
            if (langLabel) lv_label_set_text(langLabel, TR(STR_LANGUAGE));
            lv_obj_t* timeoutLabel = lv_obj_get_child(ui_ContainerGeneral, 5);
            if (timeoutLabel) lv_label_set_text(timeoutLabel, TR(STR_DISPLAY_TIMEOUT));
        }
        
        // Update display timeout dropdown options with localized "Never"
        String timeoutOptions = String(TR(STR_NEVER)) + "\n5 min\n15 min\n30 min\n60 min";
        int currentSelection = lv_dropdown_get_selected(ui_displayTimeoutDropdown);
        lv_dropdown_set_options(ui_displayTimeoutDropdown, timeoutOptions.c_str());
        lv_dropdown_set_selected(ui_displayTimeoutDropdown, currentSelection);
    }

    void setCompleteButtonVisibility()
    {
        bool isVisible = false;

        int selectedIndex = lv_dropdown_get_selected(ui_wifiDropdown);
        if (selectedIndex >= 0 && selectedIndex < DONGLE_DISCOVERY_MAX_RESULTS)
        {
            isVisible = dongleDiscovery.isValid(dongleDiscovery.discoveries[selectedIndex]);
        }
        if (isVisible)
        {
            lv_obj_clear_flag(ui_wifiSetupCompleteButton, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_add_flag(ui_wifiSetupCompleteButton, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void onCompleteClick()
    {
        int selectedIndex = lv_dropdown_get_selected(ui_wifiDropdown);
        if (selectedIndex >= 0 && selectedIndex < DONGLE_DISCOVERY_MAX_RESULTS)
        {
            result = dongleDiscovery.discoveries[selectedIndex];

            lv_obj_remove_event_cb_with_user_data(ui_wifiSetupCompleteButton, wifiSetupCompleteHandler, this);
            lv_obj_remove_event_cb_with_user_data(ui_wifiDropdown, wifiRollerHandler, this);
            lv_obj_remove_event_cb_with_user_data(ui_spotProviderDropdown, spotRollerHandler, this);
            lv_obj_remove_event_cb_with_user_data(ui_connectionTypeDropdown, connectionTypeHandler, this);
            lv_obj_remove_event_cb_with_user_data(ui_wifiPassword, onFocusHandler, this);
            lv_obj_remove_event_cb_with_user_data(ui_inverterIP, onFocusHandler, this);
            lv_obj_remove_event_cb_with_user_data(ui_inverterSN, onFocusHandler, this);
            lv_obj_remove_event_cb_with_user_data(ui_wifiPassword, onTextChangedHandler, this);
            lv_obj_remove_event_cb_with_user_data(ui_inverterIP, onTextChangedHandler, this);
            lv_obj_remove_event_cb_with_user_data(ui_inverterSN, onTextChangedHandler, this);
        }
    }

    void onWiFiRollerChanged()
    {
        int selectedIndex = lv_dropdown_get_selected(ui_wifiDropdown);
        if (selectedIndex >= 0 && selectedIndex < DONGLE_DISCOVERY_MAX_RESULTS)
        {
            lv_textarea_set_text(ui_wifiPassword, String(dongleDiscovery.discoveries[selectedIndex].password).c_str());
            lv_textarea_set_text(ui_inverterIP, String(dongleDiscovery.discoveries[selectedIndex].inverterIP).c_str());
            lv_textarea_set_text(ui_inverterSN, String(dongleDiscovery.discoveries[selectedIndex].sn).c_str());
            lv_dropdown_set_selected(ui_connectionTypeDropdown, dongleDiscovery.discoveries[selectedIndex].type);
        }
        setCompleteButtonVisibility();
    }

    void onConnectionTypeChanged()
    {
        int connectionTypeSelectedIndex = lv_dropdown_get_selected(ui_connectionTypeDropdown);
        if (connectionTypeSelectedIndex >= 0 && connectionTypeSelectedIndex < sizeof(connectionTypes) / sizeof(ConnectionType_t))
        {
            ConnectionType_t selectedType = (ConnectionType_t)connectionTypeSelectedIndex;
            int wifiSelectedIndex = lv_dropdown_get_selected(ui_wifiDropdown);
            if (wifiSelectedIndex >= 0 && wifiSelectedIndex < DONGLE_DISCOVERY_MAX_RESULTS)
            {
                dongleDiscovery.discoveries[wifiSelectedIndex].type = (ConnectionType_t)connectionTypeSelectedIndex;
            }
        }
        setCompleteButtonVisibility();
    }

    void onLanguageChanged()
    {
        int selectedIndex = lv_dropdown_get_selected(ui_languageDropdown);
        if (selectedIndex >= 0 && selectedIndex < LANG_COUNT)
        {
            Localization::setLanguage((Language_t)selectedIndex);
            LOGD("Language changed to: %d", selectedIndex);
        }
    }

    void onFocusChanged(lv_obj_t *obj)
    {
        // Show keyboard
        lv_obj_clear_flag(ui_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(ui_keyboard, obj);
        
        if (obj == ui_wifiPassword)
        {
            lv_keyboard_set_mode(ui_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
        }
        else if (obj == ui_inverterIP)
        {
            lv_keyboard_set_mode(ui_keyboard, LV_KEYBOARD_MODE_NUMBER);
        }
        else if (obj == ui_inverterSN)
        {
            lv_keyboard_set_mode(ui_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
        }
        
        // Auto-scroll to keep focused input visible above keyboard
        lv_coord_t scrHeight = lv_obj_get_height(ui_WifiSetup);
        lv_coord_t kbHeight = lv_obj_get_height(ui_keyboard);
        lv_coord_t inputY = lv_obj_get_y(obj);
        lv_coord_t inputH = lv_obj_get_height(obj);
        
        // Get parent card's Y position
        lv_obj_t* parent = lv_obj_get_parent(obj);
        if (parent && parent != ui_WifiSetup) {
            inputY += lv_obj_get_y(parent);
            // Check for grandparent (container)
            lv_obj_t* grandparent = lv_obj_get_parent(parent);
            if (grandparent && grandparent != ui_WifiSetup) {
                inputY += lv_obj_get_y(grandparent);
            }
        }
        
        lv_coord_t visibleBottom = scrHeight - kbHeight - 20;
        
        if (inputY + inputH > visibleBottom) {
            lv_coord_t scrollAmount = (inputY + inputH) - visibleBottom + 10;
            lv_obj_scroll_by(ui_Container12, 0, -scrollAmount, LV_ANIM_ON);
        }
        
        setCompleteButtonVisibility();
    }

    void onTextChanged(lv_obj_t *obj)
    {
        if (obj == ui_wifiPassword)
        {
            // Handle text change for WiFi password
            int selectedIndex = lv_dropdown_get_selected(ui_wifiDropdown);
            if (selectedIndex >= 0 && selectedIndex < DONGLE_DISCOVERY_MAX_RESULTS)
            {
                dongleDiscovery.discoveries[selectedIndex].password = lv_textarea_get_text(ui_wifiPassword);
            }
        }
        else if (obj == ui_inverterIP)
        {
            int selectedIndex = lv_dropdown_get_selected(ui_wifiDropdown);
            if (selectedIndex >= 0 && selectedIndex < DONGLE_DISCOVERY_MAX_RESULTS)
            {
                dongleDiscovery.discoveries[selectedIndex].inverterIP = lv_textarea_get_text(ui_inverterIP);
            }
        }
        else if (obj == ui_inverterSN)
        {
            int selectedIndex = lv_dropdown_get_selected(ui_wifiDropdown);
            if (selectedIndex >= 0 && selectedIndex < DONGLE_DISCOVERY_MAX_RESULTS)
            {
                LOGD("Inverter SN changed: %s", lv_textarea_get_text(ui_inverterSN));
                dongleDiscovery.discoveries[selectedIndex].sn = lv_textarea_get_text(ui_inverterSN);
            }
        }
        setCompleteButtonVisibility();
    }

private:
};

static void wifiSetupCompleteHandler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED)
    {
        WiFiSetupUI *ui = (WiFiSetupUI *)lv_event_get_user_data(e);
        ui->onCompleteClick();
    }
}

static void wifiRollerHandler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED)
    {
        WiFiSetupUI *ui = (WiFiSetupUI *)lv_event_get_user_data(e);
        if (ui)
        {
            ui->onWiFiRollerChanged();
        }
    }
}

static void spotRollerHandler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED)
    {
        int selectedIndex = lv_dropdown_get_selected(ui_spotProviderDropdown);
        LOGD("Spot provider changed to index: %d", selectedIndex);
        ElectricityPriceLoader priceLoader;
        priceLoader.storeElectricityPriceProvider((ElectricityPriceProvider_t)selectedIndex);
    }
}

static void timezoneRollerHandler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED)
    {
        int selectedIndex = lv_dropdown_get_selected(ui_timeZoneDropdown);
        if (selectedIndex >= 0 && selectedIndex < TIMEZONE_COUNT)
        {
            LOGD("Timezone changed to index: %d", selectedIndex);
            ElectricityPriceLoader priceLoader;
            priceLoader.storeTimeZone(String(TIMEZONES[selectedIndex].tz));
        }
    }
}

static void connectionTypeHandler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED)
    {
        WiFiSetupUI *ui = (WiFiSetupUI *)lv_event_get_user_data(e);
        if (ui)
        {
            ui->onConnectionTypeChanged();
        }
    }
}

static void languageRollerHandler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED)
    {
        WiFiSetupUI *ui = (WiFiSetupUI *)lv_event_get_user_data(e);
        if (ui)
        {
            ui->onLanguageChanged();
        }
    }
}

static void displayTimeoutHandler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED)
    {
        int selectedIndex = lv_dropdown_get_selected(ui_displayTimeoutDropdown);
        // Map index to minutes: 0=Never(0), 1=5, 2=15, 3=30, 4=60
        int timeoutMinutes = 0;
        switch (selectedIndex) {
            case 1: timeoutMinutes = 5;  break;
            case 2: timeoutMinutes = 15; break;
            case 3: timeoutMinutes = 30; break;
            case 4: timeoutMinutes = 60; break;
            default: timeoutMinutes = 0; break;  // Never
        }
        log_d("Display timeout changed to: %d minutes", timeoutMinutes);
        backlightResolver.setDisplayOffTimeout(timeoutMinutes);
    }
}

static void onFocusHandler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED)
    {
        // Handle focus event if needed
        // This can be used to show/hide password or inverter IP fields based on focus
        lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
        WiFiSetupUI *ui = (WiFiSetupUI *)lv_event_get_user_data(e);
        if (ui)
        {
            ui->onFocusChanged(obj);
        }
    }
}

static void onTextChangedHandler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED)
    {
        lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(e);
        WiFiSetupUI *ui = (WiFiSetupUI *)lv_event_get_user_data(e);
        if (ui)
        {
            ui->onTextChanged(obj);
        }
    }
}