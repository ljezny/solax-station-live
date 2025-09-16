#pragma once
#include <WiFi.h>
#include <WiFiMulti.h>

#include "WallboxResult.hpp"
#include <Preferences.h>
#include <mdns.h>
#include <IPAddress.h>
#include <NetworkClient.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>

#define SOLAXWEB_WALLBOX_DONGLE_IP_KEY "wb_ip"
#define SOLAXWEB_WALLBOX_DONGLE_SN_KEY "wb_sn"

typedef struct
{
    IPAddress ip;
    String sn;
} SolaxWallboxInfo_t;

typedef struct
{
    long updated = 0;
    bool evConnected = false;
    int chargingPower = 0;
    int chargingCurrent = 0;
    int maxChargingCurrent = 0;
    int targetChargingCurrent = 0;
    float totalChargedEnergy = 0;
    float chargedEnergy = 0;
    bool chargingControlEnabled = false;
    int phases = 3;
    int voltageL1 = 0;
    int voltageL2 = 0;
    int voltageL3 = 0;
    int currentL1 = 0;
    int currentL2 = 0;
    int currentL3 = 0;
    int temperature = 0;
} SolaxRealtimeData_t;

typedef struct
{
    long updated = 0;
    int mode = 0; //0: off, 1: Fast charge, 2: Eco charge, 3: Green charge
    int targetChargingCurrent = 0;
} SolaxSetData_t;

class SolaxWallboxLocalAPI
{
public:
    WallboxResult_t getStatus() {
        WallboxResult_t result;

        SolaxRealtimeData_t realtimeData = getRealtimeData();
        if(realtimeData.updated > 0) {
            result.updated = realtimeData.updated;
            result.type = WALLBOX_TYPE_SOLAX;
            result.evConnected = realtimeData.evConnected;
            result.chargingPower = realtimeData.chargingPower;
            result.chargedEnergy = realtimeData.chargedEnergy;
            result.chargingCurrent = realtimeData.chargingCurrent;
            result.maxChargingCurrent = realtimeData.maxChargingCurrent;
            result.targetChargingCurrent = realtimeData.targetChargingCurrent;
            result.totalChargedEnergy = realtimeData.totalChargedEnergy;
            result.chargingControlEnabled = realtimeData.chargingControlEnabled;
            result.phases = realtimeData.phases;
            result.voltageL1 = realtimeData.voltageL1;
            result.voltageL2 = realtimeData.voltageL2;
            result.voltageL3 = realtimeData.voltageL3;
            result.currentL1 = realtimeData.currentL1;
            result.currentL2 = realtimeData.currentL2;
            result.currentL3 = realtimeData.currentL3;
            result.temperature = realtimeData.temperature;

            SolaxSetData_t setData = getSetData();
            if(setData.updated > 0) {
                result.targetChargingCurrent = setData.targetChargingCurrent;
                result.maxChargingCurrent = 16;
            }
        }
        logWallboxResult(result);
        return result;
    }

    bool setCharging(bool enable) {
        //0: off, 1: Fast charge, 2: Eco charge, 3: Green charge
        int mode = enable ? 1 : 0;
        return setRegValue(1, mode);
    }

    bool setMaxCurrent(int current) {
        if(current < 6) current = 6;
        if(current > 16) current = 16;
        return setRegValue(82, current); // in 0.01A
    }

    bool isDiscovered() const
    {
        return wallboxInfo.ip != IPAddress(0, 0, 0, 0);
    }

    bool discoverWallbox()
    {
        wallboxInfo = getWallboxInfo();
        return isDiscovered();
    }

private:
    SolaxWallboxInfo_t wallboxInfo;
    SolaxWallboxInfo_t getWallboxInfo()
    {
        uint32_t ip = 0;
        String sn = "";
        mdns_result_t *results = NULL;
        mdns_init();

        esp_err_t err = mdns_query_ptr("_EVC", "_tcp", 3000, 20, &results);
        if (err == ESP_OK)
        {
            if (results)
            {
                mdns_result_t *r = results;
                while (r)
                {
                    log_d("Found dongle: %s", r->hostname);
                    ip = r->addr->addr.u_addr.ip4.addr;

                    // SN is in the TXT record
                    for (size_t i = 0; i < r->txt_count; i++)
                    {
                        if (strcmp(r->txt[i].key, "SN") == 0)
                        {
                            sn = String((const char *)r->txt[i].value, r->txt_value_len[i]);
                            log_d("Found dongle SN: %s", sn.c_str());
                        }
                    }

                    break;
 
                    r = r->next;
                }
                mdns_query_results_free(results);
            }
            else
            {
                log_d("No results found.");
            }
        }
        else
        {
            log_d("MDNS Query failed with error: %s", String(err).c_str());
        }

        mdns_free();

        return {IPAddress(ip), sn};
    }

    SolaxRealtimeData_t getRealtimeData()
    {
        SolaxRealtimeData_t result;
        result.updated = 0;
        if (wallboxInfo.ip != IPAddress(0, 0, 0, 0))
        {
            HTTPClient client;
            if (client.begin(wallboxInfo.ip.toString(), 80))
            {
                int httpCode = client.POST("optType=ReadRealTimeData&pwd=" + wallboxInfo.sn);
                if (httpCode == HTTP_CODE_OK)
                {
                    String payload = client.getString();
                    log_d("Wallbox payload: %s", payload.c_str());
                    DynamicJsonDocument doc(4096); // Adjust size as needed
                    DeserializationError error = deserializeJson(doc, payload);
                    if (!error)
                    {
                        // https://github.com/nazar-pc/solax-local-api-docs/blob/master/DataEvCharger.txt
                        if (doc["Data"].as<JsonArray>().size() != 0)
                        {
                            result.updated = time(NULL);
                            /*0,1: Preparing
                            2: Charging
                            3: Finishing
                            4: Faulted
                            5: Unavailable
                            6: Reserved
                            7: SuspendedEV
                            8: SuspendedEVSE*/
                            int deviceState = doc["Data"][0].as<int>();
                            log_d("Device state: %d", deviceState);
                            result.evConnected = deviceState == 1 || deviceState == 2 || deviceState == 3;

                            result.chargingPower = doc["Data"][11].as<int>();
                            result.chargedEnergy = doc["Data"][12].as<int>() / 10.f; // Convert Wh to kWh
                            result.totalChargedEnergy = (doc["Data"][15].as<int>() << 8 | doc["Data"][14].as<int>()) / 10.f; // Convert Wh to kWh
                            result.chargingCurrent = doc["Data"][5].as<int>() / 100;
                            result.chargingControlEnabled = true;
                            result.voltageL1 = doc["Data"][2].as<int>() / 100;
                            result.voltageL2 = doc["Data"][3].as<int>() / 100;
                            result.voltageL3 = doc["Data"][4].as<int>() / 100;
                            result.currentL1 = doc["Data"][5].as<int>() / 100;
                            result.currentL2 = doc["Data"][6].as<int>() / 100;
                            result.currentL3 = doc["Data"][7].as<int>() / 100;
                            result.temperature = doc["Data"][24].as<int>(); 
                            result.phases = 3;
                            result.updated = millis();
                        }
                        else
                        {
                            log_d("No data found in response.");
                        }
                    }
                    else
                    {
                        log_d("JSON deserialization failed: %s", error.c_str());
                    }
                }
                else
                {
                    log_d("HTTP GET failed with code: %d", httpCode);
                }
                client.end();
            }
            else
            {
                log_d("Failed to connect to wallbox dongle at %s", wallboxInfo.ip.toString().c_str());
            }
        }
        return result;
    }

    SolaxSetData_t getSetData()
    {
        SolaxSetData_t result;
        result.updated = 0;
        if (wallboxInfo.ip != IPAddress(0, 0, 0, 0))
        {
            HTTPClient client;
            if (client.begin(wallboxInfo.ip.toString(), 80))
            {
                int httpCode = client.POST("optType=ReadSetData&pwd=" + wallboxInfo.sn);
                if (httpCode == HTTP_CODE_OK)
                {
                    String payload = client.getString();
                    log_d("Wallbox set payload: %s", payload.c_str());
                    DynamicJsonDocument doc(4*1024); // Adjust size as needed
                    DeserializationError error = deserializeJson(doc, payload);
                    if (!error)
                    {
                        result.updated = millis();
                        result.targetChargingCurrent = doc[76].as<int>();
                        result.mode = doc[1].as<int>();
                    }
                    else
                    {
                        log_d("JSON deserialization failed: %s", error.c_str());
                    }
                }
                else
                {
                    log_d("HTTP GET failed with code: %d", httpCode);
                }
                client.end();
            }
            else
            {
                log_d("Failed to connect to wallbox dongle at %s", wallboxInfo.ip.toString().c_str());
            }
        }
        return result;
    }

    bool setRegValue(int reg, int value)
    {
        bool result = false;
        if (wallboxInfo.ip != IPAddress(0, 0, 0, 0))
        {
            HTTPClient client;
            if (client.begin(wallboxInfo.ip.toString(), 80))
            {
                //optType=setReg&pwd=SQTGDYPBXK&data={"num":1,"Data":[{"reg":2,"val":"3"}]}
                String postData = "optType=WriteSetData&pwd=" + wallboxInfo.sn + "&data={\"num\":1,\"Data\":[{\"reg\":" + String(reg) + ",\"val\":\"" + String(value) + "\"}]}";
                log_d("POST data: %s", postData.c_str());
                int httpCode = client.POST(postData);
                if (httpCode == HTTP_CODE_OK)
                {
                    String payload = client.getString();
                    result = payload.startsWith("Y:code");
                    log_d("Set reg result: %s", payload.c_str());
                }
                else
                {
                    log_d("HTTP POST failed with code: %d", httpCode);
                }
                client.end();
            }
            else
            {
                log_d("Failed to connect to wallbox dongle at %s", wallboxInfo.ip.toString().c_str());
            }
        }
        return result;
    }
};