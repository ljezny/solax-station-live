#pragma once
#include <Arduino.h>
#include "Shelly/Shelly.hpp"
#include "ESPmDNS.h"

#define SOFT_AP_SSID_PREFIX String("SolarStationLive-")

class SoftAP
{
public:
    String getSSID()
    {
        return SOFT_AP_SSID_PREFIX + getESPIdHex();
    }
    
    String getPassword()
    {
        return getESPIdHex();
    }

    void ensureRunning()
    {
        if (WiFi.softAPSSID().isEmpty())
        {
            log_d("Starting SoftAP");
            WiFi.softAP(getSSID(), getPassword(), 1, 1, MAX_SHELLY_PAIRS);
            if (!MDNS.begin(getSSID().c_str()))
            {
                log_e("Error setting up MDNS responder!");
            }
        }
    }
    
    String getESPIdHex()
    {
        char idHex[23];
        snprintf(idHex, 23, "%llX", ESP.getEfuseMac());
        return idHex;
    }

private:
    
};