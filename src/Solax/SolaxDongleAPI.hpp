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
    int batteryPower;
    int L1Power;
    int L2Power;
    int L3Power;
    int32_t feedInPower;
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
                StaticJsonDocument<8192> doc;
                String payload = http.getString();
                DeserializationError err = deserializeJson(doc, payload);
                if (err == DeserializationError::Ok)
                {
                    inverterData.status = 0;
                    inverterData.pv1Power = doc["Data"][14].as<int>();
                    inverterData.pv2Power = doc["Data"][15].as<int>();
                    inverterData.batteryPower = doc["Data"][41].as<int>();
                    inverterData.batteryTemperature = doc["Data"][105].as<int>();
                    inverterData.L1Power = doc["Data"][6].as<int>();
                    inverterData.L2Power = doc["Data"][7].as<int>();
                    inverterData.L3Power = doc["Data"][8].as<int>();
                    inverterData.soc = doc["Data"][103].as<int>();
                    inverterData.yieldToday = doc["Data"][70].as<double>() / 10.0;
                    inverterData.yieldTotal = read32BitUnsigned(doc["Data"][68].as<uint32_t>(), doc["Data"][69].as<uint32_t>());
                    inverterData.feedInPower = read32BitSigned(doc["Data"][34].as<int32_t>(), doc["Data"][35].as<int32_t>());
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
    uint32_t read32BitUnsigned(uint32_t a, uint32_t b)
    {
        return b + 65536 * a;
    }

    int32_t read32BitSigned(int32_t a, int32_t b)
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