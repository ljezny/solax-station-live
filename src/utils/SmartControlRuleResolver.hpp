#pragma once

#include <Arduino.h>
#include "MedianPowerSampler.hpp"
typedef enum
{
    SMART_CONTROL_UNKNOWN = -999,
    SMART_CONTROL_FULL_OFF = -2,
    SMART_CONTROL_PARTIAL_OFF = -1,
    SMART_CONTROL_KEEP_CURRENT_STATE = 0,
    SMART_CONTROL_PARTIAL_ON = 1,
    SMART_CONTROL_FULL_ON = 2
} RequestedSmartControlState_t;

class SmartControlRuleResolver
{
private:
    MedianPowerSampler &medianPowerSampler;
public:
    SmartControlRuleResolver(MedianPowerSampler &medianPowerSampler) : medianPowerSampler(medianPowerSampler)
    {
    }

    RequestedSmartControlState_t resolveSmartControlState(int enablePowerTreshold, int enablePartialPowerTreshold, int disableFullPowerTreshold, int disablePartialPowerTreshold)
    {
        if (!medianPowerSampler.hasValidSamples())
        {
            return SMART_CONTROL_UNKNOWN;
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
            return SMART_CONTROL_FULL_OFF;
        }

        if (batteryPower < -disableFullPowerTreshold)
        {
            log_d("Battery discharging, deactivating");
            return SMART_CONTROL_FULL_OFF;
        }

        if (feedInPower < -disableFullPowerTreshold)
        {
            log_d("Grid power, deactivating");
            return SMART_CONTROL_FULL_OFF;
        }

        if (batteryPower < -disablePartialPowerTreshold)
        {
            log_d("Battery discharging, partial deactivating");
            return SMART_CONTROL_PARTIAL_OFF;
        }

        if (feedInPower < -disablePartialPowerTreshold)
        {
            log_d("Grid power, partial deactivating");
            return SMART_CONTROL_PARTIAL_OFF;
        }

        if (soc >= 95)
        {
            log_d("Battery full, activating");
            return SMART_CONTROL_FULL_ON;
        }

        if (soc >= 85 && batteryPower > enablePowerTreshold)
        {
            log_d("Battery almost full and charging, activating");
            return SMART_CONTROL_FULL_ON;
        }

        if (soc >= 90 && batteryPower > enablePartialPowerTreshold)
        {
            log_d("Battery almost full and charging, partial activating");
            return SMART_CONTROL_PARTIAL_ON;
        }

        if (feedInPower > enablePowerTreshold)
        {
            log_d("Feeding in power, activating");
            return SMART_CONTROL_FULL_ON;
        }

        if (feedInPower > enablePartialPowerTreshold)
        {
            log_d("Feeding in power, partial activating");
            return SMART_CONTROL_PARTIAL_ON;
        }

        return SMART_CONTROL_KEEP_CURRENT_STATE;
    }
};