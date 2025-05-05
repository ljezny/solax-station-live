#pragma once

#include <Arduino.h>
#include "MedianPowerSampler.hpp"
typedef enum
{
    SHELLY_UNKNOWN = -999,
    SHELLY_FULL_OFF = -2,
    SHELLY_PARTIAL_OFF = -1,
    SHELLY_KEEP_CURRENT_STATE = 0,
    SHELLY_PARTIAL_ON = 1,
    SHELLY_FULL_ON = 2
} RequestedShellyState_t;

class ShellyRuleResolver
{
private:
    MedianPowerSampler &medianPowerSampler;

public:
    ShellyRuleResolver(MedianPowerSampler &medianPowerSampler) : medianPowerSampler(medianPowerSampler)
    {
    }

    RequestedShellyState_t resolveShellyState()
    {
        int enablePowerTreshold = 1500;
        int enablePartialPowerTreshold = 100;
        int disableFullPowerTreshold = 500;
        int disablePartialPowerTreshold = 100;

        if (!medianPowerSampler.hasValidSamples())
        {
            return SHELLY_UNKNOWN;
        }

        int pvPower = medianPowerSampler.getMedianPVPower();
        int loadPower = medianPowerSampler.getMedianLoadPower();
        int feedInPower = medianPowerSampler.getMedianFeedInPower();
        int batteryPower = medianPowerSampler.getMedianBatteryPower();
        int soc = medianPowerSampler.getSOC();

        bool hasBattery = medianPowerSampler.getSOC() != 0 && medianPowerSampler.getMedianBatteryPower() != 0;
        log_d("SOC: %d, Median battery power: %d, Median feed in power: %d, Median PV power: %d, Median load power: %d", soc, batteryPower, feedInPower, pvPower, loadPower);

        medianPowerSampler.resetSamples();

        if (hasBattery && soc < 80)
        {
            log_d("Battery under limit empty, deactivating");
            return SHELLY_FULL_OFF;
        }

        if (batteryPower < -disableFullPowerTreshold)
        {
            log_d("Battery discharging, deactivating");
            return SHELLY_FULL_OFF;
        }

        if (feedInPower < -disableFullPowerTreshold)
        {
            log_d("Grid power, deactivating");
            return SHELLY_FULL_OFF;
        }

        if (batteryPower < -disablePartialPowerTreshold)
        {
            log_d("Battery discharging, partial deactivating");
            return SHELLY_PARTIAL_OFF;
        }

        if (feedInPower < -disablePartialPowerTreshold)
        {
            log_d("Grid power, partial deactivating");
            return SHELLY_PARTIAL_OFF;
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