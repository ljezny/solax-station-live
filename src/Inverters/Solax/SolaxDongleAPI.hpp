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
        String url = "http://5.8.8.8";
        HTTPClient http;
        http.setConnectTimeout(2000);
        if (http.begin(url))
        {
            int httpCode = http.POST("optType=ReadRealTimeData&pwd=" + sn);
            if (httpCode == HTTP_CODE_OK)
            {
                StaticJsonDocument<14*1024> doc;
                String payload = http.getString();
                DeserializationError err = deserializeJson(doc, payload);
                if (err == DeserializationError::Ok)
                {
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
                    //inverterData.loadToday = ((doc["Data"][89].as<uint32_t>() << 16) + doc["Data"][86].as<uint16_t>()) / 100.0; 
                    inverterData.feedInPower = read16BitSigned(doc["Data"][34].as<uint16_t>());
                    inverterData.gridSellToday = doc["Data"][90].as<uint16_t>() / 100.0; 
                    inverterData.gridBuyToday = doc["Data"][92].as<uint16_t>() / 100.0;
                    //inverterData.gridSellTotal = ((doc["Data"][87].as<uint32_t>() << 16) + doc["Data"][86].as<uint16_t>()) / 100.0; 
                    //inverterData.gridBuyTotal = ((doc["Data"][89].as<uint32_t>() << 16) + doc["Data"][88].as<uint16_t>()) / 100.0; 
                    inverterData.batteryChargedToday = doc["Data"][79].as<uint16_t>() / 10.0;
                    inverterData.batteryDischargedToday = doc["Data"][78].as<uint16_t>() / 10.0;
                    //inverterData.batteryChargedTotal = ((doc["Data"][77].as<uint32_t>() << 16) + doc["Data"][76].as<uint16_t>()) / 10.0; 
                    //inverterData.batteryDischargedTotal = ((doc["Data"][75].as<uint32_t>() << 16) + doc["Data"][74].as<uint16_t>()) / 10.0; 
                    //inverterData.loadTotal = inverterData.pvTotal + inverterData.gridBuyTotal;
                    inverterData.loadToday = inverterData.pvToday + inverterData.gridBuyToday;
                    inverterData.sn = sn;
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
            http.end();
        }
        else
        {
            inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
        }
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