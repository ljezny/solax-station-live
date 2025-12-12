#pragma once

#include <Arduino.h>
#include "RemoteLogger.hpp"
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
        LOGD("SOC: %d, Median battery power: %d, Median feed in power: %d, Median PV power: %d, Median load power: %d", soc, batteryPower, feedInPower, pvPower, loadPower);

        medianPowerSampler.resetSamples();

        if (hasBattery && soc < 80)
        {
            LOGD("Battery under limit empty, deactivating");
            return SMART_CONTROL_FULL_OFF;
        }

        if (batteryPower < -disableFullPowerTreshold)
        {
            LOGD("Battery discharging, deactivating");
            return SMART_CONTROL_FULL_OFF;
        }

        if (feedInPower < -disableFullPowerTreshold)
        {
            LOGD("Grid power, deactivating");
            return SMART_CONTROL_FULL_OFF;
        }

        if (batteryPower < -disablePartialPowerTreshold)
        {
            LOGD("Battery discharging, partial deactivating");
            return SMART_CONTROL_PARTIAL_OFF;
        }

        if (feedInPower < -disablePartialPowerTreshold)
        {
            LOGD("Grid power, partial deactivating");
            return SMART_CONTROL_PARTIAL_OFF;
        }

        if (soc >= 95)
        {
            LOGD("Battery full, activating");
            return SMART_CONTROL_FULL_ON;
        }

        if (soc >= 85 && batteryPower > enablePowerTreshold)
        {
            LOGD("Battery almost full and charging, activating");
            return SMART_CONTROL_FULL_ON;
        }

        if (soc >= 90 && batteryPower > enablePartialPowerTreshold)
        {
            LOGD("Battery almost full and charging, partial activating");
            return SMART_CONTROL_PARTIAL_ON;
        }

        if (feedInPower > enablePowerTreshold)
        {
            LOGD("Feeding in power, activating");
            return SMART_CONTROL_FULL_ON;
        }

        if (feedInPower > enablePartialPowerTreshold)
        {
            LOGD("Feeding in power, partial activating");
            return SMART_CONTROL_PARTIAL_ON;
        }

        return SMART_CONTROL_KEEP_CURRENT_STATE;
    }
};