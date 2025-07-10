#pragma once

#include <Arduino.h>

typedef enum {
    CONNECTION_TYPE_NONE = 0,
    CONNECTION_TYPE_SOLAX,
    CONNECTION_TYPE_GOODWE,
    CONNECTION_TYPE_SOFAR,
    CONNECTION_TYPE_VICTRON,
    CONNECTION_TYPE_DEYE,
} ConnectionType_t;

typedef struct {
    ConnectionType_t type = CONNECTION_TYPE_NONE;
    String ssid = "";
    String sn = "";
    String password = "";
    bool requiresPassword = false;
    int signalPercent = 0;
    String inverterIP = "";
} WiFiDiscoveryResult_t;
