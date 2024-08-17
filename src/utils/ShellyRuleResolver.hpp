#pragma once

#include <Arduino.h>

typedef struct PowerSample {
    uint32_t timestamp;
    int pvPower = 0;
    int soc = 0;
    int16_t batteryPower = 0;
    int16_t loadPower = 0;
    int32_t feedInPower = 0;
};

#define MAX_POWER_SAMPLES 10

class ShellyRuleResolver
{
private:
    PowerSample powerSamples[MAX_POWER_SAMPLES];

    bool hasValidSamples() {
        for (int i = 0; i < MAX_POWER_SAMPLES; i++) {
            if (powerSamples[i].timestamp == 0) {
                return false;
            }
        }
        return true;
    }

    int getMedianPVPower() {
        int values[MAX_POWER_SAMPLES];
        for (int i = 0; i < MAX_POWER_SAMPLES; i++) {
            values[i] = powerSamples[i].pvPower;
        }
        std::sort(values, values + MAX_POWER_SAMPLES);
        return values[MAX_POWER_SAMPLES / 2];
    }

    int getSOC() {
        return powerSamples[0].soc;
    }

    int getMedianBatteryPower() {
        int values[MAX_POWER_SAMPLES];
        for (int i = 0; i < MAX_POWER_SAMPLES; i++) {
            values[i] = powerSamples[i].batteryPower;
        }
        std::sort(values, values + MAX_POWER_SAMPLES);
        return values[MAX_POWER_SAMPLES / 2];
    }

    int getMedianLoadPower() {
        int values[MAX_POWER_SAMPLES];
        for (int i = 0; i < MAX_POWER_SAMPLES; i++) {
            values[i] = powerSamples[i].loadPower;
        }
        std::sort(values, values + MAX_POWER_SAMPLES);
        return values[MAX_POWER_SAMPLES / 2];
    }

    int getMedianFeedInPower() {
        int values[MAX_POWER_SAMPLES];
        for (int i = 0; i < MAX_POWER_SAMPLES; i++) {
            values[i] = powerSamples[i].feedInPower;
        }
        std::sort(values, values + MAX_POWER_SAMPLES);
        return values[MAX_POWER_SAMPLES / 2];
    }
public:
    void addPowerSample(int pvPower, int soc, int batteryPower, int loadPower, int feedInPower) {
        PowerSample sample;
        sample.timestamp = millis();
        sample.pvPower = pvPower;
        sample.soc = soc;
        sample.batteryPower = batteryPower;
        sample.loadPower = loadPower;
        sample.feedInPower = feedInPower;
        for (int i = MAX_POWER_SAMPLES - 1; i > 0; i--) {
            powerSamples[i] = powerSamples[i - 1];
        }
        powerSamples[0] = sample;
    }
    
    bool canActivateShelly()
    {
        int powerTreshold = 1500;

        if (!hasValidSamples())
        {
            return false;
        }
        
        if (getSOC() >= 99)
        {
            log_d("Battery full");
            return true;
        }

        if (getSOC() > 90 && getMedianBatteryPower() > powerTreshold)
        {
            log_d("Battery almost full and charging");
            return true;
        }

        if (getMedianFeedInPower() > powerTreshold)
        {
            log_d("Feeding in power");
            return true;
        }

        return false;
    }
};