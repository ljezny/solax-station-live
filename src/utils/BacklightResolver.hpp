#pragma once

#include <ESP_Panel_Library.h>
#include "Inverters/InverterResult.hpp"

#define BACKLIGHT_TOUCH_TIMEOUT 5000

class BacklightResolver {
    private:
        ESP_PanelBacklight *backlight;
        long lastTouchTime = 0;
    public:
        void setup(ESP_PanelBacklight *backlight) {
            this->backlight = backlight;
            backlight->setBrightness(20);
        }

        void resolve(InverterData_t inverterData) {
            int pvPower = inverterData.pv1Power + inverterData.pv2Power;
            int brightness = 0;
            if(pvPower == 0) {
                brightness = 20;
            } else {
                brightness = 80;
            }

            if((millis() - this->lastTouchTime) > BACKLIGHT_TOUCH_TIMEOUT) {
                this->backlight->setBrightness(brightness);
            } else {
                this->backlight->setBrightness(100);
            }
        }

        void touch() {
            this->lastTouchTime = millis();
            this->backlight->setBrightness(100);
        }
};