#pragma once
#include <Arduino.h>
#include "Shelly/Shelly.hpp"

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

    void start()
    {
        log_d("Starting SoftAP");
        WiFi.softAP(getSSID(), getPassword(), 10, 0, MAX_SHELLY_PAIRS);
        
        // if (mdns_init()) {
        //     log_e("Failed starting MDNS");            
        // }

        // if (mdns_hostname_set(getSSID().c_str())) {
        //     log_e("Failed setting MDNS hostname");            
        // }
    }
    
    int getNumberOfConnectedDevices()
    {
        return WiFi.softAPgetStationNum();
    }

    String getESPIdHex()
    {
        char idHex[23];
        snprintf(idHex, 23, "%llX", ESP.getEfuseMac());
        return idHex;
    }
private:
    
};