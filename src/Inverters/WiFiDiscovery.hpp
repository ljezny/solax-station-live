#pragma once

#include <Arduino.h>
#include <RemoteLogger.hpp>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <map>
#include <esp_wifi.h>

#include "WiFiResult.hpp"
#include "utils/SoftAP.hpp"
#include "utils/FlashMutex.hpp"
#include "Shelly/Shelly.hpp"

#define DONGLE_DISCOVERY_MAX_RESULTS 32

#define DONGLE_DISCOVERY_PREFERENCES_KEY "discovery"

// Cache pro poslední uložené SSID - aby se nemuselo číst z flash
static String lastStoredSSIDCache = "";
static bool lastStoredSSIDCacheValid = false;

// Last WiFi disconnect reason for user feedback
static wifi_err_reason_t lastWiFiDisconnectReason = WIFI_REASON_UNSPECIFIED;
static bool wifiEventHandlerRegistered = false;

// Helper function to decode WiFi disconnect reason to user-friendly string
static const char* wifiDisconnectReasonToString(wifi_err_reason_t reason) {
    switch (reason) {
        case WIFI_REASON_AUTH_EXPIRE:
        case WIFI_REASON_AUTH_FAIL:
            return "Authentication failed";
        case WIFI_REASON_ASSOC_EXPIRE:
        case WIFI_REASON_ASSOC_FAIL:
            return "Association failed";
        case WIFI_REASON_NO_AP_FOUND:
            return "Network not found";
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            return "Handshake timeout";
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_GROUP_KEY_UPDATE_TIMEOUT:
            return "Wrong password?";
        case WIFI_REASON_BEACON_TIMEOUT:
            return "Beacon timeout";
        case WIFI_REASON_CONNECTION_FAIL:
            return "Connection failed";
        case WIFI_REASON_AP_TSF_RESET:
            return "AP reset";
        case WIFI_REASON_ROAMING:
            return "Roaming";
        default:
            return "Connection timeout";
    }
}

// WiFi event handler to capture disconnect reason
static void wifiEventHandler(WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        lastWiFiDisconnectReason = (wifi_err_reason_t)info.wifi_sta_disconnected.reason;
        LOGD("WiFi disconnected, reason: %d (%s)", 
             lastWiFiDisconnectReason, 
             wifiDisconnectReasonToString(lastWiFiDisconnectReason));
    }
}

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
    String lastConnectionError = "";  // User-friendly error message

    void scanWiFi(bool fast = false)
    {
        // Register WiFi event handler if not already done
        if (!wifiEventHandlerRegistered) {
            WiFi.onEvent(wifiEventHandler);
            wifiEventHandlerRegistered = true;
        }
        
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
        lastConnectionError = "";  // Reset error
        lastWiFiDisconnectReason = WIFI_REASON_UNSPECIFIED;
        
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
        else
        {
            // Store user-friendly error message
            lastConnectionError = wifiDisconnectReasonToString(lastWiFiDisconnectReason);
            LOGE("WiFi connection failed: %s (reason %d)", lastConnectionError.c_str(), lastWiFiDisconnectReason);
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

    /**
     * Checks if inverter is reachable (type-specific detection)
     * @param discovery The discovery result with connection type and IP
     * @return true if inverter responds, false otherwise
     */
    bool isInverterReachable(WiFiDiscoveryResult_t &discovery, String &detectedIP)
    {
        LOGD("Checking inverter reachability for type %d", discovery.type);
        detectedIP = "";
        
        switch (discovery.type) {
            case CONNECTION_TYPE_GOODWE:
                return isGoodweReachable(detectedIP);
            case CONNECTION_TYPE_VICTRON:
                return isVictronReachable(detectedIP);
            case CONNECTION_TYPE_SOLAX:
                return isTcpPortOpen(getDefaultIP(discovery), 502, detectedIP);
            case CONNECTION_TYPE_DEYE:
            case CONNECTION_TYPE_SOFAR:
                return isDeyeSofarReachable(detectedIP);
            case CONNECTION_TYPE_GROWATT:
                return isTcpPortOpen(getDefaultIP(discovery), 502, detectedIP);
            default:
                LOGW("Unknown connection type %d, cannot check reachability", discovery.type);
                return false;
        }
    }

private:
    /**
     * Tries to discover GoodWe dongle via UDP broadcast
     */
    bool isGoodweReachable(String &detectedIP)
    {
        LOGD("Discovering GoodWe via UDP broadcast on port 48899");
        WiFiUDP udp;
        String message = "WIFIKIT-214028-READ";
        
        udp.beginPacket(IPAddress(255, 255, 255, 255), 48899);
        udp.write((const uint8_t *)message.c_str(), (size_t)message.length());
        udp.endPacket();
        
        unsigned long start = millis();
        while (millis() - start < 3000) {
            int packetSize = udp.parsePacket();
            if (packetSize) {
                char d[128] = {0};
                udp.read(d, sizeof(d));
                
                int indexOfComma = String(d).indexOf(',');
                if (indexOfComma > 0) {
                    detectedIP = String(d).substring(0, indexOfComma);
                    LOGD("GoodWe found at %s", detectedIP.c_str());
                    udp.stop();
                    return true;
                }
            }
            delay(50);
        }
        udp.stop();
        LOGW("GoodWe not found via UDP broadcast");
        return false;
    }
    
    /**
     * Tries to discover Deye/Sofar dongle via UDP broadcast (same protocol as GoodWe)
     * These dongles (LSW-3, etc.) use identical discovery on port 48899
     */
    bool isDeyeSofarReachable(String &detectedIP)
    {
        LOGD("Discovering Deye/Sofar via UDP broadcast on port 48899");
        WiFiUDP udp;
        String message = "WIFIKIT-214028-READ";
        
        udp.beginPacket(IPAddress(255, 255, 255, 255), 48899);
        udp.write((const uint8_t *)message.c_str(), (size_t)message.length());
        udp.endPacket();
        
        unsigned long start = millis();
        while (millis() - start < 3000) {
            int packetSize = udp.parsePacket();
            if (packetSize) {
                char d[128] = {0};
                udp.read(d, sizeof(d));
                
                int indexOfComma = String(d).indexOf(',');
                if (indexOfComma > 0) {
                    detectedIP = String(d).substring(0, indexOfComma);
                    LOGD("Deye/Sofar dongle found at %s", detectedIP.c_str());
                    udp.stop();
                    return true;
                }
            }
            delay(50);
        }
        udp.stop();
        LOGW("Deye/Sofar dongle not found via UDP broadcast");
        return false;
    }
    
    /**
     * Tries to resolve Victron venus.local via mDNS or direct TCP
     */
    bool isVictronReachable(String &detectedIP)
    {
        LOGD("Checking Victron reachability via venus.local mDNS");
        
        // Try to resolve venus.local to IP
        IPAddress venusIP;
        if (WiFi.hostByName("venus.local", venusIP)) {
            detectedIP = venusIP.toString();
            LOGD("Victron resolved via mDNS: %s", detectedIP.c_str());
            return true;
        }
        
        // Fallback: try TCP connect to port 502
        LOGD("mDNS failed, trying TCP connect to venus.local:502");
        WiFiClient client;
        client.setTimeout(3000);
        if (client.connect("venus.local", 502)) {
            detectedIP = "venus.local";
            client.stop();
            LOGD("Victron reachable via TCP");
            return true;
        }
        
        LOGW("Victron not reachable");
        return false;
    }
    
    /**
     * Generic TCP port open check
     */
    bool isTcpPortOpen(IPAddress ip, uint16_t port, String &detectedIP)
    {
        if (ip == IPAddress(0, 0, 0, 0)) {
            LOGW("No IP address for TCP check");
            return false;
        }
        
        LOGD("TCP ping to %s:%d", ip.toString().c_str(), port);
        WiFiClient client;
        client.setTimeout(3000);
        
        if (client.connect(ip, port)) {
            detectedIP = ip.toString();
            client.stop();
            LOGD("TCP port %d open on %s", port, detectedIP.c_str());
            return true;
        }
        
        LOGW("TCP port %d not reachable on %s", port, ip.toString().c_str());
        return false;
    }
    
    /**
     * Get default IP for inverter type
     */
    IPAddress getDefaultIP(WiFiDiscoveryResult_t &discovery)
    {
        // First try user-specified IP
        if (!discovery.inverterIP.isEmpty()) {
            IPAddress ip;
            if (ip.fromString(discovery.inverterIP)) {
                return ip;
            }
        }
        
        // Use type-specific defaults for AP mode
        switch (discovery.type) {
            case CONNECTION_TYPE_SOLAX:
                // Solax AP mode uses either 192.168.10.10 or 5.8.8.8
                return (WiFi.localIP()[0] == 192) ? IPAddress(192, 168, 10, 10) : IPAddress(5, 8, 8, 8);
            case CONNECTION_TYPE_GROWATT:
                return IPAddress(192, 168, 10, 100);
            case CONNECTION_TYPE_DEYE:
            case CONNECTION_TYPE_SOFAR:
                // Deye/Sofar need mDNS or user-specified IP
                return IPAddress(0, 0, 0, 0);  // Will fail gracefully
            default:
                return IPAddress(0, 0, 0, 0);
        }
    }

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