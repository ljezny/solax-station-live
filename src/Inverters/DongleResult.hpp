#pragma once

#include <Arduino.h>

typedef enum {
    DONGLE_TYPE_UNKNOWN = 0,
    DONGLE_TYPE_IGNORE, //known dongle, but not supported (keep in list, but don't try to connect)
    DONGLE_TYPE_SOLAX,
    DONGLE_TYPE_GOODWE,
    DONGLE_TYPE_SOFAR,
    DONGLE_TYPE_VICTRON,
    DONGLE_TYPE_SHELLY,
} DongleType_t;

typedef struct {
    DongleType_t type = DONGLE_TYPE_UNKNOWN;
    String ssid = "";
    String sn = "";
    String password = "";
    bool requiresPassword = false;
    int signalPercent = 0;
} DongleDiscoveryResult_t;