#pragma once

#ifndef ShellyAPI_h
#define ShellyAPI_h

#include <WiFi.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "../utils/urlencoder.hpp"
#include <ESPmDNS.h>

#define MAX_SHELLY_PAIRS 8

#define RETRY_COUNT 3

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

typedef struct ShellyPair
{
    unsigned long long shellyId = 0;
    IPAddress ip = INADDR_NONE;
    ShellyModel_t model = PLUG_S;
} ShellyPair_t;

typedef struct ShellyStateResult {
    int updated = 0;
    ShellyModel_t model = PLUG_S;
    bool isOn = false;
    String source = "init";
    int totalPower = 0;
    int totalEnergy = 0;
} ShellyStateResult_t;

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

    ShellyAPI() {
        for(int i = 0; i < MAX_SHELLY_PAIRS; i++)
        {
            pairs[i].shellyId = 0;
            pairs[i].ip = INADDR_NONE;
            pairs[i].model = PLUG_S;
        }
    }

    void initMDNS(String name)
    {
        if (!MDNS.begin(name.c_str()))
        {
            log_e("Error setting up MDNS responder!");
        }
    }
    String findShellyAP()
    {
        int found = WiFi.scanNetworks();
        for (int i = 0; i < found; i++)
        {
            String ssid = WiFi.SSID(i);
            log_d("Found network: %s", ssid.c_str());
            for (int i = 0; i < SHELLY_SUPPORTED_MODEL_COUNT; i++)
            {
                if (ssid.startsWith(supportedModels[i].prefix))
                {
                    log_d("Found Shelly AP: %s", ssid.c_str());
                    return ssid;
                }
            }
        }
        return "";
    }

    void pairShelly(String shellySSId, String ssid, String password)
    {
        WiFi.begin(shellySSId);

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

    void discoverParings()
    {
        for (int x = 0; x < MDNS.queryService("http", "tcp"); x++)
        {
            String hostname = MDNS.hostname(x);
            for (int i = 0; i < SHELLY_SUPPORTED_MODEL_COUNT; i++)
            {
                String prefix = supportedModels[i].prefix;
                prefix.toLowerCase();
                if (hostname.startsWith(prefix))
                {
                    log_d("Found Shelly: %s", hostname.c_str());
                    String idText = hostname.substring(prefix.length());
                    unsigned long long shellyId = strtoull(idText.c_str(), NULL, 16);
                
                    for (int j = 0; i < MAX_SHELLY_PAIRS; j++)
                    {
                        if (pairs[j].shellyId == 0 || pairs[j].shellyId == shellyId)
                        {
                            pairs[j].shellyId = shellyId;
                            pairs[j].ip = MDNS.address(x);
                            pairs[j].model = supportedModels[i].model;
                            log_d("Paired Shelly %s", String(shellyId, HEX).c_str());
                            break;
                        }
                    }
                }
            }
        }
    }

    ShellyResult_t getState()
    {
        ShellyResult_t result;
        for(int i = 0; i < MAX_SHELLY_PAIRS; i++)
        {
            if(pairs[i].shellyId != 0 && pairs[i].ip != INADDR_NONE)
            {
                log_d("Getting state for Shelly %s", String(pairs[i].shellyId, HEX).c_str());
                result.pairedCount++;

                ShellyStateResult_t state = getState(pairs[i]);
                if(state.updated != 0)
                {
                    if(state.isOn)
                    {
                        result.activeCount++;
                    }
                    result.totalPower += state.totalPower;
                    result.totalEnergy += state.totalEnergy;
                } else {
                    log_e("Failed to get state for Shelly %s", String(pairs[i].shellyId, HEX).c_str());
                    pairs[i].ip = INADDR_NONE;
                }
            }
        }
        return result;
    }

private:
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

    bool setWiFiSTA_Gen1(String ssid, String password)
    {
        HTTPClient http;
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
        HTTPClient http;
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
        for (int i = 0; i < RETRY_COUNT; i++)
        {
            HTTPClient http;
            if (http.begin(url))
            {
                int httpCode = http.GET();
                if (httpCode == HTTP_CODE_OK)
                {
                    String payload = http.getString();
                    DynamicJsonDocument doc(8192);
                    deserializeJson(doc, payload);
                    result.updated = time(NULL); // doc["unixtime"].as<int>();
                    result.isOn = doc["relays"][0]["ison"].as<bool>();
                    result.source = doc["relays"][0]["source"].as<String>();
                    result.totalPower = doc["meters"][0]["power"].as<float>();
                    result.totalEnergy = doc["meters"][0]["total"].as<float>() / 60.0f;
                }
            }
            http.end();

            if (result.updated != 0)
            {
                break;
            }
            delay(i * 200);
        }

        return result;
    }

    ShellyStateResult_t getState_Gen2(IPAddress ipAddress)
    {
        ShellyStateResult_t result;
        result.updated = 0;
        String url = "http://" + ipAddress.toString() + "/rpc/Switch.GetStatus?id=0";
        for (int i = 0; i < RETRY_COUNT; i++)
        {
            HTTPClient http;
            if (http.begin(url))
            {
                int httpCode = http.GET();
                if (httpCode == HTTP_CODE_OK)
                {
                    String payload = http.getString();
                    DynamicJsonDocument doc(8192);
                    deserializeJson(doc, payload);
                    result.updated = time(NULL);
                    result.isOn = doc["output"].as<bool>();
                    result.totalPower = doc["apower"].as<float>();
                    result.source = doc["source"].as<String>();
                    result.totalEnergy = doc["aenergy"]["total"].as<float>();
                }
            }
            http.end();
            if (result.updated != 0)
            {
                break;
            }
            delay(i * 200);
        }

        return result;
    }
};

#endif