#pragma once
#include <Arduino.h>
#include "Shelly/Shelly.hpp"
#include <mdns.h>

#define SOFT_AP_SSID_PREFIX String("SolarStationLive-")
#define SOFT_AP_IDLE_TIMEOUT_MS (5 * 60 * 1000) // 5 minutes

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
        if (_isRunning) {
            log_d("SoftAP already running");
            return;
        }
        log_d("Starting SoftAP");
        int channel = selectBestChannelForSoftAP();
        log_d("SoftAP channel: %d", channel);
        //do NOT use a hidden SSID, it will not work for Shelly, some devices has connection issues with hidden SSID
        WiFi.softAP(getSSID().c_str(), getPassword().c_str(), channel, 0, MAX_SHELLY_PAIRS);
        _isRunning = true;
        _lastClientConnectedTime = millis();
    }

    void stop()
    {
        if (!_isRunning) {
            return;
        }
        log_d("Stopping SoftAP");
        WiFi.softAPdisconnect(true);
        _isRunning = false;
    }

    bool isRunning()
    {
        return _isRunning;
    }

    int getNumberOfConnectedDevices()
    {
        return WiFi.softAPgetStationNum();
    }

    // Call this periodically to manage SoftAP state
    // Returns true if SoftAP was stopped due to idle timeout
    bool manageIdleTimeout()
    {
        if (!_isRunning) {
            return false;
        }

        int connectedDevices = getNumberOfConnectedDevices();
        if (connectedDevices > 0) {
            _lastClientConnectedTime = millis();
            return false;
        }

        // No clients connected, check timeout
        if (millis() - _lastClientConnectedTime > SOFT_AP_IDLE_TIMEOUT_MS) {
            log_d("SoftAP idle timeout - no clients for 5 minutes, stopping");
            stop();
            return true;
        }
        return false;
    }

    static String getESPIdHex()
    {
        String s = String(ESP.getEfuseMac(), HEX);
        s.toUpperCase();
        return s;
    }

private:
    bool _isRunning = false;
    unsigned long _lastClientConnectedTime = 0;

    int selectBestChannelForSoftAP()
    {
        // If connected to WiFi, use the same channel to avoid interference
        if (WiFi.status() == WL_CONNECTED) {
            int staChannel = WiFi.channel();
            log_d("Using STA channel for SoftAP: %d", staChannel);
            return staChannel;
        }

        // Fallback: find least congested channel
        int count[14] = {0};
        int found = WiFi.scanNetworks();
        for (int i = 0; i < found; i++)
        {
            int channel = WiFi.channel(i);
            log_d("Found network: %s, channel: %d", WiFi.SSID(i).c_str(), channel);
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