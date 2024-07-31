#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>

typedef struct
{
    int status = -1;
    int pv1Power;
    int pv2Power;
    int soc;
    int16_t batteryPower;
    double batteryChargedToday;
    double batteryDischargedToday;
    double gridBuyToday;
    double gridSellToday;
    int L1Power;
    int L2Power;
    int L3Power;
    int16_t loadPower;
    int32_t feedInPower;
    int inverterTemperature;
    int batteryTemperature;
    double yieldToday;
    uint32_t yieldTotal;
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
                    inverterData.status = 0;
                    inverterData.pv1Power = doc["Data"][14].as<int>();
                    inverterData.pv2Power = doc["Data"][15].as<int>();
                    inverterData.batteryPower = read16BitSigned(doc["Data"][41].as<uint16_t>());
                    inverterData.batteryTemperature = doc["Data"][105].as<uint8_t>();
                    inverterData.inverterTemperature = doc["Data"][54].as<uint8_t>();
                    inverterData.L1Power = doc["Data"][6].as<int>();
                    inverterData.L2Power = doc["Data"][7].as<int>();
                    inverterData.L3Power = doc["Data"][8].as<int>();
                    inverterData.loadPower = read16BitSigned(doc["Data"][47].as<uint16_t>());
                    inverterData.soc = doc["Data"][103].as<int>();
                    inverterData.yieldToday = doc["Data"][13].as<uint16_t>() / 10.0;
                    inverterData.yieldTotal = read32BitUnsigned(doc["Data"][11].as<uint16_t>(), doc["Data"][12].as<uint16_t>()) / 10.0;;
                    inverterData.feedInPower = read32BitSigned(doc["Data"][34].as<uint16_t>(), doc["Data"][35].as<uint16_t>());
                    inverterData.gridSellToday = doc["Data"][90].as<uint16_t>() / 100.0; 
                    inverterData.gridBuyToday = doc["Data"][92].as<uint16_t>() / 100.0;
                    inverterData.batteryChargedToday = doc["Data"][79].as<uint16_t>() / 10.0;
                    inverterData.batteryDischargedToday = doc["Data"][78].as<uint16_t>() / 10.0;
                }
                else
                {
                    inverterData.status = -3;
                }
            }
            else
            {
                inverterData.status = -2;
            }
            http.end();
        }
        else
        {
            inverterData.status = -1;
        }
        return inverterData;
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

    uint32_t read32BitUnsigned(uint16_t a, uint16_t b)
    {
        return b + 65536 * a;
    }

    int32_t read32BitSigned(uint16_t a, uint16_t b)
    {
        if (a < 32768)
        {
            return b + 65536 * a;
        }
        else
        {
            return b + 65536 * a - 4294967296;
        }
    }
};