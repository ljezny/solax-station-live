#pragma once

#include <Arduino.h>
#include <RemoteLogger.hpp>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <map>

#include "WiFiResult.hpp"
#include "utils/SoftAP.hpp"
#include "utils/FlashMutex.hpp"
#include "Shelly/Shelly.hpp"

#define DONGLE_DISCOVERY_MAX_RESULTS 32

#define DONGLE_DISCOVERY_PREFERENCES_KEY "discovery"

// Cache pro poslední uložené SSID - aby se nemuselo číst z flash
static String lastStoredSSIDCache = "";
static bool lastStoredSSIDCacheValid = false;

typedef struct
{
    char password[32] = {0};
    char dongleIp[16] = {0};                                // IP address of the dongle, if available
    char sn[32] = {0};                                      // Serial number of the dongle, if available
    ConnectionType_t connectionType = CONNECTION_TYPE_NONE; // Type of the dongle
} DongleInfo_t;

// Cache pro DongleInfo - write-through cache
static std::map<String, DongleInfo_t> dongleInfoCache;
static bool dongleInfoCacheLoaded = false;

class WiFiDiscovery
{
public:
    WiFiDiscoveryResult_t discoveries[DONGLE_DISCOVERY_MAX_RESULTS];

    void scanWiFi(bool fast = false)
    {
        int found = WiFi.scanNetworks(false, false, false, fast ? 100 : 300);
        int j = 0;
        
        // Načíst cache z NVS pokud ještě nebyla načtena
        ensureCacheLoaded();
        
        for (int i = 0; i < found; i++)
        {
            String ssid = WiFi.SSID(i);
            if (ssid.length() == 0)
            {
                continue;
            }
            // skip if already found
            bool alreadyFound = false;
            for (int k = 0; k < j; k++)
            {
                if (discoveries[k].ssid == ssid)
                {
                    alreadyFound = true;
                    break;
                }
            }
            if (alreadyFound)
            {
                continue;
            }

            DongleInfo_t dongleInfo;
            getDongleInfoFromCache(ssid, dongleInfo);
            discoveries[j].type = dongleInfo.connectionType;
            if (discoveries[j].type == CONNECTION_TYPE_NONE)
            {
                discoveries[j].type = preferredConnectionType(ssid);
            }
            discoveries[j].inverterIP = dongleInfo.dongleIp;
            discoveries[j].sn = dongleInfo.sn;

            if (discoveries[j].sn.isEmpty() && discoveries[j].type != CONNECTION_TYPE_NONE)
            {
                discoveries[j].sn = parseDongleSN(ssid);
            }
            discoveries[j].ssid = ssid;
            discoveries[j].password = dongleInfo.password;
            discoveries[j].requiresPassword = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
            discoveries[j].signalPercent = wifiSignalPercent(WiFi.RSSI(i));

            if (discoveries[j].requiresPassword)
            {
                discoveries[j].password = dongleInfo.password;

                if (discoveries[j].type == CONNECTION_TYPE_GOODWE && discoveries[i].password.isEmpty())
                {
                    discoveries[j].password = "12345678";
                }
                if (discoveries[j].type == CONNECTION_TYPE_GROWATT && discoveries[i].password.isEmpty())
                {
                    discoveries[j].password = "12345678";
                }
            }

            // Dalibor Farny - Victron
            if (discoveries[j].ssid == "venus-HQ22034YWXC-159")
            {
                discoveries[j].password = "uarnb5xs";
            }

            // Lukas Capka
            if (discoveries[j].ssid == "wifi.sosna")
            {
                LOGD("Found special SSID: %s", discoveries[j].ssid.c_str());
                discoveries[j].password = "1sosna2sosny";
                discoveries[j].type = CONNECTION_TYPE_GROWATT;
            }

            j++;
        }
    }

    WiFiDiscoveryResult_t getDiscoveryResult(String ssid)
    {
        for (int i = 0; i < DONGLE_DISCOVERY_MAX_RESULTS; i++)
        {
            if (discoveries[i].ssid == ssid)
            {
                return discoveries[i];
            }
        }
        WiFiDiscoveryResult_t emptyResult;
        return emptyResult;
    }

    bool disconnect()
    {
        return WiFi.disconnect();
    }

    bool connectToDongle(WiFiDiscoveryResult_t &discovery)
    {
        if (WiFi.SSID() == discovery.ssid)
        {
            return true;
        }
        else
        {
            WiFi.disconnect();
        }

        WiFi.persistent(false);

        WiFi.begin(discovery.ssid.c_str(), discovery.password.c_str());
        WiFi.setSleep(false);
        bool connectionResult = awaitWifiConnection();

        if (connectionResult)
        {

            DongleInfo_t dongleInfo;
            loadDongleInfo(discovery.ssid, dongleInfo);
            strcpy(dongleInfo.password, discovery.password.c_str());
            strcpy(dongleInfo.dongleIp, discovery.inverterIP.c_str());
            strcpy(dongleInfo.sn, discovery.sn.c_str());
            dongleInfo.connectionType = discovery.type;
            saveDongleInfo(discovery.ssid, dongleInfo);
        }

        return connectionResult;
    }

    WiFiDiscoveryResult_t getAutoconnectDongle()
    {
        WiFiDiscoveryResult_t autoconnectDongle;
        String lastConnectedSSID = loadLastConnectedSSID();
        if (!lastConnectedSSID.isEmpty())
        {
            WiFiDiscoveryResult_t lastConnectedResult = getDiscoveryResult(lastConnectedSSID);
            if (lastConnectedResult.type != CONNECTION_TYPE_NONE)
            {
                autoconnectDongle = lastConnectedResult;
            }
        }
        else
        {
            // try to find a prefered dongle
            int foundCount = 0;
            for (int i = 0; i < DONGLE_DISCOVERY_MAX_RESULTS; i++)
            {
                if (discoveries[i].type != CONNECTION_TYPE_NONE && !discoveries[i].ssid.isEmpty() &&
                    (!discoveries[i].requiresPassword || (discoveries[i].requiresPassword && !discoveries[i].password.isEmpty())))
                {
                    autoconnectDongle = discoveries[i];
                    foundCount++;
                }
            }

            if (foundCount > 1)
            {
                LOGD("Multiple prefered dongles found, no autoconnect");
                autoconnectDongle.type = CONNECTION_TYPE_NONE;
            }
            else if (foundCount == 0)
            {
                LOGD("No prefered dongle found, no autoconnect");
                autoconnectDongle.type = CONNECTION_TYPE_NONE;
            }
        }

        return autoconnectDongle;
    }

    bool isValid(WiFiDiscoveryResult_t &result)
    {
        bool isValid = true;
        isValid &= result.type != CONNECTION_TYPE_NONE;

        if (result.requiresPassword)
        {
            isValid &= !result.password.isEmpty();
        }

        if (result.type == CONNECTION_TYPE_DEYE || result.type == CONNECTION_TYPE_SOFAR)
        {
            isValid &= !result.sn.isEmpty();
        }

        return isValid;
    }

    String loadLastConnectedSSID()
    {
        FlashGuard guard("WiFi:loadSSID");
        if (!guard.isLocked()) {
            return "";  // Nelogovat - jsme v kritické sekci
        }
        
        Preferences preferences;
        preferences.begin(DONGLE_DISCOVERY_PREFERENCES_KEY, true);
        String ssid = preferences.getString("ssid", "");
        preferences.end();
        return ssid;
    }

    void storeLastConnectedSSID(const String &ssid)
    {
        // Rychlá kontrola cache - pokud je SSID stejné, nemusíme nic dělat
        if (lastStoredSSIDCacheValid && lastStoredSSIDCache == ssid) {
            return;  // Už máme uložené stejné SSID
        }
        
        FlashGuard guard("WiFi:storeSSID");
        if (!guard.isLocked()) {
            return;  // Nelogovat - jsme v kritické sekci
        }
        
        Preferences preferences;
        preferences.begin(DONGLE_DISCOVERY_PREFERENCES_KEY, false);
        String storedssid = preferences.getString("ssid", "");
        if (storedssid != ssid)
        {
            preferences.putString("ssid", ssid);
            // Nelogovat - jsme v kritické sekci
        }
        preferences.end();
        
        // Aktualizace cache
        lastStoredSSIDCache = ssid;
        lastStoredSSIDCacheValid = true;
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
        case CONNECTION_TYPE_GROWATT:
            return "Growatt";
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
                LOGD("WiFi: %s IP=%s RSSI=%d (%d%%)", 
                     WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), 
                     WiFi.RSSI(), wifiSignalPercent(WiFi.RSSI()));
                return true;
            }
            else
            {
                delay(100);
            }
        }
        WiFi.disconnect();
        LOGE("Failed to connect to WiFi");

        return false;
    }

    ConnectionType_t preferredConnectionType(String ssid)
    {
        if (ssid.startsWith("Wifi_"))
        {
            if (ssid.startsWith("Wifi_SQ"))
            { // seems to be a wallbox dongle, ignore it
                return CONNECTION_TYPE_NONE;
            }
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
        else if (ssid.startsWith("XGD"))
        {
            return CONNECTION_TYPE_GROWATT;
        }
        else
        {
            return CONNECTION_TYPE_NONE;
        }
    }

    String parseDongleSN(String ssid)
    {
        String sn = ssid;
        if (ssid.startsWith("Wifi_"))
        {
            sn.replace("Wifi_", ""); // wallbox dongle, keep SQ
            return sn;
        }
        else if (ssid.startsWith("Solar-WiFi-"))
        {
            sn.replace("Solar-WiFi-", ""); // Solar-WiFi dongle, keep the rest
            return sn;
        }
        else if (ssid.startsWith("AP_"))
        {
            sn.replace("AP_", ""); // AP dongle, keep the rest
            return sn;
        }
        else if (ssid.startsWith("venus-"))
        {
            sn.replace("venus-", ""); // venus dongle, keep the rest
            return sn;
        }

        for (int i = 0; i < SHELLY_SUPPORTED_MODEL_COUNT; i++)
        {
            if (sn.startsWith(supportedModels[i].prefix))
            {
                sn.replace(supportedModels[i].prefix, "");
                return sn;
            }
        }
        return ""; // unknown dongle, no SN
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
        return String(hash, HEX);
    }

    /**
     * Načte všechny DongleInfo z NVS do cache (volá se jednou)
     */
    void ensureCacheLoaded()
    {
        if (dongleInfoCacheLoaded) return;
        
        FlashGuard guard("Dongle:loadAll");
        if (!guard.isLocked()) {
            LOGE("Failed to lock NVS mutex for loading dongle cache");
            return;
        }
        
        // NVS nepodporuje iteraci přes klíče, takže cache se naplní postupně
        // při prvním přístupu ke každému SSID
        dongleInfoCacheLoaded = true;
        LOGD("DongleInfo cache initialized");
    }

    /**
     * Získá DongleInfo z cache (nebo načte z NVS pokud není v cache)
     */
    bool getDongleInfoFromCache(const String &ssid, DongleInfo_t &info)
    {
        // Zkusit cache
        auto it = dongleInfoCache.find(ssid);
        if (it != dongleInfoCache.end()) {
            info = it->second;
            return true;
        }
        
        // Není v cache - načíst z NVS
        FlashGuard guard("Dongle:load");
        if (!guard.isLocked()) {
            LOGE("Failed to lock NVS mutex for loading dongle info");
            return false;
        }
        
        Preferences preferences;
        preferences.begin(DONGLE_DISCOVERY_PREFERENCES_KEY, true);
        String key = hashString(ssid);
        bool result = false;
        if (preferences.isKey(key.c_str()))
        {
            size_t len = preferences.getBytesLength(key.c_str());
            if (len == sizeof(DongleInfo_t))
            {
                preferences.getBytes(key.c_str(), (void *)&info, sizeof(DongleInfo_t));
                dongleInfoCache[ssid] = info;  // Uložit do cache
                result = true;
            }
        }
        preferences.end();
        
        if (!result) {
            // Uložit prázdný záznam do cache aby se příště nehledalo v NVS
            dongleInfoCache[ssid] = DongleInfo_t();
        }
        
        return result;
    }

    void saveDongleInfo(String ssid, DongleInfo_t &info)
    {
        // Write-through: uložit do cache
        dongleInfoCache[ssid] = info;
        
        FlashGuard guard("Dongle:save");
        if (!guard.isLocked()) {
            LOGE("Failed to lock NVS mutex for saving dongle info");
            return;
        }

        Preferences preferences;
        preferences.begin(DONGLE_DISCOVERY_PREFERENCES_KEY, false);
        preferences.putBytes(hashString(ssid).c_str(), (void *)&info, sizeof(DongleInfo_t));
        preferences.end();
    }

    /**
     * Načte DongleInfo - používá cache
     */
    bool loadDongleInfo(String ssid, DongleInfo_t &info)
    {
        return getDongleInfoFromCache(ssid, info);
    }
};