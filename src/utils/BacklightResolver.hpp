#pragma once

#include "Inverters/InverterResult.hpp"
#include "RemoteLogger.hpp"
#include "gfx_conf.h"
#include <Preferences.h>

#define BACKLIGHT_TOUCH_TIMEOUT 15000
#define PREF_DISPLAY_TIMEOUT "dispTimeout"

#if CROW_PANEL_ADVANCE
#include <PCA9557.h>
PCA9557 io(0x18, &Wire);
#endif

class BacklightResolver
{
private:
    long lastTouchTime = 0;
    unsigned long lastActivityTime = 0;
    int displayOffTimeout = 0; // 0 = never, otherwise minutes (5, 15, 30, 60)
    bool displayIsOff = false;
    Preferences preferences;

    bool i2cScanForAddress(uint8_t address)
    {
        Wire.beginTransmission(address);
        return (Wire.endTransmission() == 0);
    }

public:
    void setup()
    {
        // Load display timeout from preferences
        preferences.begin("backlight", true);
        displayOffTimeout = preferences.getInt(PREF_DISPLAY_TIMEOUT, 0);
        preferences.end();
        log_d("Display off timeout loaded: %d minutes", displayOffTimeout);

        lastActivityTime = millis();

#if CROW_PANEL_ADVANCE
        Wire.begin(15, 16);
        delay(500);
        if (i2cScanForAddress(0x30)) // new V1.2
        {
            Wire.beginTransmission(0x30);
            Wire.write(0x10);
            Wire.endTransmission();
        }
        if (i2cScanForAddress(0x18)) // old V1.0
        {
            // Wire.beginTransmission(0x18);
            // Wire.write(0x03);
            // Wire.write(0x01);
            // Wire.endTransmission();

            // Wire.beginTransmission(0x18);
            // Wire.write(0x02);
            // Wire.write(0x01);
            // Wire.endTransmission();

            io.pinMode(1, OUTPUT);
            io.digitalWrite(1, 1);
        }

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
                brightness = 255;
            }
            else if (pvPower > 0)
            {
                brightness = 128;
            }
            else
            {
                brightness = 16;
            }
        }

        // Check if display should be turned off (timeout)
        if (displayOffTimeout > 0)
        {
            unsigned long timeoutMs = (unsigned long)displayOffTimeout * 60 * 1000;
            if ((millis() - lastActivityTime) > timeoutMs)
            {
                if (!displayIsOff)
                {
                    log_d("Display off after %d minutes of inactivity", displayOffTimeout);
                    displayIsOff = true;
                }
                setBacklightAnimated(0);
                return;
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
        this->lastActivityTime = millis();

        // Wake up display if it was off
        if (displayIsOff)
        {
            log_d("Display waking up from touch");
            displayIsOff = false;
        }

        setBacklightAnimated(255);
    }

    /**
     * Set display off timeout in minutes
     * @param minutes 0 = never, 5/15/30/60 = timeout in minutes
     */
    void setDisplayOffTimeout(int minutes)
    {
        displayOffTimeout = minutes;
        lastActivityTime = millis(); // Reset timer when changing setting
        displayIsOff = false;

        preferences.begin("backlight", false);
        preferences.putInt(PREF_DISPLAY_TIMEOUT, displayOffTimeout);
        preferences.end();
        log_d("Display off timeout saved: %d minutes", displayOffTimeout);
    }

    /**
     * Get current display off timeout in minutes
     */
    int getDisplayOffTimeout()
    {
        return displayOffTimeout;
    }

    /**
     * Check if display is currently off
     */
    bool isDisplayOff()
    {
        return displayIsOff;
    }

    void setBacklightAnimated(int brightness)
    {
#if CROW_PANEL_ADVANCE
        if (i2cScanForAddress(0x30)) // new V1.2
        {
            Wire.beginTransmission(0x30);
            // needs to recompute brightness from 0-255 to 0-16
            uint8_t pwmValue = map(brightness, 0, 255, 0, 16);
            Wire.write(pwmValue);
            Wire.endTransmission();
        }
        else if (i2cScanForAddress(0x18)) // old V1.0
        {
            io.digitalWrite(1, brightness > 0 ? 1 : 0);
        }

#else
        for (int i = tft.getBrightness(); i != brightness; i += (brightness > tft.getBrightness()) ? 1 : -1)
        {
            tft.setBrightness(i);
            delay(5);
        }
#endif
    }
};