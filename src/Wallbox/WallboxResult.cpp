#include "WallboxResult.hpp"
#include <RemoteLogger.hpp>
#include <Arduino.h>

void logWallboxResult(WallboxResult_t &result)
{
    LOGD("Wallbox: EV=%d P=%dW I=%d/%d/%dA E=%.1f/%.1fkWh ctrl=%d ph=%d V=%d/%d/%d I=%d/%d/%d T=%d",
         result.evConnected, result.chargingPower,
         result.chargingCurrent, result.maxChargingCurrent, result.targetChargingCurrent,
         result.chargedEnergy, result.totalChargedEnergy,
         result.chargingControlEnabled, result.phases,
         result.voltageL1, result.voltageL2, result.voltageL3,
         result.currentL1, result.currentL2, result.currentL3,
         result.temperature);
}