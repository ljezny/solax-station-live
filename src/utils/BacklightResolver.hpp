#pragma once

#include <ESP_Panel_Library.h>
#include "Solax/SolaxDongleAPI.hpp"
class BacklightResolver {
    private:
        ESP_PanelBacklight *backlight;

    public:
        void setup(ESP_PanelBacklight *backlight) {
            this->backlight = backlight;
            backlight->setBrightness(50);
        }

        void resolve(SolaxDongleInverterData_t inverterData) {
            int pvPower = inverterData.pv1Power + inverterData.pv2Power;
            if(pvPower == 0) {
                this->backlight->setBrightness(50);
            } else {
                this->backlight->setBrightness(100);
            }
        }
};