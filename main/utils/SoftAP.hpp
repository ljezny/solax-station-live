#pragma once
#include <Arduino.h>
#include <mdns.h>
#include "../Shelly/Shelly.hpp"
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
        int channel = selectBestChannelForSoftAP();
        log_d("SoftAP channel: %d", channel);
        //do NOT use a hidden SSID, it will not work for Shelly, some devices has connection issues with hidden SSID
        WiFi.softAP(getSSID().c_str(), getPassword().c_str(), channel, 0, MAX_SHELLY_PAIRS);
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
    int selectBestChannelForSoftAP()
    {
        int count[14] = {0};
        int found = WiFi.scanNetworks();
        for (int i = 0; i < found; i++)
        {
            int channel = WiFi.channel(i);
            if (channel > 0 && channel < 14)
            {
                count[channel]++;
            }
        }
        int bestChannel = 1;
        int minCount = count[1];
        for (int i = 1; i < 14; i++)
        {
            if (count[i] < minCount)
            {
                minCount = count[i];
                bestChannel = i;
            }
        }
        log_d("Best channel for SoftAP: %d", bestChannel);
        return bestChannel;
    }
};