#pragma once

#ifndef ShellyAPI_h
#define ShellyAPI_h

#include <WiFi.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "../utils/urlencoder.hpp"
#include <mdns.h>
#include "utils/ShellyRuleResolver.hpp"
#include <StreamUtils.h>

#define MAX_SHELLY_PAIRS 8

typedef enum
{
    PLUG_S,
    PLUG,
    PRO1PM,
    PLUS_PLUG_S,
    PLUS1PM,
    PRO3
} ShellyModel_t;

typedef struct ShellyModelInfo
{
    ShellyModel_t model;
    String prefix;
} ShellyModelInfo_t;

#define SHELLY_SUPPORTED_MODEL_COUNT 10
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
    {PRO3, "Pro3-"}};

typedef struct ShellyStateResult
{
    int updated = 0;
    ShellyModel_t model = PLUG_S;
    bool isOn = false;
    String source = "init";
    int totalPower = 0;
    int totalEnergy = 0;
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

    void queryMDNS()
    {
        mdns_result_t *results = NULL;

        if (mdnsSearch == NULL)
        {
            if (mdns_init() != ESP_OK)
            {
                log_e("Failed starting MDNS");
            }
            mdnsSearch = mdns_query_async_new(NULL, "_http", "_tcp", MDNS_TYPE_PTR, 5000, 20, NULL);
            if (mdnsSearch == NULL)
            {
                log_e("Failed to start mDNS search");
                return;
            }
        }
        uint8_t numResult = 0;
        if (mdns_query_async_get_results(mdnsSearch, 100, &results, &numResult))
        {
            mdns_result_t *r = results;

            while (r)
            {
                String hostname = r->hostname;
                for (int i = 0; i < SHELLY_SUPPORTED_MODEL_COUNT; i++)
                {
                    String prefix = supportedModels[i].prefix;
                    prefix.toLowerCase();
                    if (hostname.startsWith(prefix))
                    {
                        log_d("Found Shelly: %s model: %s", hostname.c_str(), supportedModels[i].prefix.c_str());
                        String idText = hostname.substring(prefix.length());
                        unsigned long long shellyId = strtoull(idText.c_str(), NULL, 16);
                        for (int j = 0; i < MAX_SHELLY_PAIRS; j++)
                        {
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
        bool newActivated = false;
        for (int i = 0; i < MAX_SHELLY_PAIRS; i++)
        {
            if (pairs[i].shellyId != 0 && pairs[i].ip != INADDR_NONE)
            {
                ShellyStateResult_t state = pairs[i].lastState;
                if (state.updated != 0)
                {
                    bool canBeControlled = (state.isOn && (state.source == NULL || String("http").equals(state.source) || String("init").equals(state.source))) || !state.isOn;
                    if (canBeControlled)
                    {
                        if (state.isOn)
                        { // prolong timeout
                            if (requestedState >= SHELLY_KEEP_CURRENT_STATE)
                            {
                                log_d("Prolonging timeout for Shelly %s", String(pairs[i].shellyId, HEX).c_str());
                                setState(pairs[i], true, timeoutSec);
                            }
                        }
                        else
                        {
                            if (!newActivated)
                            {
                                if (requestedState == SHELLY_ACTIVATE)
                                {
                                    log_d("Activating Shelly %s", String(pairs[i].shellyId, HEX).c_str());
                                    setState(pairs[i], true, timeoutSec);
                                }
                                newActivated = true;
                            }
                        }
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
        StaticJsonDocument<200> doc;
        doc["id"] = 1;
        doc["method"] = "WiFi.SetConfig";
        doc["params"]["config"]["sta"]["ssid"] = ssid;
        doc["params"]["config"]["sta"]["pass"] = password;
        doc["params"]["config"]["sta"]["enable"] = true;
        doc["params"]["config"]["ap"]["enable"] = false;
        String requestBody;
        serializeJson(doc, requestBody);
        bool result = false;

        String url = "http://192.168.33.1/rpc";

        if (http.begin(url))
        {
            int httpCode = http.POST(requestBody);
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
            result = getState_Gen2(shellyPair.ip);
            break;
        }
        return result;
    }

    ShellyStateResult_t getState_Gen1(IPAddress ipAddress)
    {
        ShellyStateResult_t result;
        result.updated = 0;

        String url = "http://" + ipAddress.toString() + "/status";

        if (http.begin(url))
        {
            int httpCode = http.GET();
            if (httpCode == HTTP_CODE_OK)
            {
                DynamicJsonDocument doc(8192);
                ReadBufferingStream httpStream(http.getStream(), 1024);
                LoggingStream loggingStream(httpStream, Serial);
                DeserializationError err = deserializeJson(doc, loggingStream);
                result.updated = millis(); // doc["unixtime"].as<int>();
                result.isOn = doc["relays"][0]["ison"].as<bool>();
                result.source = doc["relays"][0]["source"].as<String>();
                result.totalPower = doc["meters"][0]["power"].as<float>();
                result.totalEnergy = doc["meters"][0]["total"].as<float>() / 60.0f;
            }
        }
        http.end();

        return result;
    }

    ShellyStateResult_t getState_Gen2(IPAddress ipAddress)
    {
        ShellyStateResult_t result;
        result.updated = 0;
        String url = "http://" + ipAddress.toString() + "/rpc/Switch.GetStatus?id=0";

        if (http.begin(url))
        {
            int httpCode = http.GET();
            if (httpCode == HTTP_CODE_OK)
            {
                DynamicJsonDocument doc(8192);
                ReadBufferingStream httpStream(http.getStream(), 1024);
                LoggingStream loggingStream(httpStream, Serial);
                DeserializationError err = deserializeJson(doc, loggingStream);
                result.updated = millis();
                result.isOn = doc["output"].as<bool>();
                result.totalPower = doc["apower"].as<float>();
                result.source = doc["source"].as<String>();
                result.totalEnergy = doc["aenergy"]["total"].as<float>();
            }
        }
        http.end();

        return result;
    }

    bool setState(ShellyPair_t shellyPair, bool on, int timeoutSec)
    {
        bool result = true;
        switch (shellyPair.model)
        {
        case PLUG:
            result = setState_Gen1(shellyPair.ip, on, timeoutSec);
            break;
        case PLUG_S:
            result = setState_Gen1(shellyPair.ip, on, timeoutSec);
            break;
        case PRO1PM:
        case PLUS_PLUG_S:
        case PLUS1PM:
            result = setState_Gen2(shellyPair.ip, 0, on, timeoutSec);
            break;
        case PRO3:
            result = setState_Gen2(shellyPair.ip, 0, on, timeoutSec);
            result = setState_Gen2(shellyPair.ip, 1, on, timeoutSec);
            result = setState_Gen2(shellyPair.ip, 2, on, timeoutSec);
            break;
        }
        return result;
    }

    bool setState_Gen1(IPAddress ipAddress, bool on, int timeoutSec)
    {
        bool result = false;
        String url = "http://" + ipAddress.toString() + "/relay/0?turn=" + (on ? "on" : "off");
        if (timeoutSec > 0)
        {
            url += "&timer=" + String(timeoutSec);
        }

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

    bool setState_Gen2(IPAddress ipAddress, int relay, bool on, int timeoutSec)
    {
        bool result = true;
        String url = "http://" + ipAddress.toString() + "/relay/" + String(relay) + "?turn=" + (on ? "on" : "off");
        if (timeoutSec > 0)
        {
            url += "&timer=" + String(timeoutSec);
        }

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
};

#endif