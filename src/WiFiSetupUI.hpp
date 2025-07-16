#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "ui/ui.h"
#include "Inverters/WiFiDiscovery.hpp"

static void wifiSetupCompleteHandler(lv_event_t *e);
static void wifiRollerHandler(lv_event_t *e);
static void connectionTypeHandler(lv_event_t *e);
static void onFocusHandler(lv_event_t *e);
static void onTextChangedHandler(lv_event_t *e);
class WiFiSetupUI
{
public:
    WiFiDiscovery &dongleDiscovery;
    WiFiDiscoveryResult_t result;

    const ConnectionType_t connectionTypes[6] = {
        CONNECTION_TYPE_NONE,
        CONNECTION_TYPE_SOLAX,
        CONNECTION_TYPE_GOODWE,
        CONNECTION_TYPE_SOFAR,
        CONNECTION_TYPE_VICTRON,
        CONNECTION_TYPE_DEYE,
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

        setCompleteButtonVisibility();
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

    void onFocusChanged(lv_obj_t *obj)
    {
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
                log_d("Inverter SN changed: %s", lv_textarea_get_text(ui_inverterSN));
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

static void onFocusHandler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED)
    {
        // Handle focus event if needed
        // This can be used to show/hide password or inverter IP fields based on focus
        lv_obj_t *obj = lv_event_get_target(e);
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
        lv_obj_t *obj = lv_event_get_target(e);
        WiFiSetupUI *ui = (WiFiSetupUI *)lv_event_get_user_data(e);
        if (ui)
        {
            ui->onTextChanged(obj);
        }
    }
}