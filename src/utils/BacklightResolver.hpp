#pragma once

#include "Inverters/InverterResult.hpp"
#include "gfx_conf.h"
#define BACKLIGHT_TOUCH_TIMEOUT 15000

class BacklightResolver {
    private:
        long lastTouchTime = 0;
    public:
        void setup() {
            tft.setBrightness(255);
        }

        void resolve(InverterData_t inverterData) {
            log_d("Resolving backlight");
            int pvPower = inverterData.pv1Power + inverterData.pv2Power;
            int brightness = 100;
            if(inverterData.status == DONGLE_STATUS_OK) {
                if(pvPower > 2000) {
                    log_d("Setting brightness to 100%");
                    brightness = 255;
                } else if(pvPower > 0) {
                    log_d("Setting brightness to 80%");
                    brightness = 192;
                } else {
                    log_d("Setting brightness to 20%");
                    brightness = 16;
                }
            }

            if((millis() - this->lastTouchTime) > BACKLIGHT_TOUCH_TIMEOUT) {
               setBacklightAnimated(brightness);
            } else {
               setBacklightAnimated(255);
            }
        }

        void touch() {
            this->lastTouchTime = millis();
            setBacklightAnimated(255);
        }

        void setBacklightAnimated(int brightness) {
            for(int i = tft.getBrightness(); i != brightness; i += (brightness > tft.getBrightness()) ? 1 : -1) {
                tft.setBrightness(i);
                delay(5);
            }
        }
};