#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>

#include "DongleResult.hpp"
#include "utils/SoftAP.hpp"
#include "Shelly/Shelly.hpp"

#define DONGLE_DISCOVERY_MAX_RESULTS 10

#define DONGLE_DISCOVERY_PREFERENCES_KEY "dongleDiscovery"

class DongleDiscovery
{
public:
    DongleDiscoveryResult_t discoveries[DONGLE_DISCOVERY_MAX_RESULTS];
    int preferedInverterWifiDongleIndex = -1;

    bool discoverDongle(bool fast = false)
    {
        bool result = false;

        int found = WiFi.scanNetworks(false, false, false, fast ? 100 : 300);

        for (int i = 0; i < found; i++)
        {
            log_d("Found network: %s", WiFi.SSID(i).c_str());
            String ssid = WiFi.SSID(i);
            if (ssid.length() == 0)
            {
                log_d("Empty SSID");
                continue;
            }
            DongleType_t type = getTypeFromSSID(ssid);

            if (type == DONGLE_TYPE_UNKNOWN)
            {
                log_d("Unknown dongle type");
                continue;
            }

            int discoveryIndex = -1;

            // find if existing in sparse array
            for (int j = 0; j < DONGLE_DISCOVERY_MAX_RESULTS; j++)
            {
                if (ssid.equals(discoveries[j].ssid))
                {
                    discoveryIndex = j;
                    break;
                }
            }

            if (discoveryIndex != -1)
            {
                log_d("Already discovered this dongle");
                continue;
            }

            // find empty slot
            for (int j = 0; j < DONGLE_DISCOVERY_MAX_RESULTS; j++)
            {
                if (discoveries[j].type == DONGLE_TYPE_UNKNOWN)
                {
                    discoveryIndex = j;
                    break;
                }
            }

            if (discoveryIndex == -1)
            {
                log_d("No more space for discovery results");
                continue;
            }

            if (discoveries[discoveryIndex].type != DONGLE_TYPE_UNKNOWN)
            {
                log_d("Already discovered this dongle");
                discoveries[discoveryIndex].signalPercent = wifiSignalPercent(WiFi.RSSI(i)); // update signal strength
                continue;
            }

            discoveries[discoveryIndex].sn = parseDongleSN(ssid);
            discoveries[discoveryIndex].type = type;
            discoveries[discoveryIndex].ssid = ssid;
            discoveries[discoveryIndex].password = "";
            discoveries[discoveryIndex].requiresPassword = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
            discoveries[discoveryIndex].signalPercent = wifiSignalPercent(WiFi.RSSI(i));

            if (discoveries[discoveryIndex].requiresPassword)
            {
                discoveries[discoveryIndex].password = loadDonglePassword(ssid);

                if (discoveries[discoveryIndex].type == DONGLE_TYPE_GOODWE && discoveries[discoveryIndex].password.isEmpty())
                {
                    discoveries[discoveryIndex].password = "12345678";
                }
            }

            // Dalibor Farny - Victron
            if (discoveries[discoveryIndex].ssid == "venus-HQ22034YWXC-159")
            {
                discoveries[discoveryIndex].password = "uarnb5xs";
            }

            result = true;
        }

        return true;
    }

    bool connectToDongle(DongleDiscoveryResult_t &discovery)
    {
        if (discovery.type == DONGLE_TYPE_UNKNOWN)
        {
            return false;
        }

        if (WiFi.SSID() == discovery.ssid)
        {
            log_d("Already connected to %s", discovery.ssid.c_str());
            return awaitWifiConnection();
        }
        else
        {
            log_d("Disconnecting from %s", WiFi.SSID().c_str());
            WiFi.disconnect();
        }

        WiFi.persistent(false);

        log_d("Connecting to %s", discovery.ssid.c_str());
        WiFi.begin(discovery.ssid.c_str(), discovery.password.c_str());
        WiFi.setSleep(false);
        bool connectionResult = awaitWifiConnection();

        if (connectionResult)
        {
            log_d("Connected to %s", discovery.ssid.c_str());

            saveDonglePassword(discovery.ssid, discovery.password);
        }
        else
        {
            log_d("Failed to connect to %s", discovery.ssid.c_str());

            clearDonglePassword(discovery.ssid);

            WiFi.begin(discovery.ssid.c_str(), SoftAP::getPassword().c_str());
            WiFi.setSleep(false);

            connectionResult = awaitWifiConnection();

            if (connectionResult)
            {
                log_d("Connected to %s with default password", discovery.ssid.c_str());
                saveDonglePassword(discovery.ssid, SoftAP::getPassword());
            }
        }

        return connectionResult;
    }

    void trySelectPreferedInverterWifiDongleIndex()
    {
        if (preferedInverterWifiDongleIndex != -1)
        {
            return;
        }

        // select first found inverter dongle, but only when there is only one
        int preferred = -1;
        for (int i = 0; i < DONGLE_DISCOVERY_MAX_RESULTS; i++)
        {
            if (discoveries[i].type == DONGLE_TYPE_UNKNOWN)
            {
                continue;
            }
            if (discoveries[i].type == DONGLE_TYPE_SHELLY) //skip shelly
            {
                continue;
            }
            if (!discoveries[i].requiresPassword || (discoveries[i].requiresPassword && !discoveries[i].password.isEmpty()))
            {
                log_d("Prefered dongle: %s", discoveries[i].ssid.c_str());
                if (preferred == -1)
                {
                    preferred = i;
                }
                else
                {
                    log_d("Multiple dongles found, no prefered one");
                    preferred = -1;
                    break;
                }
            }
        }
        this->preferedInverterWifiDongleIndex = preferred;
    }

    String getDongleTypeName(DongleType_t type)
    {
        switch (type)
        {
        case DONGLE_TYPE_SOLAX:
            return "Solax";
        case DONGLE_TYPE_GOODWE:
            return "GoodWe";
        case DONGLE_TYPE_SOFAR:
            return "Sofar";
        case DONGLE_TYPE_SHELLY:
            return "Shelly";
        case DONGLE_TYPE_VICTRON:
            return "Victron";
        default:
            return "Unknown";
        }
    }

private:
    bool awaitWifiConnection()
    {
        int retries = 100;
        for (int r = 0; r < retries; r++)
        {
            if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0))
            {
                log_d("Connected to WiFi: %s", WiFi.SSID().c_str());
                log_d("IP Address: %s", WiFi.localIP().toString().c_str());
                log_d("RSSI: %d", WiFi.RSSI());
                log_d("Signal strength: %d%%", wifiSignalPercent(WiFi.RSSI()));
            
                return true;
            }
            else
            {
                delay(100);
            }
        }
        WiFi.disconnect();
        log_d("Failed to connect to WiFi");

        return false;
    }

    bool isSolaxDongleSSID(String ssid)
    {
        return ssid.startsWith("Wifi_");
    }

    bool isGoodWeSSID(String ssid)
    {
        return ssid.startsWith("Solar-WiFi");
    }

    bool isSofarSolarSID(String ssid)
    {
        return ssid.startsWith("AP_");
    }

    bool isShellySSID(String ssid)
    {
        for(int i = 0; i < SHELLY_SUPPORTED_MODEL_COUNT; i++)
        {
            if(ssid.startsWith(supportedModels[i].prefix))
            {
                return true;
            }
        }
        return false;
    }

    bool isVictronSSID(String ssid)
    {
        return ssid.startsWith("venus-");
    }

    String parseDongleSN(String ssid)
    {
        String sn = ssid;
        sn.replace("Wifi_", "");
        sn.replace("Solar-WiFi", "");
        sn.replace("AP_", "");
        sn.replace("venus-", "");
        
        for(int i = 0; i < SHELLY_SUPPORTED_MODEL_COUNT; i++)
        {
            sn.replace(supportedModels[i].prefix, "");
        }
        return sn;
    }

    DongleType_t getTypeFromSSID(String ssid)
    {
        if (isSolaxDongleSSID(ssid))
        {
            return DONGLE_TYPE_SOLAX;
        }
        if (isGoodWeSSID(ssid))
        {
            return DONGLE_TYPE_GOODWE;
        }
        if (isSofarSolarSID(ssid))
        {
            return DONGLE_TYPE_SOFAR;
        }
        if (isShellySSID(ssid))
        {
            return DONGLE_TYPE_SHELLY;
        }
        if (isVictronSSID(ssid))
        {
            return DONGLE_TYPE_VICTRON;
        }
        return DONGLE_TYPE_UNKNOWN;
    }

    int wifiSignalPercent(int rssi)
    {
        if (rssi <= -100)
        {
            return 0;
        }
        else if (rssi >= -50)
        {
            return 100;
        }
        else
        {
            return 2 * (rssi + 100);
        }
    }

    String hashString(String str)
    {
        uint32_t hash = 0;
        for (int i = 0; i < str.length(); i++)
        {
            hash = (hash << 5) + hash + str[i];
        }
        String result = String(hash, HEX);
        log_d("Data: %s, Hash: %s", str.c_str(), result.c_str());
        return result;
    }

    void saveDonglePassword(String ssid, String password)
    {
        Preferences preferences;
        preferences.begin(DONGLE_DISCOVERY_PREFERENCES_KEY, false);
        preferences.putString(hashString(ssid).c_str(), password);
        preferences.end();
    }

    String loadDonglePassword(String ssid)
    {
        Preferences preferences;
        preferences.begin(DONGLE_DISCOVERY_PREFERENCES_KEY, true);
        String password = preferences.getString(hashString(ssid).c_str(), "");
        preferences.end();
        return password;
    }

    void clearDonglePassword(String ssid)
    {
        Preferences preferences;
        preferences.begin(DONGLE_DISCOVERY_PREFERENCES_KEY, false);
        preferences.remove(hashString(ssid).c_str());
        preferences.end();
    }
};