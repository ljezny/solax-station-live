
#ifndef ShellyAPI_h
#define ShellyAPI_h

#include <WiFi.h>
#include <RemoteLogger.hpp>
#include <Preferences.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include "../utils/urlencoder.hpp"
#include <mdns.h>
#include "utils/SmartControlRuleResolver.hpp"
#include <StreamUtils.h>
#include <algorithm>

#define MAX_SHELLY_PAIRS 8

typedef enum
{
    PLUG_S,
    PLUG,
    PRO1PM,
    PLUS_PLUG_S,
    PLUS1PM,
    PRO3,
    PRODM1PM,
    DIMG3,
    MINIG3,
} ShellyModel_t;

typedef struct ShellyModelInfo
{
    ShellyModel_t model;
    String prefix;
} ShellyModelInfo_t;

#define SHELLY_SUPPORTED_MODEL_COUNT 13
const ShellyModelInfo_t supportedModels[SHELLY_SUPPORTED_MODEL_COUNT] = {
    {PLUG_S, "shellyplug-s-"},
    {PLUG, "shellyplug-"},
    {PLUS_PLUG_S, "ShellyPlusPlugS-"},
    {PLUS_PLUG_S, "PlusPlugS-"},
    {PRO1PM, "ShellyPro1PM-"},
    {PRO1PM, "Pro1PM-"}, //???
    {PLUS1PM, "ShellyPlus1PM-"},
    {PLUS1PM, "Plus1PM-"}, //????
    {PRO3, "ShellyPro3-"},
    {PRO3, "Pro3-"},
    {PRODM1PM, "ShellyProDM1PM-"},
    {DIMG3, "Shelly0110DimG3-"},
    {MINIG3, "Shelly1MiniG3-"}};

typedef struct ShellyStateResult
{
    int updated = 0;
    ShellyModel_t model = PLUG_S;
    bool isOn = false;
    int percent = 0;
    String source = "init";
    int totalPower = 0;
    int totalEnergy = 0;
    int signalPercent = 0;
} ShellyStateResult_t;

typedef struct ShellyPair
{
    unsigned long long shellyId = 0;
    IPAddress ip = INADDR_NONE;
    ShellyModel_t model = PLUG_S;
    ShellyStateResult_t lastState;
} ShellyPair_t;
typedef struct ShellyResult
{
    int pairedCount = 0;
    int activeCount = 0;
    int maxPercent = 0;
    int totalPower = 0;
    int totalEnergy = 0;
} ShellyResult_t;

class ShellyAPI
{
public:
    ShellyPair_t pairs[MAX_SHELLY_PAIRS] = {};

    ShellyAPI()
    {
        for (int i = 0; i < MAX_SHELLY_PAIRS; i++)
        {
            pairs[i].shellyId = 0;
            pairs[i].ip = INADDR_NONE;
            pairs[i].model = PLUG_S;
        }
    }

    bool isShellySSID(String ssid)
    {
        for (int i = 0; i < SHELLY_SUPPORTED_MODEL_COUNT; i++)
        {
            if (ssid.startsWith(supportedModels[i].prefix))
            {
                return true;
            }
        }
        return false;
    }

    void pairShelly(String shellySSId, String ssid, String password)
    {
        WiFi.begin(shellySSId.c_str());

        int retries = 100;
        for (int r = 0; r < retries; r++)
        {
            if (WiFi.status() == WL_CONNECTED)
            {
                LOGD("Connected to Shelly AP: %s", shellySSId.c_str());
                setWiFiSTA(shellySSId, ssid, password);
                break;
            }
            else
            {
                delay(100);
            }
        }

        WiFi.disconnect();
    }

    bool sendRequest(WiFiClient &client, IPAddress ipAddress, String method, String path, String requestBody)
    {
        LOGD("IP: %s", ipAddress.toString().c_str());
        if (client.connect(ipAddress, 80, 3000))
        {
            String request = "";
            request += method + " ";
            request += path;
            request += " HTTP/1.1\r\n";
            request += "Host: ";
            request += ipAddress.toString();
            request += "\r\n";
            request += "Connection: close\r\n";
            if (!requestBody.isEmpty())
            {
                request += "Content-Length: " + String(requestBody.length()) + "\r\n";
                request += "Content-Type: application/json\r\n";
            }
            request += "\r\n";
            if (!requestBody.isEmpty())
            {
                request += requestBody;
            }
            LOGD("Request: %s", request.c_str());
            client.write(request.c_str(), request.length());

            for (int i = 0; i < 50; i++)
            {
                if (client.available())
                {
                    break;
                }
                delay(10);
            }

            String headerLine = client.readStringUntil('\n');
            LOGD("Response code: %s", headerLine.c_str());
            if (headerLine.startsWith("HTTP/1.1 200 OK"))
            {
                while (!headerLine.isEmpty())
                {
                    headerLine = client.readStringUntil('\n');
                    headerLine.trim();
                }
                return true;
            }
        }
        else
        {
            LOGE("Failed to connect");
        }

        return false;
    }

    static inline bool inSameSubnet(IPAddress ip, IPAddress base, IPAddress mask)
    {
        uint32_t ip_u = (uint32_t)ip;
        uint32_t base_u = (uint32_t)base;
        uint32_t mask_u = (uint32_t)mask;
        return (ip_u & mask_u) == (base_u & mask_u);
    }

    void queryMDNS(IPAddress softAPIP, IPAddress softAPSubnet)
    {
        mdns_result_t *results = NULL;

        if (mdns_init() != ESP_OK)
        {
            LOGE("Failed starting MDNS");
            return;
        }

        if (mdns_query_ptr("_http", "_tcp", 3000, 20, &results) != ESP_OK)
        {
            LOGE("Failed to query MDNS");
            mdns_free();
            return;
        }

        uint8_t numResult = 0;

        mdns_result_t *r = results;
        LOGD("SoftAP IP: %s, Subnet: %s", softAPIP.toString().c_str(), softAPSubnet.toString().c_str());
        while (r)
        {
            const char *rawHost = r->hostname;
            if (!rawHost || !*rawHost)
            {
                LOGW("mDNS result with null/empty hostname; skipping");
                r = r->next;
                continue;
            }
            String hostname(rawHost);
            LOGD("Found service: %s", hostname.c_str());
            // check null
            if (r->addr == nullptr)
            {
                LOGW("mDNS result with null address; skipping");
                r = r->next;
                continue;
            }
            if (r->addr->addr.type != ESP_IPADDR_TYPE_V4)
            {
                LOGW("mDNS result with non-IPv4 address; skipping");
                r = r->next;
                continue;
            }

            IPAddress ipAddress = r->addr->addr.u_addr.ip4.addr;
            // check if IP is in the same subnet as softAP
            if (inSameSubnet(ipAddress, softAPIP, softAPSubnet))
            {
                LOGD("Found Shelly on softAP subnet: %s", hostname.c_str());
                hostname.toLowerCase();
                for (int i = 0; i < SHELLY_SUPPORTED_MODEL_COUNT; i++)
                {
                    String prefix = supportedModels[i].prefix;
                    prefix.toLowerCase();
                    if (hostname.startsWith(prefix))
                    {
                        LOGD("Found Shelly: %s model: %s", hostname.c_str(), supportedModels[i].prefix.c_str());
                        String idText = hostname.substring(prefix.length());
                        unsigned long long shellyId = strtoull(idText.c_str(), NULL, 16);
                        for (int j = 0; j < MAX_SHELLY_PAIRS; j++)
                        {
                            LOGD("Checking Shelly %s", String(pairs[j].shellyId, HEX).c_str());
                            if (pairs[j].shellyId == 0 || pairs[j].shellyId == shellyId)
                            {
                                pairs[j].shellyId = shellyId;
                                pairs[j].ip = r->addr->addr.u_addr.ip4.addr;
                                pairs[j].model = supportedModels[i].model;
                                LOGD("Paired Shelly %s", String(shellyId, HEX).c_str());
                                break;
                            }
                        }
                        break;
                    }
                }
            }

            r = r->next;
        }
        mdns_query_results_free(results);
        mdns_free();
        LOGD("Paired Shelly count: %d", getPairedCount());
    }

    ShellyResult_t getState()
    {
        ShellyResult_t result;
        for (int i = 0; i < MAX_SHELLY_PAIRS; i++)
        {
            if (pairs[i].shellyId != 0 && pairs[i].ip != INADDR_NONE)
            {
                LOGD("Getting state for Shelly %s", String(pairs[i].shellyId, HEX).c_str());
                result.pairedCount++;

                ShellyStateResult_t state = getState(pairs[i]);
                if (state.updated != 0)
                {
                    pairs[i].lastState = state;
                }
                else
                {
                    LOGE("Failed to get state for Shelly %s", String(pairs[i].shellyId, HEX).c_str());
                }

                if (pairs[i].lastState.isOn)
                {
                    result.activeCount++;
                }
                result.totalPower += state.totalPower;
                result.totalEnergy += state.totalEnergy;
                result.maxPercent = max(result.maxPercent, state.percent);

                if ((millis() - pairs[i].lastState.updated) > 5 * 60 * 1000)
                {
                    LOGW("Shelly %s state is outdated", String(pairs[i].shellyId, HEX).c_str());
                    pairs[i].lastState = ShellyStateResult_t();
                    pairs[i].ip = INADDR_NONE;
                    pairs[i].shellyId = 0;
                }
            }
        }
        return result;
    }

    void updateState(RequestedSmartControlState_t requestedState, int timeoutSec)
    {
        LOGD("Updating state to %d", requestedState);
        for (int i = 0; i < MAX_SHELLY_PAIRS; i++)
        {
            if (pairs[i].shellyId != 0 && pairs[i].ip != INADDR_NONE)
            {
                ShellyStateResult_t state = pairs[i].lastState;
                if (state.updated != 0)
                {
                    bool canBeControlled = ((state.isOn && (state.source == NULL || String("http").equals(state.source) || String("timer").equals(state.source) || String("init").equals(state.source) || String("WS_in").equals(state.source))) || !state.isOn);
                    if (canBeControlled)
                    {
                        bool wasOn = state.isOn;

                        setState(pairs[i], requestedState, timeoutSec);

                        if (!wasOn && requestedState == SMART_CONTROL_FULL_ON)
                        {
                            break; // activete only one relay
                        }
                    }
                    else
                    {
                        LOGW("Shelly %s cannot be controlled", String(pairs[i].shellyId, HEX).c_str());
                    }
                }
            }
        }
    }

    bool setWiFiSTA(String shellyAPSsid, String ssid, String password)
    {
        switch (getModelFromSSID(shellyAPSsid))
        {
        case PLUG:
        case PLUG_S:
            setup_Gen1();
            disableCloud_Gen1();
            return setWiFiSTA_Gen1(ssid, password);
        case PRO1PM:
        case PLUS_PLUG_S:
        case PLUS1PM:
        case PRO3:
        case PRODM1PM:
        case DIMG3:
        case MINIG3:
            return setWiFiSTA_Gen2(ssid, password);
        }
        return false;
    }

    int getPairedCount()
    {
        int count = 0;
        for (int i = 0; i < MAX_SHELLY_PAIRS; i++)
        {
            if (pairs[i].shellyId != 0)
            {
                count++;
            }
        }
        return count;
    }

private:
    mdns_search_once_t *mdnsSearch = NULL;
    ShellyModel_t getModelFromSSID(String ssid)
    {
        for (int i = 0; i < SHELLY_SUPPORTED_MODEL_COUNT; i++)
        {
            if (ssid.startsWith(supportedModels[i].prefix))
            {
                return supportedModels[i].model;
            }
        }
        return PLUG;
    }

    void disableCloud_Gen1()
    {
        WiFiClient client;
        if (sendRequest(client, IPAddress(192, 168, 33, 1), "GET", "/settings/cloud?enabled=0", ""))
        {
        }
        client.stop();
    }

    void setup_Gen1()
    {
        WiFiClient client;
        if (sendRequest(client, IPAddress(192, 168, 33, 1), "GET", "/settings/coiot_enable=0&mqtt_enable=0&ap_roaming_enabled=0", ""))
        {
        }
        client.stop();
    }

    bool setWiFiSTA_Gen1(String ssid, String password)
    {
        bool result = false;
        String path = String("/settings/sta?enabled=true&ssid=") + urlencode(ssid) + "&key=" + urlencode(password);
        WiFiClient client;
        if (sendRequest(client, IPAddress(192, 168, 33, 1), "GET", path, ""))
        {
            result = true;
        }
        client.stop();
        return result;
    }

    bool setWiFiSTA_Gen2(String ssid, String password)
    {
        StaticJsonDocument<1024> doc;
        doc["id"] = 1;
        doc["method"] = "WiFi.SetConfig";
        doc["params"]["config"]["sta"]["ssid"] = ssid;
        doc["params"]["config"]["sta"]["pass"] = password;
        doc["params"]["config"]["sta"]["enable"] = true;
        doc["params"]["config"]["ap"]["enable"] = false;
        String requestBody;
        serializeJson(doc, requestBody);
        bool result = false;
        WiFiClient client;
        if (sendRequest(client, IPAddress(192, 168, 33, 1), "POST", "/rpc", requestBody))
        {
            result = true;
        }
        client.stop();
        return result;
    }

    ShellyStateResult_t getState(ShellyPair_t shellyPair)
    {
        ShellyStateResult_t result;
        result.model = shellyPair.model;
        switch (shellyPair.model)
        {
        case PLUG:
        case PLUG_S:
            result = getState_Gen1(shellyPair.ip);
            break;
        case PRO1PM:
        case PLUS_PLUG_S:
        case PLUS1PM:
        case PRO3:
        case MINIG3:
            result = getState_Gen2(shellyPair.ip, "switch");
            break;
        case PRODM1PM:
        case DIMG3:
            result = getState_Gen2(shellyPair.ip, "light");
            break;
        }
        return result;
    }

    ShellyStateResult_t getState_Gen1(IPAddress ipAddress)
    {
        ShellyStateResult_t result;
        result.updated = 0;
        WiFiClient client;
        if (sendRequest(client, ipAddress, "GET", "/status", ""))
        {
            DynamicJsonDocument doc(8192);
            ReadBufferingStream bufferingStream(client, 1024);
            LoggingStream loggingStream(bufferingStream, Serial);
            DeserializationError err = deserializeJson(doc, loggingStream);
            if (err)
            {
                LOGE("Failed to parse JSON");
            }
            else
            {
                result.updated = millis();
                result.isOn = doc["relays"][0]["ison"].as<bool>();
                result.source = doc["relays"][0]["source"].as<String>();
                result.totalPower = doc["meters"][0]["power"].as<int>();
                result.totalEnergy = doc["meters"][0]["total"].as<int>();
                result.signalPercent = min(max(2 * (doc["wifi_sta"]["rssi"].as<int>() + 100), 0), 100);
            }
        }
        client.stop();
        return result;
    }

    ShellyStateResult_t getState_Gen2(IPAddress ipAddress, String type)
    {
        ShellyStateResult_t result;
        result.updated = 0;
        WiFiClient client;
        if (sendRequest(client, ipAddress, "POST", "/rpc", "{\"id\":1,\"method\":\"Shelly.GetStatus\"}"))
        {
            DynamicJsonDocument doc(8192);
            ReadBufferingStream bufferingStream(client, 1024);
            LoggingStream loggingStream(bufferingStream, Serial);
            DeserializationError err = deserializeJson(doc, loggingStream);
            if (err)
            {
                LOGE("Failed to parse JSON");
            }
            else
            {
                result.updated = millis();
                DynamicJsonDocument resultDoc = doc;
                if (doc.containsKey("result"))
                {
                    resultDoc = doc["result"];
                }
                result.isOn = resultDoc[type + ":0"]["output"].as<bool>();
                result.totalPower = resultDoc[type + ":0"]["apower"].as<float>();
                result.source = resultDoc[type + ":0"]["source"].as<String>();
                result.totalEnergy = resultDoc[type + ":0"]["aenergy"]["total"].as<float>();
                if (result.isOn)
                {
                    result.percent = resultDoc[type + ":0"]["brightness"].as<int>();
                }
                result.signalPercent = min(max(2 * (resultDoc["wifi"]["rssi"].as<int>() + 100), 0), 100);
            }
        }
        client.stop();
        return result;
    }

    bool setState(ShellyPair_t shellyPair, RequestedSmartControlState_t requestedState, int timeoutSec)
    {
        bool result = true;

        switch (shellyPair.model)
        {
        case PLUG:
        case PLUG_S:
            if (requestedState == SMART_CONTROL_FULL_ON || (requestedState >= SMART_CONTROL_KEEP_CURRENT_STATE && shellyPair.lastState.isOn))
            {
                result = setState_Gen1(shellyPair.ip, true, timeoutSec);
            }
            break;
        case PRO1PM:
        case PLUS_PLUG_S:
        case PLUS1PM:
        case MINIG3:
            if (requestedState == SMART_CONTROL_FULL_ON || (requestedState >= SMART_CONTROL_KEEP_CURRENT_STATE && shellyPair.lastState.isOn))
            {
                result = setState_Gen2(shellyPair.ip, "relay", 0, true, timeoutSec);
            }
            break;
        case DIMG3:
        case PRODM1PM:
        {
            int step = 0;
            int percent = (shellyPair.lastState.isOn ? shellyPair.lastState.percent : 0);

            // be more precise with dimmer with 70-90% range
            int multiplier = 1;
            if (percent > 90)
            {
                multiplier = 3;
            }
            else if (percent < 70)
            {
                multiplier = 5;
            }

            switch (requestedState)
            {
            case SMART_CONTROL_FULL_OFF:
                step = -2;
                break;
            case SMART_CONTROL_PARTIAL_OFF:
                step = -1;
                break;
            case SMART_CONTROL_FULL_ON:
                step = 2;
                break;
            case SMART_CONTROL_PARTIAL_ON:
                step = 1;
                break;
            default:
                step = 0;
                break;
            }
            percent += step * multiplier;
            percent = min(max(percent, 0), 100);
            result = setState_Gen2(shellyPair.ip, "light", 0, percent > 0, timeoutSec, percent);
            break;
        }
        case PRO3:
            if (requestedState == SMART_CONTROL_FULL_ON || (requestedState >= SMART_CONTROL_KEEP_CURRENT_STATE && shellyPair.lastState.isOn))
            {
                result &= setState_Gen2(shellyPair.ip, "relay", 0, true, timeoutSec);
                result &= setState_Gen2(shellyPair.ip, "relay", 1, true, timeoutSec);
                result &= setState_Gen2(shellyPair.ip, "relay", 2, true, timeoutSec);
            }
            break;
        }

        return result;
    }

    bool setState_Gen1(IPAddress ipAddress, bool on, int timeoutSec)
    {
        bool result = false;
        String path = String("/relay/0?turn=") + (on ? "on" : "off");
        if (timeoutSec > 0)
        {
            path += "&timer=" + String(timeoutSec);
        }
        WiFiClient client;
        if (sendRequest(client, ipAddress, "GET", path, ""))
        {
            result = true;
        }
        client.stop();

        return result;
    }

    bool setState_Gen2(IPAddress ipAddress, String type, int index, bool on, int timeoutSec, int percent = -1)
    {
        bool result = false;
        String path = String("/") + type + "/" + String(index) + "?turn=" + (on ? "on" : "off");
        if (on && timeoutSec > 0)
        {
            path += "&timer=" + String(timeoutSec);
        }
        if (percent > 0)
        {
            path += "&brightness=" + String(percent);
        }
        WiFiClient client;
        if (sendRequest(client, ipAddress, "GET", path, ""))
        {
            result = true;
        }
        client.stop();

        return result;
    }
};

#endif