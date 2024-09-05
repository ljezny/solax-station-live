#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>

#include "Inverters/InverterResult.hpp"

class SolaxDongleAPI
{
public:
    InverterData_t loadData(String sn)
    {
        InverterData_t inverterData;
        HTTPClient http;
        if (http.begin(getUrl()))
        {
            int httpCode = http.POST("optType=ReadRealTimeData&pwd=" + sn);
            if (httpCode == HTTP_CODE_OK)
            {
                StaticJsonDocument<14*1024> doc;
                String payload = http.getString();
                log_d("Payload: %s", payload.c_str());
                DeserializationError err = deserializeJson(doc, payload);
                if (err == DeserializationError::Ok)
                {
                    if(doc["type"].as<int>() == 14) {
                        inverterData.status = DONGLE_STATUS_OK;
                        inverterData.millis = millis();
                        inverterData.pv1Power = doc["Data"][14].as<int>();
                        inverterData.pv2Power = doc["Data"][15].as<int>();
                        inverterData.batteryPower = read16BitSigned(doc["Data"][41].as<uint16_t>());
                        inverterData.batteryTemperature = doc["Data"][105].as<uint8_t>();
                        inverterData.inverterTemperature = doc["Data"][54].as<uint8_t>();
                        inverterData.L1Power = doc["Data"][6].as<int>();
                        inverterData.L2Power = doc["Data"][7].as<int>();
                        inverterData.L3Power = doc["Data"][8].as<int>();
                        inverterData.inverterPower = doc["Data"][9].as<int>();
                        inverterData.loadPower = read16BitSigned(doc["Data"][47].as<uint16_t>());
                        inverterData.soc = doc["Data"][103].as<int>();
                        inverterData.pvToday = doc["Data"][70].as<uint16_t>() / 10.0;
                        inverterData.pvTotal = ((doc["Data"][69].as<uint32_t>() << 16) + doc["Data"][68].as<uint16_t>()) / 10.0;
                        inverterData.feedInPower = read16BitSigned(doc["Data"][34].as<uint16_t>());
                        inverterData.gridSellToday = doc["Data"][90].as<uint16_t>() / 100.0; 
                        inverterData.gridBuyToday = doc["Data"][92].as<uint16_t>() / 100.0;
                        inverterData.batteryChargedToday = doc["Data"][79].as<uint16_t>() / 10.0;
                        inverterData.batteryDischargedToday = doc["Data"][78].as<uint16_t>() / 10.0;
                        inverterData.loadToday = inverterData.pvToday + inverterData.gridBuyToday;
                        inverterData.sn = sn;
                    } if(doc["type"].as<int>() == 16) { //X3-MIC/PRO-G2 https://github.com/simatec/ioBroker.solax/blob/master/lib/inverterData.js
                        inverterData.status = DONGLE_STATUS_OK;
                        inverterData.millis = millis();
                        inverterData.pv1Power = doc["Data"][15].as<int>();
                        inverterData.pv2Power = doc["Data"][16].as<int>() 
                                                + doc["Data"][17].as<int>();
                        inverterData.L1Power = doc["Data"][6].as<int>();
                        inverterData.L2Power = doc["Data"][7].as<int>();
                        inverterData.L3Power = doc["Data"][8].as<int>();
                        inverterData.inverterPower = doc["Data"][78].as<int>();
                        inverterData.pvToday = doc["Data"][24].as<uint16_t>() / 10.0;
                        inverterData.pvTotal = doc["Data"][22].as<uint16_t>() / 10.0;
                        inverterData.feedInPower = read16BitSigned(doc["Data"][74].as<uint16_t>());
                        inverterData.gridSellToday = doc["Data"][74].as<uint16_t>() / 100.0; 
                        inverterData.sn = sn;
                    } else if(doc["type"].as<int>() == 1) { //wallbox
                        //wallboxData.power = doc["Data"][11].as<int>();
                        inverterData.status = DONGLE_STATUS_UNSUPPORTED_DONGLE;
                    } else {
                        inverterData.status = DONGLE_STATUS_UNSUPPORTED_DONGLE;
                    }                    
                }
                else
                {
                    inverterData.status = DONGLE_STATUS_JSON_ERROR;
                }
            }
            else
            {
                inverterData.status = DONGLE_STATUS_HTTP_ERROR;
            }
            
        }
        else
        {
            inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
        }
        
        http.end();

        return inverterData;
    }

    String getStatusText(DongleStatus_t status)
    {
        switch (status)
        {
        case DONGLE_STATUS_OK:
            return "OK";
        case DONGLE_STATUS_CONNECTION_ERROR:
            return "Connection error";
        case DONGLE_STATUS_HTTP_ERROR:
            return "HTTP error";
        case DONGLE_STATUS_JSON_ERROR:
            return "JSON error";
        default:
            return "Unknown";
        }
    }

    String getUrl() {
        if(WiFi.localIP()[0] == 192) {
            return "http://192.168.10.10";
        } else {
            return "http://5.8.8.8";
        }
    }
    
private:
    int16_t read16BitSigned(uint16_t a)
    {
        if (a < 32768)
        {
            return a;
        }
        else
        {
            return a - 65536;
        }
    }
};