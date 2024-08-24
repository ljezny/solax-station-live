#pragma once

typedef enum DongleStatus {
    DONGLE_STATUS_OK = 1,
    DONGLE_STATUS_UNKNOWN = 0,
    DONGLE_STATUS_CONNECTION_ERROR = -1,
    DONGLE_STATUS_HTTP_ERROR = -2,
    DONGLE_STATUS_JSON_ERROR = -3,
    DONGLE_STATUS_WIFI_DISCONNECTED = -4,
} DongleStatus_t;

typedef struct
{
    DongleStatus_t status = DONGLE_STATUS_UNKNOWN;
    String sn;
    int pv1Power = 0;
    int pv2Power = 0;
    int pv3Power = 0;
    int pv4Power = 0;
    int soc = 0;
    int16_t batteryPower = 0;
    double batteryChargedToday = 0;
    double batteryDischargedToday = 0;
    double gridBuyToday = 0;
    double gridSellToday = 0;
    int L1Power = 0;
    int L2Power = 0;
    int L3Power = 0;
    int inverterPower = 0;
    int16_t loadPower = 0;
    float loadToday = 0;
    int32_t feedInPower = 0;
    int inverterTemperature = 0;
    int batteryTemperature = 0;
    double pvToday = 0;
    uint32_t pvTotal = 0;
} InverterData_t;

typedef struct
{
    DongleStatus_t status = DONGLE_STATUS_UNKNOWN;
    String sn;
    bool isConnected = false;
    bool isCharging = false;
    int power = 0;
} WallboxData_t;