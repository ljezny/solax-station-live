#pragma once

#include "Inverters/InverterResult.hpp"
#include "gfx_conf.h"

#define BACKLIGHT_TOUCH_TIMEOUT 15000

#if CROW_PANEL_ADVANCE
#include <PCA9557.h>
PCA9557 io(0x18, &Wire);
#endif

class BacklightResolver
{
private:
    long lastTouchTime = 0;

public:
    void setup()
    {
#if CROW_PANEL_ADVANCE
        io.pinMode(1, OUTPUT);
#endif
        setBacklightAnimated(255);
    }

    void resolve(InverterData_t inverterData)
    {
        int pvPower = inverterData.pv1Power + inverterData.pv2Power + inverterData.pv3Power + inverterData.pv4Power;
        int brightness = 100;
        if (inverterData.status == DONGLE_STATUS_OK)
        {
            if (pvPower > 2000)
            {
                log_d("Setting brightness to 100%");
                brightness = 255;
            }
            else if (pvPower > 0)
            {
                log_d("Setting brightness to 80%");
                brightness = 192;
            }
            else
            {
                log_d("Setting brightness to 20%");
                brightness = 16;
            }
        }

        if ((millis() - this->lastTouchTime) > BACKLIGHT_TOUCH_TIMEOUT)
        {
            setBacklightAnimated(brightness);
        }
        else
        {
            setBacklightAnimated(255);
        }
    }

    void touch()
    {
        this->lastTouchTime = millis();
        setBacklightAnimated(255);
    }

    void setBacklightAnimated(int brightness)
    {
#if CROW_PANEL_ADVANCE
        io.digitalWrite(1, brightness > 0);
#else
        for (int i = tft.getBrightness(); i != brightness; i += (brightness > tft.getBrightness()) ? 1 : -1)
        {
            tft.setBrightness(i);
            delay(5);
        }
#endif
    } 
};