#pragma once

#include <WiFi.h>
#include <WiFiMulti.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "sha-256/sha-256.h"
#include "WallboxResult.hpp"
#include "utils/HexString.h"
#include <HTTPClient.h>

// http://asnplus.github.io/ev-local-api-docs/#/charger/chargerUpdateSettings

class EcoVolterProAPIV2
{
public:
    EcoVolterProAPIV2()
    {
    }

    bool setTargetCurrent(String wallboxId, String wallboxApiKey, int targetCurrent)
    {
        HTTPClient http;
        wallboxId.toLowerCase();
        wallboxApiKey.toLowerCase();

        bool result = false;

        String path = "/api/v1/charger/settings";
        String url = "http://" + wallboxId + ".local" + path;
       
        time_t timestamp = time(NULL);

        String body = "{\"timestamp\": " + String(timestamp) + "000" + ",\"targetCurrent\": " + String(targetCurrent) + ",\"isChargingEnable\": " + String(targetCurrent > 0) + "}";
       
        if (http.begin(url))
        {
            authorize(http, wallboxId, path, body, wallboxApiKey, timestamp);
            int httpCode = http.PATCH(body);
            if (httpCode == HTTP_CODE_OK)
            {
                result = true;
            }
            else
            {
                log_d("ERROR: %s", http.errorToString(httpCode).c_str());
                log_d("Response: %s", http.getString().c_str());
            }

            http.end();
        }
        else
        {
            log_d("Unable to connect.");
        }

        return result;
    }

    WallboxResult_t getStatus()
    {

        HTTPClient http;
        WallboxResult_t result;
        result.updated = 0;

        log_d("Reloading EcoVolterProV2 data");

        String path = "/api/v1/charger/status";
        String url = "http://" + ecoVolterId + ".local" + path;
        log_d("Requesting: %s", url.c_str());
        time_t timestamp = time(NULL);

        if (http.begin(url))
        {
            authorize(http, ecoVolterId, path, "", ecoVolterId, timestamp);
            int httpCode = http.GET();
            if (httpCode == HTTP_CODE_OK)
            {
                String payload = http.getString();
                log_d("Response: %s", payload.c_str());
                DynamicJsonDocument doc(2048);
                deserializeJson(doc, payload);

                result.updated = time(NULL);
                result.chargingEnergy = doc["chargedEnergy"].as<float>();
                result.chargingPower = doc["actualPower"].as<int>() * 1000.0f;
                result.chargingCurrent = doc["currentL1"].as<int>();
                result.maxChargingCurrent = doc["adapterMaxCurrent"].as<int>();
                result.chargingControlEnabled = !doc["isBoostModeActive"].as<bool>();
                result.evConnected = doc["isVehicleConnected"].as<bool>();
                result.phases = doc["isThreePhaseModeActive"].as<bool>() ? 3 : 1;
                result.voltageL1 = doc["voltageL1"].as<int>();
                result.voltageL2 = doc["voltageL2"].as<int>();
                result.voltageL3 = doc["voltageL3"].as<int>();
                result.currentL1 = doc["currentL1"].as<int>();
                result.currentL2 = doc["currentL2"].as<int>();
                result.currentL3 = doc["currentL3"].as<int>();
                result.temperature = doc["temperatures"]["internal"].as<int>();
                log_d("EVConnected: %s", String(result.evConnected).c_str());
            }
            else
            {
                log_d("ERROR: %s", http.errorToString(httpCode));
                log_d("Response: %s", http.getString());
            }

        }
        else
        {
            log_d("Unable to connect.");
        }
        http.end();
        return result;
    }

    bool isDiscovered()
    {
        return ecoVolterId.length() > 0;
    }

    void queryMDNS()
    {
        mdns_result_t *results = NULL;

        if (mdns_init() != ESP_OK)
        {
            log_e("Failed starting MDNS");
            return;
        }

        if (mdns_query_ptr("_http", "_tcp", 2000, 20, &results) != ESP_OK)
        {
            log_e("Failed to query MDNS");
            mdns_free();
            return;
        }

        uint8_t numResult = 0;

        mdns_result_t *r = results;
        while (r)
        {
            String hostname = r->hostname;
            
            log_d("Found service: %s", hostname.c_str());
            log_d("Found IP: %s", String(r->addr->addr.u_addr.ip4.addr).c_str());
            log_d("Found instance: %s", String(r->instance_name).c_str());
            
            IPAddress ipAddress = r->addr->addr.u_addr.ip4.addr;
            if(hostname.startsWith("REVCS")) {
                log_d("Found REVCS device: %s", hostname.c_str());
                hostname.toLowerCase();
                ecoVolterId = hostname;
                break;
            }
            
            r = r->next;
        }
        mdns_query_results_free(results);        
        mdns_free();
    }

private:

    String ecoVolterId;

    void authorize(HTTPClient &http, String wallboxId, String path, String body, String apiKey, time_t timestamp)
    {
        wallboxId.toLowerCase();
        apiKey.toLowerCase();

        String url = "http://" + wallboxId + ".local" + path;

        String data = url + "\n" + String(timestamp) + "\n" + body;
        log_d("Data: %s", data.c_str());
        uint8_t hash[SIZE_OF_SHA_256_HASH];

        // hmac sha256
        hmac_sha256(hash, apiKey, data);
        String hexHash = dataToHexString(hash, SIZE_OF_SHA_256_HASH);
        log_d("HMAC: %s", hexHash.c_str());

        // add headers
        http.addHeader("Authorization", "HmacSHA256 " + hexHash);
        http.addHeader("X-Timestamp", String(timestamp));
    }

    void hmac_sha256(uint8_t *out, const String key, const String data)
    {
        log_d("HMAC SHA256");
        log_d("Key: %s", key.c_str());
        log_d("Data: %s", data.c_str());
        uint8_t o_key_pad[SIZE_OF_SHA_256_CHUNK];
        uint8_t i_key_pad[SIZE_OF_SHA_256_CHUNK];
        uint8_t temp_hash[SIZE_OF_SHA_256_HASH];
        uint8_t key_block[SIZE_OF_SHA_256_CHUNK];

        struct Sha_256 sha_ctx;
        size_t key_len = key.length();
        size_t data_len = data.length();

        // Step 1: Process key
        if (key_len > SIZE_OF_SHA_256_CHUNK)
        {
            calc_sha_256(key_block, key.c_str(), key_len); // Hash long key
            memset(key_block + SIZE_OF_SHA_256_HASH, 0, SIZE_OF_SHA_256_CHUNK - SIZE_OF_SHA_256_HASH);
        }
        else
        {
            memcpy(key_block, key.c_str(), key_len);
            memset(key_block + key_len, 0, SIZE_OF_SHA_256_CHUNK - key_len);
        }

        // Step 2: Create inner and outer pads
        for (size_t i = 0; i < SIZE_OF_SHA_256_CHUNK; i++)
        {
            o_key_pad[i] = key_block[i] ^ 0x5C;
            i_key_pad[i] = key_block[i] ^ 0x36;
        }

        // Step 3: Inner hash
        sha_256_init(&sha_ctx, temp_hash);
        sha_256_write(&sha_ctx, i_key_pad, SIZE_OF_SHA_256_CHUNK);
        sha_256_write(&sha_ctx, data.c_str(), data_len);
        sha_256_close(&sha_ctx);

        // Step 4: Outer hash
        sha_256_init(&sha_ctx, out);
        sha_256_write(&sha_ctx, o_key_pad, SIZE_OF_SHA_256_CHUNK);
        sha_256_write(&sha_ctx, temp_hash, SIZE_OF_SHA_256_HASH);
        sha_256_close(&sha_ctx);
    }
};