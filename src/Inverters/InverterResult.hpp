#pragma once

typedef enum DongleStatus {
    DONGLE_STATUS_OK = 1,
    DONGLE_STATUS_UNKNOWN = 0,
    DONGLE_STATUS_CONNECTION_ERROR = -1,
    DONGLE_STATUS_HTTP_ERROR = -2,
    DONGLE_STATUS_JSON_ERROR = -3,
    DONGLE_STATUS_WIFI_DISCONNECTED = -4,
    DONGLE_STATUS_UNSUPPORTED_DONGLE = -5,
} DongleStatus_t;

/**
 * Příkazy pro režim střídače (inteligentní řízení)
 */
typedef enum InverterMode {
    INVERTER_MODE_UNKNOWN = 0,         // Neznámý stav / inteligence vypnutá
    INVERTER_MODE_SELF_USE,            // Normální provoz - spotřeba z baterie pro vlastní potřebu
    INVERTER_MODE_CHARGE_FROM_GRID,    // Nabíjet baterii ze sítě
    INVERTER_MODE_DISCHARGE_TO_GRID,   // Vybíjet baterii do sítě (prodej)
    INVERTER_MODE_HOLD_BATTERY,        // Držet baterii - nepoužívat ani nenabíjet
} InverterMode_t;

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
    InverterMode_t inverterMode = INVERTER_MODE_UNKNOWN;  // Aktuální režim střídače (inteligence)
    time_t inverterTime = 0;  // RTC čas ze střídače (0 = neplatný)
} InverterData_t;

void logInverterData(InverterData_t& inverterData) {
    log_d("Inverter data:");
    log_d("Status: %d", inverterData.status);
    log_d("Millis: %ld", inverterData.millis);
    log_d("SN: %s", inverterData.sn.c_str());
    log_d("PV1 Power: %d", inverterData.pv1Power);
    log_d("PV2 Power: %d", inverterData.pv2Power);
    log_d("PV3 Power: %d", inverterData.pv3Power);
    log_d("PV4 Power: %d", inverterData.pv4Power);
    log_d("SOC: %d", inverterData.soc);
    log_d("Battery Power: %d", inverterData.batteryPower);
    log_d("Battery Charged Today: %f", inverterData.batteryChargedToday);
    log_d("Battery Discharged Today: %f", inverterData.batteryDischargedToday);
    log_d("Battery capacity: %f", inverterData.batteryCapacityWh);
    log_d("Grid Buy Today: %f", inverterData.gridBuyToday);
    log_d("Grid Sell Today: %f", inverterData.gridSellToday);
    log_d("Grid Buy Total: %f", inverterData.gridBuyTotal);
    log_d("Grid Sell Total: %f", inverterData.gridSellTotal);
    log_d("L1 Power: %d", inverterData.inverterOutpuPowerL1);
    log_d("L2 Power: %d", inverterData.inverterOutpuPowerL2);
    log_d("L3 Power: %d", inverterData.inverterOutpuPowerL3);
    log_d("Load Power: %d", inverterData.loadPower);
    log_d("Load Today: %f", inverterData.loadToday);
    log_d("Load Total: %f", inverterData.loadTotal);
    log_d("Grid Power L1: %d", inverterData.gridPowerL1);
    log_d("Grid Power L2: %d", inverterData.gridPowerL2);
    log_d("Grid Power L3: %d", inverterData.gridPowerL3);
    log_d("Inverter Temperature: %d", inverterData.inverterTemperature);
    log_d("Battery Temperature: %d", inverterData.batteryTemperature);
    log_d("PV Today: %f", inverterData.pvToday);
    log_d("PV Total: %f", inverterData.pvTotal);
    log_d("Has Battery: %d", inverterData.hasBattery);
    log_d("Max Charge Power: %d W", inverterData.maxChargePowerW);
    log_d("Max Discharge Power: %d W", inverterData.maxDischargePowerW);
    if (inverterData.inverterTime > 0) {
        struct tm timeinfo;
        localtime_r(&inverterData.inverterTime, &timeinfo);
        log_d("Inverter Time: %04d-%02d-%02d %02d:%02d:%02d", 
              timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
              timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    }
} 
