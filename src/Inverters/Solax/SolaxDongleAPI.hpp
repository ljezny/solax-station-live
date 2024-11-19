#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <StreamUtils.h>
#include "Inverters/InverterResult.hpp"

#define SOLAX_DONGLE_TIMEOUT_MS 5000
class SolaxDongleAPI
{
public:
    SolaxDongleAPI() {
        client.setTimeout(SOLAX_DONGLE_TIMEOUT_MS);
    }

    InverterData_t loadData(String sn) {
        InverterData_t inverterData;
        
        if(client.connect(getIp(), 80, SOLAX_DONGLE_TIMEOUT_MS)) {
            String body = "optType=ReadRealTimeData&pwd=" + sn;
            client.println("POST / HTTP/1.1");
            client.println("Host: " + getIp().toString());
            client.println("Content-Length: " + String(body.length()));
            client.println();
            client.print(body);
            log_d("Request sent");
            unsigned long lastDataTime = millis();
            while(client.connected()) {
                size_t len = client.available();
                if(len > 0) {
                    String headerLine = client.readStringUntil('\n');
                    log_d("Response code: %s", headerLine.c_str());
                    if(headerLine.startsWith("HTTP/1.1 200 OK")) {
                        while(!headerLine.isEmpty()) {
                            headerLine = client.readStringUntil('\n');
                            headerLine.trim();
                            log_d("Header: %s", headerLine.c_str());                                        
                        }   
                        StaticJsonDocument<14*1024> doc;
                        log_d("Parsing JSON");
                        ReadBufferingStream bufferingStream(client, 1024);
                        LoggingStream loggingStream(bufferingStream, Serial);
                        DeserializationError err = deserializeJson(doc, loggingStream);
                        if (err == DeserializationError::Ok)
                            {
                                if((doc["type"].as<int>() == 14) //X1-Hybrid 
                                || (doc["type"].as<int>() == 25)) //X1-Hybrid 3.0
                                { 
                                    inverterData.status = DONGLE_STATUS_OK;
                                    inverterData.millis = millis();
                                    inverterData.pv1Power = doc["Data"][14].as<int>();
                                    inverterData.pv2Power = doc["Data"][15].as<int>();
                                    inverterData.batteryPower = read16BitSigned(doc["Data"][41].as<uint16_t>());
                                    inverterData.batteryTemperature = doc["Data"][105].as<uint8_t>();
                                    inverterData.inverterTemperature = doc["Data"][54].as<uint8_t>();
                                    inverterData.L1Power = ((int16_t) doc["Data"][6].as<uint16_t>());
                                    inverterData.L2Power = ((int16_t) doc["Data"][7].as<uint16_t>());
                                    inverterData.L3Power = ((int16_t) doc["Data"][8].as<uint16_t>());
                                    inverterData.inverterPower = ((int16_t) doc["Data"][9].as<uint16_t>());
                                    inverterData.loadPower = read16BitSigned(doc["Data"][47].as<uint16_t>());
                                    inverterData.soc = doc["Data"][103].as<int>();
                                    inverterData.pvToday = doc["Data"][70].as<uint16_t>() / 10.0 - doc["Data"][78].as<uint16_t>() / 10.0; //yield is PV + battery
                                    inverterData.pvTotal = ((doc["Data"][69].as<uint32_t>() << 16) + doc["Data"][68].as<uint16_t>()) / 10.0;
                                    inverterData.feedInPower = read16BitSigned(doc["Data"][34].as<uint16_t>());
                                    inverterData.gridSellToday = doc["Data"][90].as<uint16_t>() / 100.0; 
                                    inverterData.gridBuyToday = doc["Data"][92].as<uint16_t>() / 100.0;
                                    inverterData.gridSellTotal = ((doc["Data"][87].as<uint32_t>() << 16) + doc["Data"][86].as<uint16_t>()) / 100.0; 
                                    inverterData.gridBuyTotal = ((doc["Data"][89].as<uint32_t>() << 16) + doc["Data"][88].as<uint16_t>()) / 100.0; 
                                    inverterData.batteryChargedToday = doc["Data"][79].as<uint16_t>() / 10.0;
                                    inverterData.batteryDischargedToday = doc["Data"][78].as<uint16_t>() / 10.0;
                                    inverterData.loadToday = inverterData.pvToday + inverterData.gridBuyToday - inverterData.gridSellToday;
                                    inverterData.sn = sn;
                                    logInverterData(inverterData);
                                } else if(doc["type"].as<int>() == 16) { //X3-MIC/PRO-G2 https://github.com/simatec/ioBroker.solax/blob/master/lib/inverterData.js
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
                                    inverterData.gridBuyToday = doc["Data"][76].as<uint16_t>() / 100.0;
                                    inverterData.hasBattery = false;
                                    inverterData.sn = sn;
                                    logInverterData(inverterData);
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
                                log_d("JSON error");
                            }
                    }
                    else
                    {
                        inverterData.status = DONGLE_STATUS_HTTP_ERROR;
                        log_d("HTTP error");
                    }
                    break;
                } else {
                    if((millis() - lastDataTime) > SOLAX_DONGLE_TIMEOUT_MS) {
                        inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                        log_d("Connection error");
                        break;
                    }
                    delay(10);
                }
            }            
            log_d("Closing connection");
        }
        else
        {
            inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
            log_d("Failed to connect to Solax dongle");
        }
        client.stop();
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

    IPAddress getIp() {
        if(WiFi.localIP()[0] == 192) {
            return IPAddress(192, 168, 10, 10);
        } else {
            return IPAddress(5, 8, 8, 8);
        }
    }
    
private:
    WiFiClient client;
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