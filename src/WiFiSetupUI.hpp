#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "ui/ui.h"
#include "Inverters/DongleDiscovery.hpp"

static void wifiSetupCompleteHandler(lv_event_t *e);
static void wifiRollerHandler(lv_event_t *e);

class WiFiSetupUI
{
public:
    DongleDiscovery& dongleDiscovery;
    bool complete = false;

    WiFiSetupUI(DongleDiscovery &dongleDiscovery): dongleDiscovery(dongleDiscovery)
    {
    }

    void show()
    {   
        complete = false;
        lv_scr_load_anim(ui_WifiSetup, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, false);
        lv_obj_add_event_cb(ui_wifiSetupCompleteButton, wifiSetupCompleteHandler, LV_EVENT_ALL, this);
        lv_obj_add_event_cb(ui_wifiDongleRoller, wifiRollerHandler, LV_EVENT_ALL, this);
        // dongleDiscovery.preferedInverterWifiDongleIndex = lv_roller_get_selected(ui_wifiDongleRoller);
        String options = "";
        for (int i = 0; i < DONGLE_DISCOVERY_MAX_RESULTS; i++)
        {
            if (dongleDiscovery.discoveries[i].type == DONGLE_TYPE_UNKNOWN)
            {
                break;
            }
            options += dongleDiscovery.getDongleTypeName(dongleDiscovery.discoveries[i].type);
            options += " - ";
            options += dongleDiscovery.discoveries[i].sn;
            options += " (";
            options += dongleDiscovery.discoveries[i].signalPercent;
            options += "%)";
            options += "\n";
        }
        lv_roller_set_options(ui_wifiDongleRoller, options.c_str(), LV_ROLLER_MODE_NORMAL);
    }

    void onCompleteClick()
    {
        dongleDiscovery.preferedInverterWifiDongleIndex = lv_roller_get_selected(ui_wifiDongleRoller);
        dongleDiscovery.discoveries[dongleDiscovery.preferedInverterWifiDongleIndex].password = lv_textarea_get_text(ui_wifiPassword);
        
        lv_obj_remove_event_cb_with_user_data(ui_wifiSetupCompleteButton, wifiSetupCompleteHandler, this);
        lv_obj_remove_event_cb_with_user_data(ui_wifiDongleRoller, wifiRollerHandler, this);

        complete = true;
    }

    void onRollerChanged() {
        lv_textarea_set_text(ui_wifiPassword, dongleDiscovery.discoveries[lv_roller_get_selected(ui_wifiDongleRoller)].password.c_str());
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

static void wifiRollerHandler(lv_event_t *e) {
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_VALUE_CHANGED) {
        WiFiSetupUI *ui = (WiFiSetupUI *)lv_event_get_user_data(e);
        ui->onRollerChanged();
    }
}