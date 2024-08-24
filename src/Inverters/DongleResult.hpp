#pragma once

#include <Arduino.h>

typedef enum {
    DONGLE_TYPE_UNKNOWN = 0,
    DONGLE_TYPE_SOLAX_INVERTER,
    DONGLE_TYPE_SOLAX_WALLBOX,
    DONGLE_TYPE_GOODWE,
    DONGLE_TYPE_SHELLY,
} DongleType_t;

typedef struct {
    DongleType_t type = DONGLE_TYPE_UNKNOWN;
    String ssid = "";
    String sn = "";
} DongleDiscoveryResult_t;