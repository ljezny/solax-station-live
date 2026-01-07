#pragma once

#include "Inverters/InverterResult.hpp"
#include <RemoteLogger.hpp>
#include "gfx_conf.h"
#include <Preferences.h>

#define BACKLIGHT_TOUCH_TIMEOUT 15000
#define PREF_DISPLAY_TIMEOUT "dispTimeout"

#if CROW_PANEL_ADVANCE
#include <PCA9557.h>
PCA9557 io(0x18, &Wire);
#endif

// Board version detection
enum class CrowPanelVersion {
    UNKNOWN = 0,
    V1_0 = 10,   // Uses PCA9557 at 0x18
    V1_2 = 12,   // Uses STC8H1K28 at 0x30, brightness 0x05-0x10
    V1_3 = 13    // Uses STC8H1K28 at 0x30, brightness 0-245 (inverted)
};

class BacklightResolver
{
private:
    long lastTouchTime = 0;
    unsigned long lastActivityTime = 0;
    int displayOffTimeout = 0; // 0 = never, otherwise minutes (5, 15, 30, 60)
    bool displayIsOff = false;
    Preferences preferences;
    int lastBrightness = -1; // For logging brightness changes
    CrowPanelVersion detectedVersion = CrowPanelVersion::UNKNOWN;

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
        LOGD("Display off timeout loaded: %d minutes", displayOffTimeout);

        lastActivityTime = millis();

#if CROW_PANEL_ADVANCE
        Wire.begin(15, 16);
        delay(500);
        
        // Detect board version
        detectedVersion = detectBoardVersion();
        LOGD("Detected CrowPanel version: %s", getVersionName());
        
        if (detectedVersion == CrowPanelVersion::V1_0 && i2cScanForAddress(0x18))
        {
            io.pinMode(1, OUTPUT);
            io.digitalWrite(1, 1);
        }
        else if (detectedVersion == CrowPanelVersion::V1_2 || detectedVersion == CrowPanelVersion::V1_3)
        {
            // Set max brightness based on version
            setBacklightAnimated(255);
        }
        else if (i2cScanForAddress(0x30))
        {
            // Fallback: use 0x10 which works on both V1.2 and V1.3
            Wire.beginTransmission(0x30);
            Wire.write(0x10);
            Wire.endTransmission();
        }
        if (i2cScanForAddress(0x18)) // old V1.0
        {
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
            unsigned long elapsed = millis() - lastActivityTime;
            
            if (elapsed > timeoutMs)
            {
                if (!displayIsOff)
                {
                    LOGD("Display turning OFF after %d minutes of inactivity", displayOffTimeout);
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
            LOGD("Display waking up from touch");
            displayIsOff = false;
        }

        LOGD("Touch detected, resetting activity timer");
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
        LOGD("Display off timeout saved: %d minutes", displayOffTimeout);
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
        if (brightness != lastBrightness)
        {
            LOGD("Setting backlight: %d -> %d", lastBrightness, brightness);
            lastBrightness = brightness;
        }
#if CROW_PANEL_ADVANCE
        if (detectedVersion == CrowPanelVersion::V1_0)
        {
            // V1.0: PCA9557 at 0x18, digital on/off only
            io.digitalWrite(1, brightness > 0 ? 1 : 0);
            LOGD("V1.0: digitalWrite=%d", brightness > 0 ? 1 : 0);
        }
        else
        {
            // V1.2/V1.3: Use detected protocol
            if (detectedVersion == CrowPanelVersion::V1_2)
            {
                // V1.2 protocol: 0x05=off, 0x06-0x10=brightness levels
                uint8_t v12Value;
                if (brightness == 0) {
                    v12Value = 0x05; // OFF
                } else {
                    v12Value = map(brightness, 1, 255, 0x06, 0x10);
                }
                Wire.beginTransmission(0x30);
                Wire.write(v12Value);
                Wire.endTransmission();
                LOGD("V1.2: brightness=0x%02X", v12Value);
            }
            else
            {
                // V1.3 protocol: inverted, 0=max, 244=min, 245=off
                uint8_t v13Value;
                if (brightness == 0) {
                    v13Value = 245; // OFF
                } else {
                    // Map 255->0 (brightest), 1->244 (dimmest)
                    v13Value = map(brightness, 1, 255, 244, 0);
                }
                Wire.beginTransmission(0x30);
                Wire.write(v13Value);
                Wire.endTransmission();
                LOGD("V1.3: brightness=%d", v13Value);
            }
        }

#else
        for (int i = tft.getBrightness(); i != brightness; i += (brightness > tft.getBrightness()) ? 1 : -1)
        {
            tft.setBrightness(i);
            delay(5);
        }
#endif
    }

private:
    /**
     * Detect board version by testing I2C response to value 25
     * V1.0: PCA9557 at 0x18
     * V1.2: STC8H1K28 at 0x30, value 25 causes I2C timeout
     * V1.3: STC8H1K28 at 0x30, value 25 works fine (valid brightness)
     */
    CrowPanelVersion detectBoardVersion()
    {
        // Check for V1.0 (PCA9557 at 0x18)
        if (i2cScanForAddress(0x18))
        {
            LOGD("Found PCA9557 at 0x18 -> V1.0");
            return CrowPanelVersion::V1_0;
        }
        
        // Check for V1.2/V1.3 (STC8H1K28 at 0x30) using Wire first
        if (!i2cScanForAddress(0x30))
        {
            LOGW("No backlight controller found at 0x30 or 0x18!");
            return CrowPanelVersion::UNKNOWN;
        }
        
        LOGD("Found STC8H1K28 at 0x30, detecting version...");
        
        // Set Wire timeout to detect slow transactions
        Wire.setTimeOut(100); // 100ms timeout
        
        // Send value 25 - valid for V1.3, problematic for V1.2
        Wire.beginTransmission(0x30);
        Wire.write(25);
        uint8_t result1 = Wire.endTransmission();
        delay(50);
        
        // Try to read from chip - V1.2 may not respond after bad value
        int bytesRead = Wire.requestFrom((uint8_t)0x30, (uint8_t)1);
        uint8_t readVal = 0;
        if (bytesRead > 0) {
            readVal = Wire.read();
        }
        LOGD("After 25: result=%d, read %d bytes (0x%02X)", result1, bytesRead, readVal);
        
        CrowPanelVersion detected;
        
        // If read returned 0 bytes, chip stopped responding = V1.2
        if (bytesRead == 0 || result1 != 0)
        {
            LOGD("No response after 25 -> V1.2 detected");
            detected = CrowPanelVersion::V1_2;
            
            // Reset I2C bus to recover from error state
            Wire.end();
            delay(100);
            Wire.begin(15, 16);
            delay(100);
            
            // Send valid V1.2 command to turn on display
            Wire.beginTransmission(0x30);
            Wire.write(0x10);
            Wire.endTransmission();
        }
        else
        {
            LOGD("Chip responded after 25 -> V1.3 detected");
            detected = CrowPanelVersion::V1_3;
            
            // Set V1.3 max brightness
            Wire.beginTransmission(0x30);
            Wire.write(0); // V1.3 max brightness
            Wire.endTransmission();
        }
        
        return detected;
    }
    
    const char* getVersionName() const
    {
        switch (detectedVersion)
        {
            case CrowPanelVersion::V1_0: return "V1.0";
            case CrowPanelVersion::V1_2: return "V1.2";
            case CrowPanelVersion::V1_3: return "V1.3";
            default: return "UNKNOWN";
        }
    }
};