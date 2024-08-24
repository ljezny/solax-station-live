#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>

#include "Inverters/InverterResult.hpp"

class SolaxWallboxDongleAPI
{
public:
    WallboxData_t loadData(String sn)
    {
        WallboxData_t wallboxData;
        String url = "http://192.168.10.10";
        HTTPClient http;
        http.setConnectTimeout(1000);
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
                    wallboxData.status = DONGLE_STATUS_OK;
                    wallboxData.power = doc["Data"][11].as<int>();
                    wallboxData.sn = sn;
                }
                else
                {
                    wallboxData.status = DONGLE_STATUS_JSON_ERROR;
                }
            }
            else
            {
                wallboxData.status = DONGLE_STATUS_HTTP_ERROR;
            }
            http.end();
        }
        else
        {
            wallboxData.status = DONGLE_STATUS_CONNECTION_ERROR;
        }
        return wallboxData;
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