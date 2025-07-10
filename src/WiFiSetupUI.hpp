#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "ui/ui.h"
#include "Inverters/DongleDiscovery.hpp"

static void wifiSetupCompleteHandler(lv_event_t *e);
static void wifiRollerHandler(lv_event_t *e);
static void connectionTypeHandler(lv_event_t *e);

class WiFiSetupUI
{
public:
    WiFiDiscovery &dongleDiscovery;
    bool complete = false;

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
        complete = false;
        lv_scr_load(ui_WifiSetup);
        lv_obj_add_event_cb(ui_wifiSetupCompleteButton, wifiSetupCompleteHandler, LV_EVENT_ALL, this);
        lv_obj_add_event_cb(ui_wifiDongleRoller, wifiRollerHandler, LV_EVENT_ALL, this);
        lv_obj_add_event_cb(ui_connectionTypeDropdown, connectionTypeHandler, LV_EVENT_ALL, this);
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
        lv_roller_set_options(ui_wifiDongleRoller, wifiOptions.c_str(), LV_ROLLER_MODE_NORMAL);
        lv_roller_set_selected(ui_wifiDongleRoller, 0, LV_ANIM_OFF);
        lv_textarea_set_text(ui_wifiPassword, dongleDiscovery.discoveries[0].password.c_str());
    }

    void onCompleteClick()
    {
        dongleDiscovery.preferedInverterWifiDongleIndex = lv_roller_get_selected(ui_wifiDongleRoller);
        dongleDiscovery.discoveries[dongleDiscovery.preferedInverterWifiDongleIndex].password = lv_textarea_get_text(ui_wifiPassword);

        lv_obj_remove_event_cb_with_user_data(ui_wifiSetupCompleteButton, wifiSetupCompleteHandler, this);
        lv_obj_remove_event_cb_with_user_data(ui_wifiDongleRoller, wifiRollerHandler, this);

        complete = true;
    }

    void onWiFiRollerChanged()
    {
        int selectedIndex = lv_roller_get_selected(ui_wifiDongleRoller);
        if (selectedIndex >= 0 && selectedIndex < DONGLE_DISCOVERY_MAX_RESULTS)
        {
            lv_textarea_set_text(ui_wifiPassword, dongleDiscovery.discoveries[selectedIndex].password.c_str());
        }
        else
        {
            lv_textarea_set_text(ui_wifiPassword, "");
        }
    }

    void onConnectionTypeChanged()
    {
        int connectionTypeSelectedIndex = lv_dropdown_get_selected(ui_connectionTypeDropdown);
        if (connectionTypeSelectedIndex >= 0 && connectionTypeSelectedIndex < sizeof(connectionTypes) / sizeof(ConnectionType_t))
        {
            ConnectionType_t selectedType = (ConnectionType_t)connectionTypeSelectedIndex;
            int wifiSelectedIndex = lv_roller_get_selected(ui_wifiDongleRoller);
            if( wifiSelectedIndex >= 0 && wifiSelectedIndex < DONGLE_DISCOVERY_MAX_RESULTS)
            {
                dongleDiscovery.discoveries[wifiSelectedIndex].type = (ConnectionType_t) connectionTypeSelectedIndex;
            }
        }
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
        ui->onWiFiRollerChanged();
    }
}

static void connectionTypeHandler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_VALUE_CHANGED)
    {
        WiFiSetupUI *ui = (WiFiSetupUI *)lv_event_get_user_data(e);
        ui->onConnectionTypeChanged();
    }
}