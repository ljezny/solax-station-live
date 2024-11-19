#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "ui/ui.h"
#include "Inverters/DongleDiscovery.hpp"

class WiFiSetupUI
{
    public:
        void show() {
            lv_scr_load_anim(ui_WifiSetup, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, false);  
        }

        void update(DongleDiscovery &dongleDiscovery) {
            //dongleDiscovery.preferedInverterWifiDongleIndex = lv_roller_get_selected(ui_wifiDongleRoller);
            String options = "";
            for(int i = 0; i < DONGLE_DISCOVERY_MAX_RESULTS; i++) {
                    options += dongleDiscovery.discoveries[i].ssid;
                    options += "\n";
            }
            lv_roller_set_options(ui_wifiDongleRoller, options.c_str(), LV_ROLLER_MODE_NORMAL);

            // if(dongleDiscovery.preferedInverterWifiDongleIndex != -1) {
            //     lv_roller_set_selected(ui_wifiDongleRoller, dongleDiscovery.preferedInverterWifiDongleIndex, LV_ANIM_OFF);
            // } else {
            //     dongleDiscovery.preferedInverterWifiDongleIndex = lv_roller_get_selected(ui_wifiDongleRoller);
            // }
        }
};