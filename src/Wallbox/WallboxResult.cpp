#include "WallboxResult.hpp"
#include <RemoteLogger.hpp>
#include <Arduino.h>

void logWallboxResult(WallboxResult_t &result)
{
    LOGD("Wallbox updated: %ld", result.updated);
    LOGD("  EV connected: %s", String(result.evConnected).c_str());
    LOGD("  Charging power: %d W", result.chargingPower);
    LOGD("  Charging current: %d A", result.chargingCurrent);
    LOGD("  Max charging current: %d A", result.maxChargingCurrent);
    LOGD("  Target charging current: %d A", result.targetChargingCurrent);
    LOGD("  Charged energy: %.2f kWh", result.chargedEnergy);
    LOGD("  Total charged energy: %.2f kWh", result.totalChargedEnergy);
    LOGD("  Charging control enabled: %s", String(result.chargingControlEnabled).c_str());
    LOGD("  Phases: %d", result.phases);
    LOGD("  Voltage L1: %d V", result.voltageL1);
    LOGD("  Voltage L2: %d V", result.voltageL2);
    LOGD("  Voltage L3: %d V", result.voltageL3);
    LOGD("  Current L1: %d A", result.currentL1);
    LOGD("  Current L2: %d A", result.currentL2);
    LOGD("  Current L3: %d A", result.currentL3);
    LOGD("  Temperature: %d °C", result.temperature);
}