#pragma once

#include <Arduino.h>

typedef enum
{
    SHELLY_FULL_OFF = -2,
    SHELLY_PARTIAL_OFF = -1,
    SHELLY_KEEP_CURRENT_STATE = 0,
    SHELLY_PARTIAL_ON = 1,
    SHELLY_FULL_ON = 2
} RequestedShellyState_t;

typedef struct PowerSample
{
    uint32_t timestamp;
    int pvPower = 0;
    int soc = 0;
    int16_t batteryPower = 0;
    int16_t loadPower = 0;
    int32_t feedInPower = 0;
} PowerSample_t;

#define MAX_POWER_SAMPLES 3

class ShellyRuleResolver
{
private:
    PowerSample_t powerSamples[MAX_POWER_SAMPLES];

    bool hasValidSamples()
    {
        for (int i = 0; i < MAX_POWER_SAMPLES; i++)
        {
            if (powerSamples[i].timestamp == 0)
            {
                return false;
            }
        }
        return true;
    }

    void resetSamples()
    {
        for (int i = 0; i < MAX_POWER_SAMPLES; i++)
        {
            powerSamples[i].timestamp = 0;
        }
    }

    int getMedianPVPower()
    {
        int values[MAX_POWER_SAMPLES];
        for (int i = 0; i < MAX_POWER_SAMPLES; i++)
        {
            values[i] = powerSamples[i].pvPower;
        }
        std::sort(values, values + MAX_POWER_SAMPLES);
        return values[MAX_POWER_SAMPLES / 2];
    }

    int getSOC()
    {
        return powerSamples[0].soc;
    }

    int getMedianBatteryPower()
    {
        int values[MAX_POWER_SAMPLES];
        for (int i = 0; i < MAX_POWER_SAMPLES; i++)
        {
            values[i] = powerSamples[i].batteryPower;
        }
        std::sort(values, values + MAX_POWER_SAMPLES);
        return values[MAX_POWER_SAMPLES / 2];
    }

    int getMedianLoadPower()
    {
        int values[MAX_POWER_SAMPLES];
        for (int i = 0; i < MAX_POWER_SAMPLES; i++)
        {
            values[i] = powerSamples[i].loadPower;
        }
        std::sort(values, values + MAX_POWER_SAMPLES);
        return values[MAX_POWER_SAMPLES / 2];
    }

    int getMedianFeedInPower()
    {
        int values[MAX_POWER_SAMPLES];
        for (int i = 0; i < MAX_POWER_SAMPLES; i++)
        {
            values[i] = powerSamples[i].feedInPower;
        }
        std::sort(values, values + MAX_POWER_SAMPLES);
        return values[MAX_POWER_SAMPLES / 2];
    }

public:
    void addPowerSample(int pvPower, int soc, int batteryPower, int loadPower, int feedInPower)
    {
        PowerSample_t sample;
        sample.timestamp = millis();
        sample.pvPower = pvPower;
        sample.soc = soc;
        sample.batteryPower = batteryPower;
        sample.loadPower = loadPower;
        sample.feedInPower = feedInPower;
        for (int i = MAX_POWER_SAMPLES - 1; i > 0; i--)
        {
            powerSamples[i] = powerSamples[i - 1];
        }
        powerSamples[0] = sample;
    }

    RequestedShellyState_t resolveShellyState()
    {
        int enablePowerTreshold = 1500;
        int enablePartialPowerTreshold = 100;
        int disableFullPowerTreshold = 500;
        int disablePartialPowerTreshold = 100;

        if (!hasValidSamples())
        {
            return SHELLY_KEEP_CURRENT_STATE;
        }

        float pvPower = getMedianPVPower();
        float loadPower = getMedianLoadPower();
        float feedInPower = getMedianFeedInPower();
        float batteryPower = getMedianBatteryPower();
        int soc = getSOC();

        bool hasBattery = getSOC() != 0 && getMedianBatteryPower() != 0;
        log_d("SOC: %d, Median battery power: %d, Median feed in power: %d, Median PV power: %d, Median load power: %d", getSOC(), batteryPower, feedInPower, pvPower, loadPower);

        resetSamples();

        return getMedianLoadPower() > 400 ? SHELLY_PARTIAL_ON : SHELLY_PARTIAL_OFF; // testing

        if (batteryPower < -disableFullPowerTreshold)
        {
            log_d("Battery discharging, deactivating");
            return SHELLY_FULL_OFF;
        }

        if (batteryPower < -disablePartialPowerTreshold)
        {
            log_d("Battery discharging, partial deactivating");
            return SHELLY_PARTIAL_OFF;
        }

        if (feedInPower < -disableFullPowerTreshold)
        {
            log_d("Grid power, deactivating");
            return SHELLY_FULL_OFF;
        }

        if (feedInPower < -disablePartialPowerTreshold)
        {
            log_d("Grid power, partial deactivating");
            return SHELLY_PARTIAL_OFF;
        }

        if (hasBattery && soc < 80)
        {
            log_d("Battery under limit empty, deactivating");
            return SHELLY_FULL_OFF;
        }

        if (soc >= 99)
        {
            log_d("Battery full, activating");
            return SHELLY_FULL_ON;
        }

        if (soc > 90 && batteryPower > enablePowerTreshold)
        {
            log_d("Battery almost full and charging, activating");
            return SHELLY_FULL_ON;
        }

        if (soc > 96 && batteryPower > enablePartialPowerTreshold)
        {
            log_d("Battery almost full and charging, partial activating");
            return SHELLY_PARTIAL_ON;
        }

        if (feedInPower > enablePowerTreshold)
        {
            log_d("Feeding in power, activating");
            return SHELLY_FULL_ON;
        }

        if (feedInPower > enablePartialPowerTreshold)
        {
            log_d("Feeding in power, partial activating");
            return SHELLY_PARTIAL_ON;
        }

        return SHELLY_KEEP_CURRENT_STATE;
    }
};