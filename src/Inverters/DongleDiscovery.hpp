#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>

#include "DongleResult.hpp"
#include "utils/SoftAP.hpp"
#include "Shelly/Shelly.hpp"

#define DONGLE_DISCOVERY_MAX_RESULTS 32

#define DONGLE_DISCOVERY_PREFERENCES_KEY "dongle_storage"

typedef struct
{
    char password[32] = {0};
    char dongleIp[16] = {0};                                // IP address of the dongle, if available
    ConnectionType_t connectionType = CONNECTION_TYPE_NONE; // Type of the dongle
} DongleInfo_t;

class WiFiDiscovery
{
public:
    WiFiDiscoveryResult_t discoveries[DONGLE_DISCOVERY_MAX_RESULTS];
    int preferedInverterWifiDongleIndex = -1;

    void scanWiFi(bool fast = false)
    {
        memset(discoveries, 0, sizeof(discoveries));

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
            DongleInfo_t dongleInfo;
            loadDongleInfo(ssid, dongleInfo);
            discoveries[i].type = dongleInfo.connectionType;
            if( discoveries[i].type == CONNECTION_TYPE_NONE)
            {
                discoveries[i].type = preferredConnectionType(ssid);
            }
            discoveries[i].inverterIP = dongleInfo.dongleIp;
            discoveries[i].ssid = ssid;
            discoveries[i].password = dongleInfo.password;
            discoveries[i].requiresPassword = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
            discoveries[i].signalPercent = wifiSignalPercent(WiFi.RSSI(i));

            if (discoveries[i].requiresPassword)
            {
                discoveries[i].password = dongleInfo.password;

                if (discoveries[i].type == CONNECTION_TYPE_GOODWE && discoveries[i].password.isEmpty())
                {
                    discoveries[i].password = "12345678";
                }
            }

            // Dalibor Farny - Victron
            if (discoveries[i].ssid == "venus-HQ22034YWXC-159")
            {
                discoveries[i].password = "uarnb5xs";
            }
        }
    }

    bool disconnect()
    {
        log_d("Disconnecting from WiFi");
        return WiFi.disconnect();
    }

    bool connectToDongle(WiFiDiscoveryResult_t &discovery)
    {
        if (discovery.type == CONNECTION_TYPE_NONE)
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

            DongleInfo_t dongleInfo;
            loadDongleInfo(discovery.ssid, dongleInfo);
            strcpy(dongleInfo.password, discovery.password.c_str());
            saveDongleInfo(discovery.ssid, dongleInfo);
        }
        else
        {
            log_d("Failed to connect to %s", discovery.ssid.c_str());

            WiFi.begin(discovery.ssid.c_str(), SoftAP::getPassword().c_str());
            WiFi.setSleep(false);

            connectionResult = awaitWifiConnection();

            if (connectionResult)
            {
                log_d("Connected to %s with default password", discovery.ssid.c_str());

                DongleInfo_t dongleInfo;
                loadDongleInfo(discovery.ssid, dongleInfo);
                strcpy(dongleInfo.password, SoftAP::getPassword().c_str());
                saveDongleInfo(discovery.ssid, dongleInfo);
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
            if (discoveries[i].type == CONNECTION_TYPE_NONE)
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

    String getDongleTypeName(ConnectionType_t type)
    {
        switch (type)
        {
        case CONNECTION_TYPE_SOLAX:
            return "Solax";
        case CONNECTION_TYPE_GOODWE:
            return "GoodWe";
        case CONNECTION_TYPE_SOFAR:
            return "Sofar";
        case CONNECTION_TYPE_DEYE:
            return "DEYE";
        case CONNECTION_TYPE_VICTRON:
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

    ConnectionType_t preferredConnectionType(String ssid)
    {
        if (ssid.startsWith("Wifi_"))
        {
            return CONNECTION_TYPE_SOLAX;
        }
        else if (ssid.startsWith("Solar-WiFi"))
        {
            return CONNECTION_TYPE_GOODWE;
        }
        else if (ssid.startsWith("AP_"))
        {
            return CONNECTION_TYPE_SOFAR;
        }
        else if (ssid.startsWith("venus-"))
        {
            return CONNECTION_TYPE_VICTRON;
        }
        else if (ssid.startsWith("Deye-"))
        {
            return CONNECTION_TYPE_DEYE;
        }
        else
        {
            return CONNECTION_TYPE_NONE;
        }
    }

    String parseDongleSN(String ssid)
    {
        String sn = ssid;
        sn.replace("Wifi_", "");
        sn.replace("Solar-WiFi", "");
        sn.replace("AP_", "");
        sn.replace("venus-", "");

        for (int i = 0; i < SHELLY_SUPPORTED_MODEL_COUNT; i++)
        {
            sn.replace(supportedModels[i].prefix, "");
        }
        return sn;
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

    void saveDongleInfo(String ssid, DongleInfo_t &info)
    {
        Preferences preferences;
        preferences.begin(DONGLE_DISCOVERY_PREFERENCES_KEY, false);
        preferences.putBytes(hashString(ssid).c_str(), (void *)&info, sizeof(DongleInfo_t));
        preferences.end();
    }

    bool loadDongleInfo(String ssid, DongleInfo_t &info)
    {
        Preferences preferences;
        preferences.begin(DONGLE_DISCOVERY_PREFERENCES_KEY, true);
        String key = hashString(ssid);
        bool result = false;
        if (!preferences.isKey(key.c_str()))
        {
            log_d("No dongle info found for %s", ssid.c_str());
        }
        else
        {
            size_t len = preferences.getBytesLength(key.c_str());
            if (len == sizeof(DongleInfo_t))
            {
                preferences.getBytes(key.c_str(), (void *)&info, sizeof(DongleInfo_t));
                result = true;
                log_d("Loaded dongle info for %s", ssid.c_str());
            }
            else
            {
                log_d("Dongle info for %s has invalid length: %d", ssid.c_str(), len);
            }
        }
        preferences.end();
        return result;
    }
};