
#ifndef ShellyAPI_h
#define ShellyAPI_h

#include <WiFi.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include "../utils/urlencoder.hpp"
#include <mdns.h>
#include "utils/ShellyRuleResolver.hpp"
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
    DIMG3
} ShellyModel_t;

typedef struct ShellyModelInfo
{
    ShellyModel_t model;
    String prefix;
} ShellyModelInfo_t;

#define SHELLY_SUPPORTED_MODEL_COUNT 12
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
    {DIMG3, "Shelly0110DimG3-"}
};


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
        http.setConnectTimeout(2000);
        http.setTimeout(2000);
        client.setTimeout(2000);
        for (int i = 0; i < MAX_SHELLY_PAIRS; i++)
        {
            pairs[i].shellyId = 0;
            pairs[i].ip = INADDR_NONE;
            pairs[i].model = PLUG_S;
        }
    }

    void pairShelly(String shellySSId, String ssid, String password)
    {
        WiFi.begin(shellySSId.c_str());

        int retries = 100;
        for (int r = 0; r < retries; r++)
        {
            if (WiFi.status() == WL_CONNECTED)
            {
                log_d("Connected to Shelly AP: %s", shellySSId.c_str());
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

    bool sendRequest(NetworkClient &client, IPAddress ipAddress, String method, String path, String requestBody)
    {
        log_d("IP: %s", ipAddress.toString().c_str());

        if (client.connect(ipAddress, 80, 5000))
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
            log_d("Request: %s", request.c_str());
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
            log_d("Response code: %s", headerLine.c_str());
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
            log_e("Failed to connect");
        }
        return false;
    }

    void queryMDNS()
    {
        mdns_result_t *results = NULL;

        if (mdnsSearch == NULL)
        {
            if (mdns_init() != ESP_OK)
            {
                log_e("Failed starting MDNS");
            }
            mdnsSearch = mdns_query_async_new(NULL, "_http", "_tcp", MDNS_TYPE_PTR, 30000, 20, NULL);
            if (mdnsSearch == NULL)
            {
                log_e("Failed to start mDNS search");
                return;
            }
        }
        uint8_t numResult = 0;
        if (mdns_query_async_get_results(mdnsSearch, 1000, &results, &numResult))
        {
            mdns_result_t *r = results;

            while (r)
            {
                String hostname = r->hostname;
                log_d("Found service: %s", hostname.c_str());
                hostname.toLowerCase();
                for (int i = 0; i < SHELLY_SUPPORTED_MODEL_COUNT; i++)
                {
                    String prefix = supportedModels[i].prefix;
                    prefix.toLowerCase();
                    if (hostname.startsWith(prefix))
                    {
                        log_d("Found Shelly: %s model: %s", hostname.c_str(), supportedModels[i].prefix.c_str());
                        String idText = hostname.substring(prefix.length());
                        unsigned long long shellyId = strtoull(idText.c_str(), NULL, 16);
                        for (int j = 0; j < MAX_SHELLY_PAIRS; j++)
                        {
                            log_d("Checking Shelly %s", String(pairs[j].shellyId, HEX).c_str());
                            if (pairs[j].shellyId == 0 || pairs[j].shellyId == shellyId)
                            {
                                pairs[j].shellyId = shellyId;
                                pairs[j].ip = r->addr->addr.u_addr.ip4.addr;
                                pairs[j].model = supportedModels[i].model;
                                log_d("Paired Shelly %s", String(shellyId, HEX).c_str());
                                break;
                            }
                        }
                        break;
                    }
                }
                r = r->next;
            }
            mdns_query_results_free(results);
            mdns_query_async_delete(mdnsSearch);
            mdnsSearch = NULL;

            mdns_free();
        }
    }

    ShellyResult_t getState()
    {
        ShellyResult_t result;
        for (int i = 0; i < MAX_SHELLY_PAIRS; i++)
        {
            if (pairs[i].shellyId != 0 && pairs[i].ip != INADDR_NONE)
            {
                log_d("Getting state for Shelly %s", String(pairs[i].shellyId, HEX).c_str());
                result.pairedCount++;

                ShellyStateResult_t state = getState(pairs[i]);
                if (state.updated != 0)
                {
                    pairs[i].lastState = state;
                }
                else
                {
                    log_e("Failed to get state for Shelly %s", String(pairs[i].shellyId, HEX).c_str());
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
                    log_w("Shelly %s state is outdated", String(pairs[i].shellyId, HEX).c_str());
                    pairs[i].lastState = ShellyStateResult_t();
                    pairs[i].ip = INADDR_NONE;
                    pairs[i].shellyId = 0;
                }
            }
        }
        return result;
    }

    void updateState(RequestedShellyState_t requestedState, int timeoutSec)
    {
        log_d("Updating state to %d", requestedState);
        for (int i = 0; i < MAX_SHELLY_PAIRS; i++)
        {
            if (pairs[i].shellyId != 0 && pairs[i].ip != INADDR_NONE)
            {
                ShellyStateResult_t state = pairs[i].lastState;
                if (state.updated != 0)
                {
                    bool canBeControlled = (state.isOn && (state.source == NULL || String("http").equals(state.source) || String("timer").equals(state.source) || String("init").equals(state.source) || String("WS_in").equals(state.source)) || !state.isOn);
                    if (canBeControlled)
                    {
                        bool wasOn = state.isOn;

                        setState(pairs[i], requestedState, timeoutSec);

                        if (!wasOn && requestedState == SHELLY_FULL_ON)
                        {
                            break; // activete only one relay
                        }
                    }
                    else
                    {
                        log_w("Shelly %s cannot be controlled", String(pairs[i].shellyId, HEX).c_str());
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
            return setWiFiSTA_Gen1(ssid, password);
        case PRO1PM:
        case PLUS_PLUG_S:
        case PLUS1PM:
        case PRO3:
        case PRODM1PM:
        case DIMG3:
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
    HTTPClient http;
    WiFiClient client;
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

    bool setWiFiSTA_Gen1(String ssid, String password)
    {
        bool result = false;
        String url = "http://192.168.33.1/settings/sta?enabled=true&ssid=" + urlencode(ssid) + "&key=" + urlencode(password);
        if (http.begin(url))
        {
            int httpCode = http.GET();
            if (httpCode == HTTP_CODE_OK)
            {
                result = true;
            }
        }
        http.end();
        return result;
    }

    bool setWiFiSTA_Gen2(String ssid, String password)
    {
        String url = "http://192.168.33.1/rpc";
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

        if (http.begin(url))
        {
            int httpCode = http.POST(requestBody);
            log_d("HTTP POST %s", requestBody.c_str());
            if (httpCode == HTTP_CODE_OK)
            {
                result = true;
            }
        }
        http.end();
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

        if (sendRequest(client, ipAddress, "GET", "/status", ""))
        {
            DynamicJsonDocument doc(8192);
            ReadBufferingStream bufferingStream(client, 1024);
            LoggingStream loggingStream(bufferingStream, Serial);
            DeserializationError err = deserializeJson(doc, loggingStream);
            if (err)
            {
                log_e("Failed to parse JSON");
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

        if (sendRequest(client, ipAddress, "POST", "/rpc", "{\"id\":1,\"method\":\"Shelly.GetStatus\"}"))
        {
            DynamicJsonDocument doc(8192);
            ReadBufferingStream bufferingStream(client, 1024);
            LoggingStream loggingStream(bufferingStream, Serial);
            DeserializationError err = deserializeJson(doc, loggingStream);
            if (err)
            {
                log_e("Failed to parse JSON");
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

    bool setState(ShellyPair_t shellyPair, RequestedShellyState_t requestedState, int timeoutSec)
    {
        bool result = true;

        switch (shellyPair.model)
        {
        case PLUG:
        case PLUG_S:
            if (requestedState == SHELLY_FULL_ON || requestedState >= SHELLY_KEEP_CURRENT_STATE && shellyPair.lastState.isOn)
            {
                result = setState_Gen1(shellyPair.ip, true, timeoutSec);
            }
            break;
        case PRO1PM:
        case PLUS_PLUG_S:
        case PLUS1PM:
            if (requestedState == SHELLY_FULL_ON || requestedState >= SHELLY_KEEP_CURRENT_STATE && shellyPair.lastState.isOn)
            {
                result = setState_Gen2(shellyPair.ip, "relay", 0, true, timeoutSec);
            }
            break;
        case DIMG3:
        case PRODM1PM:
        {
            int step = 0;
            switch (requestedState)
            {
            case SHELLY_FULL_OFF:
                step = -10;
                break;
            case SHELLY_PARTIAL_OFF:
                step = -5;
                break;
            case SHELLY_FULL_ON:
                step = 10;
                break;
            case SHELLY_PARTIAL_ON:
                step = 5;
                break;
            }
            int percent = (shellyPair.lastState.isOn ? shellyPair.lastState.percent : 0) + step;
            percent = min(max(percent, 0), 100);
            result = setState_Gen2(shellyPair.ip, "light", 0, percent > 0, timeoutSec, percent);
            break;
        }
        case PRO3:
            if (requestedState == SHELLY_FULL_ON || requestedState >= SHELLY_KEEP_CURRENT_STATE && shellyPair.lastState.isOn)
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

        if (sendRequest(client, ipAddress, "GET", path, ""))
        {
            result = true;
        }
        client.stop();

        return result;
    }
};

#endif