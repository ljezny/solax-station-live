#pragma once

#include <Arduino.h>

typedef enum {
    DONGLE_TYPE_SOLAX = 1,
    DONGLE_TYPE_GOODWE = 2,
    DONGLE_TYPE_UNKNOWN = 0,
} DongleType_t;

typedef struct {
    bool result = false;
    DongleType_t type = DONGLE_TYPE_UNKNOWN;
    String sn = "";
} DongleDiscoveryResult_t;