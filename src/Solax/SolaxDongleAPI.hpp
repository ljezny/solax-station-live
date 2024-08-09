#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>

typedef enum SolaxDongleStatus {
    SOLAX_DONGLE_STATUS_OK = 1,
    SOLAX_DONGLE_STATUS_UNKNOWN = 0,
    SOLAX_DONGLE_STATUS_CONNECTION_ERROR = -1,
    SOLAX_DONGLE_STATUS_HTTP_ERROR = -2,
    SOLAX_DONGLE_STATUS_JSON_ERROR = -3
} SolaxDongleStatus_t;

typedef struct
{
    SolaxDongleStatus_t status = SOLAX_DONGLE_STATUS_UNKNOWN;
    int pv1Power = 0;
    int pv2Power = 0;
    int soc = 0;
    int16_t batteryPower = 0;
    double batteryChargedToday = 0;
    double batteryDischargedToday = 0;
    double batteryChargedTotal = 0;
    double batteryDischargedTotal = 0;
    double gridBuyToday = 0;
    double gridSellToday = 0;
    double gridBuyTotal = 0;
    double gridSellTotal = 0;
    int L1Power = 0;
    int L2Power = 0;
    int L3Power = 0;
    int inverterPower = 0;
    int16_t loadPower = 0;
    float loadToday = 0;
    float loadTotal = 0;
    int32_t feedInPower = 0;
    int inverterTemperature = 0;
    int batteryTemperature = 0;
    double yieldToday = 0;
    uint32_t yieldTotal = 0;
} SolaxDongleInverterData_t;

class SolaxDongleAPI
{
public:
    SolaxDongleInverterData_t loadData(String sn)
    {
        SolaxDongleInverterData_t inverterData;
        String url = "http://5.8.8.8";
        HTTPClient http;
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
                    inverterData.status = SOLAX_DONGLE_STATUS_OK;
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
                    inverterData.yieldToday = doc["Data"][82].as<uint16_t>() / 10.0;
                    inverterData.yieldTotal = ((doc["Data"][69].as<uint32_t>() << 16) + doc["Data"][68].as<uint16_t>()) / 10.0;
                    inverterData.loadToday = doc["Data"][88].as<uint16_t>() / 1000.0;
                    inverterData.feedInPower = read16BitSigned(doc["Data"][34].as<uint16_t>());
                    inverterData.gridSellToday = doc["Data"][90].as<uint16_t>() / 100.0; 
                    inverterData.gridBuyToday = doc["Data"][92].as<uint16_t>() / 100.0;
                    inverterData.gridSellTotal = ((doc["Data"][87].as<uint32_t>() << 16) + doc["Data"][86].as<uint16_t>()) / 100.0; 
                    inverterData.gridBuyTotal = ((doc["Data"][89].as<uint32_t>() << 16) + doc["Data"][86].as<uint16_t>()) / 100.0; 
                    inverterData.batteryChargedToday = doc["Data"][79].as<uint16_t>() / 10.0;
                    inverterData.batteryDischargedToday = doc["Data"][78].as<uint16_t>() / 10.0;
                    inverterData.batteryChargedTotal = ((doc["Data"][77].as<uint32_t>() << 16) + doc["Data"][76].as<uint16_t>()) / 10.0; 
                    inverterData.batteryDischargedTotal = ((doc["Data"][75].as<uint32_t>() << 16) + doc["Data"][74].as<uint16_t>()) / 10.0; 
                    inverterData.loadTotal = inverterData.yieldTotal + inverterData.gridBuyTotal;
                }
                else
                {
                    inverterData.status = SOLAX_DONGLE_STATUS_JSON_ERROR;
                }
            }
            else
            {
                inverterData.status = SOLAX_DONGLE_STATUS_HTTP_ERROR;
            }
            http.end();
        }
        else
        {
            inverterData.status = SOLAX_DONGLE_STATUS_CONNECTION_ERROR;
        }
        return inverterData;
    }

    String getStatusText(SolaxDongleStatus_t status)
    {
        switch (status)
        {
        case SOLAX_DONGLE_STATUS_OK:
            return "OK";
        case SOLAX_DONGLE_STATUS_CONNECTION_ERROR:
            return "Connection error";
        case SOLAX_DONGLE_STATUS_HTTP_ERROR:
            return "HTTP error";
        case SOLAX_DONGLE_STATUS_JSON_ERROR:
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