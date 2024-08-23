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
            backlight->setBrightness(100);
        }

        void resolve(InverterData_t inverterData) {
            log_d("Resolving backlight");
            int pvPower = inverterData.pv1Power + inverterData.pv2Power;
            int brightness = 100;
            if(inverterData.status == DONGLE_STATUS_OK) {
                if(pvPower > 2000) {
                    log_d("Setting brightness to 100");
                    brightness = 100;
                } else if(pvPower > 0) {
                    log_d("Setting brightness to 80");
                    brightness = 80;
                } else {
                    log_d("Setting brightness to 20");
                    brightness = 20;
                }
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