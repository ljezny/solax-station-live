#pragma once
#include <climits>
#include <ctime>

typedef enum WallboxType
{
    WALLBOX_TYPE_UNKNOWN = 0,
    WALLBOX_TYPE_SOLAX = 1,
    WALLBOX_TYPE_ECOVOLTER_PRO_V2 = 2
} WallboxType_t;

typedef struct WallboxResult
{
    long updated = 0;
    WallboxType_t type = WALLBOX_TYPE_UNKNOWN;
    bool evConnected = false;
    int chargingPower = 0;
    int chargingCurrent = 0;
    int maxChargingCurrent = 0;
    int targetChargingCurrent = 0;
    float totalChargedEnergy = 0;
    float chargedEnergy = 0;
    bool chargingControlEnabled = false;
    int phases = 3;
    int voltageL1 = 0;
    int voltageL2 = 0;
    int voltageL3 = 0;
    int currentL1 = 0;
    int currentL2 = 0;
    int currentL3 = 0;
    int temperature = 0;
} WallboxResult_t;

typedef enum WallboxChargingRequestState
{
    CHARGING_REQUEST_STATE_STOP_IMMEDIATELY = INT_MIN,
    CHARGING_REQUEST_STATE_START_AT_MAX = INT_MAX,
    CHARGING_REQUEST_STATE_NO_CHANGE = 0,
    CHARGING_REQUEST_STATE_SLOWER = -1,
    CHARGING_REQUEST_STATE_FASTER = 1
} WallboxChargingRequestState_t;

void logWallboxResult(WallboxResult_t &result);