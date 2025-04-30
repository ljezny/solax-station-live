#pragma once
#include <Arduino.h>
#include "Shelly/Shelly.hpp"
#include <mdns.h>

#define SOFT_AP_SSID_PREFIX String("SolarStationLive-")

class SoftAP
{
public:
    String getSSID()
    {
        return SOFT_AP_SSID_PREFIX + getESPIdHex();
    }

    static String getPassword()
    {
        return getESPIdHex();
    }

    void start()
    {
        log_d("Starting SoftAP");
        //do NOT use a hidden SSID, it will not work for Shelly, some devices has connection issues with hidden SSID
        WiFi.softAP(getSSID().c_str(), getPassword().c_str(), 10, 0, MAX_SHELLY_PAIRS);
    }

    int getNumberOfConnectedDevices()
    {
        return WiFi.softAPgetStationNum();
    }

    static String getESPIdHex()
    {
        char idHex[23];
        snprintf(idHex, 23, "%llX", ESP.getEfuseMac());
        return idHex;
    }

private:
};