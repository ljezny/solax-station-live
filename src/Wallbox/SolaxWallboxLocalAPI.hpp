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

class SolaxWallboxLocalAPI
{
public:
    WallboxResult_t getStatus()
    {
        WallboxResult_t result;
        result.updated = 0;
        SolaxWallboxInfo_t wbInfo = getWallboxInfo();
        if (wbInfo.ip != IPAddress(0, 0, 0, 0))
        {
            HTTPClient client;
            if (client.begin(wbInfo.ip.toString(), 80))
            {
                int httpCode = client.POST("optType=ReadRealTimeData&pwd=" + wbInfo.sn);
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
                            result.chargingEnergy = doc["Data"][12].as<int>() / 10.f; // Convert Wh to kWh
                            result.chargingCurrent = doc["Data"][5].as<int>() / 100;
                            result.chargingControlEnabled = false;
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
                log_d("Failed to connect to wallbox dongle at %s", wbInfo.ip.toString().c_str());
            }
        }
        return result;
    }

private:
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
};