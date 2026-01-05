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
            // Fallback: assume V1.2 for backwards compatibility
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
            LOGD("Setting backlight: %d -> %d (version=%s)", lastBrightness, brightness, getVersionName());
            lastBrightness = brightness;
        }
#if CROW_PANEL_ADVANCE
        if (detectedVersion == CrowPanelVersion::V1_0)
        {
            // V1.0: PCA9557 at 0x18, digital on/off only
            io.digitalWrite(1, brightness > 0 ? 1 : 0);
            LOGD("V1.0: digitalWrite=%d", brightness > 0 ? 1 : 0);
        }
        else if (detectedVersion == CrowPanelVersion::V1_2)
        {
            // V1.2: STC8H1K28 at 0x30, values 0x05 (off) to 0x10 (max)
            Wire.beginTransmission(0x30);
            uint8_t pwmValue;
            if (brightness == 0) {
                pwmValue = 0x05; // OFF
            } else {
                // Map 1-255 to 0x06-0x10 (6 levels)
                pwmValue = map(brightness, 1, 255, 0x06, 0x10);
            }
            Wire.write(pwmValue);
            uint8_t result = Wire.endTransmission();
            LOGD("V1.2: pwmValue=0x%02X, result=%d", pwmValue, result);
        }
        else if (detectedVersion == CrowPanelVersion::V1_3)
        {
            // V1.3: STC8H1K28 at 0x30, values 0 (max) to 244 (min), 245 (off)
            Wire.beginTransmission(0x30);
            uint8_t pwmValue;
            if (brightness == 0) {
                pwmValue = 245; // OFF
            } else {
                // Map 1-255 to 244-0 (inverted, 0 is brightest)
                pwmValue = map(brightness, 1, 255, 244, 0);
            }
            Wire.write(pwmValue);
            uint8_t result = Wire.endTransmission();
            LOGD("V1.3: pwmValue=%d, result=%d", pwmValue, result);
        }
        else
        {
            // Unknown version - try both protocols and log
            LOGW("Unknown board version, trying V1.2 protocol as fallback");
            if (i2cScanForAddress(0x30))
            {
                Wire.beginTransmission(0x30);
                uint8_t pwmValue = map(brightness, 0, 255, 0x05, 0x10);
                Wire.write(pwmValue);
                Wire.endTransmission();
            }
            else if (i2cScanForAddress(0x18))
            {
                io.digitalWrite(1, brightness > 0 ? 1 : 0);
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
     * Detect board version by reading from I2C
     */
    CrowPanelVersion detectBoardVersion()
    {
        // Check for V1.0 (PCA9557 at 0x18)
        if (i2cScanForAddress(0x18))
        {
            LOGD("Found PCA9557 at 0x18 -> V1.0");
            return CrowPanelVersion::V1_0;
        }
        
        // Check for V1.2/V1.3 (STC8H1K28 at 0x30)
        if (!i2cScanForAddress(0x30))
        {
            LOGW("No backlight controller found at 0x30 or 0x18!");
            return CrowPanelVersion::UNKNOWN;
        }
        
        // Read 4 bytes from 0x30 to distinguish V1.2 vs V1.3
        // V1.2 returns: [0xFF, 0x4E, ...]
        // V1.3 returns: [0xFF, 0x52, ...]
        uint8_t readBuffer[4] = {0};
        int bytesRead = Wire.requestFrom((uint8_t)0x30, (uint8_t)4);
        
        for (int i = 0; i < bytesRead && i < 4; i++)
        {
            readBuffer[i] = Wire.read();
        }
        
        LOGD("Read from 0x30: [0x%02X, 0x%02X, 0x%02X, 0x%02X]", 
             readBuffer[0], readBuffer[1], readBuffer[2], readBuffer[3]);
        
        // Detect version based on second byte
        // V1.2: 0x4E (78), V1.3: 0x52 (82)
        if (readBuffer[1] == 0x4E)
        {
            LOGD("Detected CrowPanel V1.2 (signature byte: 0x4E)");
            return CrowPanelVersion::V1_2;
        }
        else if (readBuffer[1] == 0x52)
        {
            LOGD("Detected CrowPanel V1.3 (signature byte: 0x52)");
            return CrowPanelVersion::V1_3;
        }
        else
        {
            // Unknown signature, default to V1.3 as it's the newest and seems compatible
            LOGW("Unknown signature byte 0x%02X, defaulting to V1.3 protocol", readBuffer[1]);
            return CrowPanelVersion::V1_3;
        }
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