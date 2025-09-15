#include "WallboxResult.hpp"
#include <Arduino.h>

void logWallboxResult(WallboxResult_t &result)
{
    log_d("Wallbox updated: %ld", result.updated);
    log_d("  EV connected: %s", String(result.evConnected).c_str());
    log_d("  Charging power: %d W", result.chargingPower);
    log_d("  Charging current: %d A", result.chargingCurrent);
    log_d("  Max charging current: %d A", result.maxChargingCurrent);
    log_d("  Target charging current: %d A", result.targetChargingCurrent);
    log_d("  Charged energy: %.2f kWh", result.chargedEnergy);
    log_d("  Total charged energy: %.2f kWh", result.totalChargedEnergy);
    log_d("  Charging control enabled: %s", String(result.chargingControlEnabled).c_str());
    log_d("  Phases: %d", result.phases);
    log_d("  Voltage L1: %d V", result.voltageL1);
    log_d("  Voltage L2: %d V", result.voltageL2);
    log_d("  Voltage L3: %d V", result.voltageL3);
    log_d("  Current L1: %d A", result.currentL1);
    log_d("  Current L2: %d A", result.currentL2);
    log_d("  Current L3: %d A", result.currentL3);
    log_d("  Temperature: %d °C", result.temperature);
}