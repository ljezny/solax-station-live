#pragma once

#include <RemoteLogger.hpp>
#include <SolarIntelligence.h>

typedef enum DongleStatus {
    DONGLE_STATUS_OK = 1,
    DONGLE_STATUS_UNKNOWN = 0,
    DONGLE_STATUS_CONNECTION_ERROR = -1,
    DONGLE_STATUS_HTTP_ERROR = -2,
    DONGLE_STATUS_JSON_ERROR = -3,
    DONGLE_STATUS_WIFI_DISCONNECTED = -4,
    DONGLE_STATUS_UNSUPPORTED_DONGLE = -5,
} DongleStatus_t;

typedef struct
{
    DongleStatus_t status = DONGLE_STATUS_UNKNOWN;
    long millis = 0;
    String sn;
    String dongleFWVersion;
    int pv1Power = 0; 
    int pv2Power = 0;
    int pv3Power = 0;
    int pv4Power = 0;
    int soc = 0;
    int minSoc = 0;
    int maxSoc = 100;
    uint16_t batteryCapacityWh = 0; // in Wh
    bool socApproximated = false;
    int16_t batteryPower = 0;
    float batteryVoltage = 0.0f;
    double batteryChargedToday = 0;
    double batteryChargedTotal = 0;
    double batteryDischargedToday = 0;
    double batteryDischargedTotal = 0;
    double gridBuyToday = 0;
    double gridSellToday = 0;
    double gridBuyTotal = 0;
    double gridSellTotal = 0;
    uint16_t maxChargePowerW = 0;     // Maximální nabíjecí výkon baterie ve W
    uint16_t maxDischargePowerW = 0;
    int inverterOutpuPowerL1 = 0;
    int inverterOutpuPowerL2 = 0;
    int inverterOutpuPowerL3 = 0;
    int16_t loadPower = 0;
    float loadToday = 0;
    float loadTotal = 0;
    int32_t gridPowerL1 = 0;
    int32_t gridPowerL2 = 0;
    int32_t gridPowerL3 = 0;
    int inverterTemperature = 0;
    int batteryTemperature = 0;
    double pvToday = 0;
    double pvTotal = 0;
    bool hasBattery = true;
    SolarInverterMode_t inverterMode = SI_MODE_UNKNOWN;  // Aktuální režim střídače (inteligence)
    time_t inverterTime = 0;  // RTC čas ze střídače (0 = neplatný)
} InverterData_t;

void logInverterData(InverterData_t& inverterData, int loadTimeMs = 0) {
    LOGD("Inv: SOC=%d%% BatPwr=%dW PV1=%dW PV2=%dW PV3=%dW PV4=%dW Load=%dW GridPwr=%d/%d/%dW [%dms]",
         inverterData.soc, inverterData.batteryPower,
         inverterData.pv1Power, inverterData.pv2Power, inverterData.pv3Power, inverterData.pv4Power,
         inverterData.loadPower,
         inverterData.gridPowerL1, inverterData.gridPowerL2, inverterData.gridPowerL3,
         loadTimeMs);
} 
