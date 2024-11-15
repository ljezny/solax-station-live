#pragma once

#include <Arduino.h>

typedef enum {
    SHELLY_DEACTIVATE = 0,
    SHELLY_KEEP_CURRENT_STATE = 1,
    SHELLY_ACTIVATE = 2
} RequestedShellyState_t;

typedef struct PowerSample {
    uint32_t timestamp;
    int pvPower = 0;
    int soc = 0;
    int16_t batteryPower = 0;
    int16_t loadPower = 0;
    int32_t feedInPower = 0;
} PowerSample_t;

#define MAX_POWER_SAMPLES 10

class ShellyRuleResolver
{
private:
    PowerSample_t powerSamples[MAX_POWER_SAMPLES];

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
        PowerSample_t sample;
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
    
    RequestedShellyState_t resolveShellyState()
    {
        int enablePowerTreshold = 1500;
        int disablePowerTreshold = 500;

        bool hasBattery = getSOC() != 0 && getMedianBatteryPower() != 0;
        log_d("SOC: %d, Median battery power: %d, Median feed in power: %d, Median PV power: %d, Median load power: %d", getSOC(), getMedianBatteryPower(), getMedianFeedInPower(), getMedianPVPower(), getMedianLoadPower());
       
        if (!hasValidSamples())
        {
            return SHELLY_DEACTIVATE;
        }

        if(getMedianBatteryPower() < -disablePowerTreshold) {
            log_d("Battery discharging, deactivating");
            return SHELLY_DEACTIVATE;
        }

        if(getMedianFeedInPower() < -disablePowerTreshold) {
            log_d("Grid power, deactivating");
            return SHELLY_DEACTIVATE;
        }
        
        if(hasBattery && getSOC() < 80) {
            log_d("Battery under limit empty, deactivating");
            return SHELLY_DEACTIVATE;
        }

        if (getSOC() >= 99)
        {
            log_d("Battery full, activating");
            return SHELLY_ACTIVATE;
        }

        if (getSOC() > 90 && getMedianBatteryPower() > enablePowerTreshold)
        {
            log_d("Battery almost full and charging, activating");
            return SHELLY_ACTIVATE;
        }

        if (getMedianFeedInPower() > enablePowerTreshold)
        {
            log_d("Feeding in power, activating");
            return SHELLY_ACTIVATE;
        }

        return SHELLY_KEEP_CURRENT_STATE;
    }
};