#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <NetworkClient.h>
#include "../InverterResult.hpp"
#include <float.h>

#define SOLAX_DONGLE_TIMEOUT_MS 5000
class SolaxDongleAPI
{
public:
    SolaxDongleAPI()
    {
    }

    float voltageToPercent(float voltage, float minVoltage, float maxVoltage)
    {
        // Check for valid voltage range
        if (voltage < minVoltage)
        {
            return 0.0f; // Below minimum voltage, 0% charge
        }
        else if (voltage > maxVoltage)
        {
            return 100.0f; // Above maximum voltage, 100% charge
        }
        else if (maxVoltage - minVoltage < 0.1)
        {                // division by zero
            return 0.0f; // Above maximum voltage, 100% charge
        }

        float percent = ((voltage - minVoltage) / (maxVoltage - minVoltage)) * 100.0f;
        return percent;
    }

    InverterData_t loadData(String sn)
    {
        InverterData_t inverterData;
        NetworkClient client;
        client.setTimeout(SOLAX_DONGLE_TIMEOUT_MS);
        if (client.connect(getIp(), 80, SOLAX_DONGLE_TIMEOUT_MS))
        {
            String request = "";
            String body = "optType=ReadRealTimeData&pwd=" + sn;
            request += "POST / HTTP/1.1\r\n";
            request += "Host: " + getIp().toString() + "\r\n";
            request += "Content-Length: " + String(body.length()) + "\r\n";
            request += "\r\n";
            request += body;
            log_d("Request: %s", request.c_str());
            client.write(request.c_str(), request.length());
            log_d("Request sent");
            unsigned long lastDataTime = millis();
            while (client.connected())
            {
                size_t len = client.available();
                if (len > 0)
                {
                    String headerLine = client.readStringUntil('\n');
                    log_d("Response code: %s", headerLine.c_str());
                    if (headerLine.startsWith("HTTP/1.1 200 OK"))
                    {
                        while (!headerLine.isEmpty())
                        {
                            headerLine = client.readStringUntil('\n');
                            headerLine.trim();
                            log_d("Header: %s", headerLine.c_str());
                        }
                        StaticJsonDocument<14 * 1024> doc;
                        log_d("Parsing JSON");
                        
                        DeserializationError err = deserializeJson(doc, client);
                        if (err == DeserializationError::Ok)
                        {
                            if (doc["type"].as<int>() == 14) // X1-Hybrid 3.0
                            {
                                inverterData.status = DONGLE_STATUS_OK;
                                inverterData.millis = millis();
                                inverterData.pv1Power = doc["Data"][14].as<int>();
                                inverterData.pv2Power = doc["Data"][15].as<int>();
                                inverterData.batteryPower = read16BitSigned(doc["Data"][41].as<uint16_t>());
                                inverterData.batteryVoltage = doc["Data"][39].as<int>() / 100.0;
                                inverterData.batteryTemperature = doc["Data"][105].as<uint8_t>();
                                inverterData.inverterTemperature = doc["Data"][54].as<uint8_t>();
                                inverterData.L1Power = ((int16_t)doc["Data"][6].as<uint16_t>());
                                inverterData.L2Power = ((int16_t)doc["Data"][7].as<uint16_t>());
                                inverterData.L3Power = ((int16_t)doc["Data"][8].as<uint16_t>());
                                inverterData.inverterPower = ((int16_t)doc["Data"][9].as<uint16_t>());
                                inverterData.loadPower = read16BitSigned(doc["Data"][47].as<uint16_t>());
                                inverterData.soc = doc["Data"][103].as<int>();
                                if (inverterData.soc == 0 && inverterData.batteryVoltage > 0.0)
                                { // use battery voltage approximation
                                    minimumBatteryVoltage = min(minimumBatteryVoltage, inverterData.batteryVoltage);
                                    log_d("Minimum battery voltage: %f", minimumBatteryVoltage);
                                    maximumBatteryVoltage = max(maximumBatteryVoltage, inverterData.batteryVoltage);
                                    log_d("Maximum battery voltage: %f", maximumBatteryVoltage);
                                    inverterData.soc = voltageToPercent(inverterData.batteryVoltage, minimumBatteryVoltage, maximumBatteryVoltage);
                                    log_d("Battery voltage: %f, SOC: %d", inverterData.batteryVoltage, inverterData.soc);
                                    inverterData.socApproximated = true;
                                }
                                inverterData.pvToday = doc["Data"][70].as<uint16_t>() / 10.0; // yield is PV inverter output (solar + battery)
                                inverterData.pvTotal = ((doc["Data"][69].as<uint32_t>() << 16) + doc["Data"][68].as<uint16_t>()) / 10.0;
                                inverterData.feedInPower = read16BitSigned(doc["Data"][34].as<uint16_t>());
                                inverterData.gridSellToday = doc["Data"][90].as<uint16_t>() / 100.0;
                                inverterData.gridBuyToday = doc["Data"][92].as<uint16_t>() / 100.0;
                                inverterData.gridSellTotal = ((doc["Data"][87].as<uint32_t>() << 16) + doc["Data"][86].as<uint16_t>()) / 100.0;
                                inverterData.gridBuyTotal = ((doc["Data"][89].as<uint32_t>() << 16) + doc["Data"][88].as<uint16_t>()) / 100.0;
                                inverterData.batteryChargedToday = doc["Data"][79].as<uint16_t>() / 10.0;
                                inverterData.batteryDischargedToday = doc["Data"][78].as<uint16_t>() / 10.0;
                                inverterData.loadToday = inverterData.pvToday + inverterData.gridBuyToday - inverterData.gridSellToday;
                                inverterData.hasBattery = inverterData.soc != 0 || inverterData.batteryPower != 0;
                                inverterData.sn = sn;
                                logInverterData(inverterData);
                            }
                            else if (doc["type"].as<int>() == 25) // X3-Ultra https://github.com/squishykid/solax/blob/master/solax/inverters/x3_ultra.py
                            {
                                inverterData.status = DONGLE_STATUS_OK;
                                inverterData.millis = millis();
                                inverterData.pv1Power = doc["Data"][14].as<int>();
                                inverterData.pv2Power = doc["Data"][15].as<int>();
                                inverterData.batteryPower = read16BitSigned(doc["Data"][41].as<uint16_t>()) + read16BitSigned(doc["Data"][134].as<uint16_t>());
                                inverterData.batteryTemperature = doc["Data"][105].as<uint8_t>();
                                inverterData.inverterTemperature = doc["Data"][54].as<uint8_t>();
                                inverterData.L1Power = ((int16_t)doc["Data"][6].as<uint16_t>()) + ((int16_t)doc["Data"][29].as<uint16_t>());
                                inverterData.L2Power = ((int16_t)doc["Data"][7].as<uint16_t>()) + ((int16_t)doc["Data"][30].as<uint16_t>());
                                inverterData.L3Power = ((int16_t)doc["Data"][8].as<uint16_t>()) + ((int16_t)doc["Data"][31].as<uint16_t>());
                                inverterData.inverterPower = inverterData.L1Power + inverterData.L2Power + inverterData.L3Power;
                                inverterData.soc = doc["Data"][158].as<int>();
                                inverterData.pvToday = doc["Data"][70].as<uint16_t>() / 10.0; // yield is PV + battery
                                inverterData.pvTotal = ((doc["Data"][59].as<uint32_t>() << 16) + doc["Data"][58].as<uint16_t>()) / 10.0;
                                inverterData.feedInPower = ((doc["Data"][35].as<uint32_t>() << 16) + doc["Data"][34].as<uint16_t>());
                                inverterData.gridSellToday = doc["Data"][90].as<uint16_t>() / 100.0;
                                inverterData.gridBuyToday = doc["Data"][92].as<uint16_t>() / 100.0;
                                inverterData.gridSellTotal = ((doc["Data"][87].as<uint32_t>() << 16) + doc["Data"][86].as<uint16_t>()) / 100.0;
                                inverterData.gridBuyTotal = ((doc["Data"][89].as<uint32_t>() << 16) + doc["Data"][88].as<uint16_t>()) / 100.0;
                                inverterData.batteryChargedToday = doc["Data"][79].as<uint16_t>() / 10.0;
                                inverterData.batteryDischargedToday = doc["Data"][78].as<uint16_t>() / 10.0;
                                inverterData.loadToday = inverterData.pvToday + inverterData.gridBuyToday - inverterData.gridSellToday;
                                inverterData.loadPower = inverterData.inverterPower - inverterData.feedInPower;
                                inverterData.sn = sn;
                                inverterData.hasBattery = inverterData.soc != 0 || inverterData.batteryPower != 0;
                                logInverterData(inverterData);
                            }
                            else if (doc["type"].as<int>() == 5) // X3-Hybiyd/Fit / X3-20K/30K / X3-MIC/PRO
                            {
                                inverterData.status = DONGLE_STATUS_OK;
                                inverterData.millis = millis();
                                inverterData.pv1Power = doc["Data"][13].as<int>();
                                inverterData.pv2Power = doc["Data"][14].as<int>();
                                inverterData.batteryPower = read16BitSigned(doc["Data"][26].as<uint16_t>());
                                inverterData.batteryVoltage = doc["Data"][24].as<int>() / 100.0;
                                inverterData.batteryTemperature = doc["Data"][27].as<uint8_t>();
                                inverterData.soc = doc["Data"][28].as<int>();
                                inverterData.inverterTemperature = doc["Data"][49].as<uint8_t>(); //??
                                inverterData.L1Power = ((int16_t)doc["Data"][6].as<uint16_t>());
                                inverterData.L2Power = ((int16_t)doc["Data"][7].as<uint16_t>());
                                inverterData.L3Power = ((int16_t)doc["Data"][8].as<uint16_t>());
                                inverterData.inverterPower = ((int16_t)doc["Data"][181].as<uint16_t>()); //???
                                inverterData.pvToday = doc["Data"][112].as<uint16_t>() / 10.0;           // yield is PV inverter output (solar + battery)
                                inverterData.pvTotal = ((doc["Data"][90].as<uint32_t>() << 16) + doc["Data"][89].as<uint16_t>()) / 10.0;
                                inverterData.feedInPower = read16BitSigned(doc["Data"][65].as<uint16_t>());
                                // inverterData.gridSellToday = doc["Data"][90].as<uint16_t>() / 100.0;
                                // inverterData.gridBuyToday = doc["Data"][92].as<uint16_t>() / 100.0;
                                inverterData.gridSellTotal = ((doc["Data"][68].as<uint32_t>() << 16) + doc["Data"][67].as<uint16_t>()) / 100.0;
                                // inverterData.gridBuyTotal = ((doc["Data"][89].as<uint32_t>() << 16) + doc["Data"][88].as<uint16_t>()) / 100.0;
                                inverterData.batteryChargedToday = doc["Data"][114].as<uint16_t>() / 10.0;
                                inverterData.batteryDischargedToday = doc["Data"][113].as<uint16_t>() / 10.0;
                                inverterData.loadToday = doc["Data"][21].as<uint16_t>() / 10.0;
                                inverterData.loadTotal = ((doc["Data"][70].as<uint32_t>() << 16) + doc["Data"][69].as<uint16_t>()) / 10.0;
                                inverterData.hasBattery = inverterData.soc != 0 || inverterData.batteryPower != 0;
                                inverterData.sn = sn;
                                inverterData.loadPower = inverterData.inverterPower - inverterData.feedInPower;
                                logInverterData(inverterData);
                            }
                            else if (doc["type"].as<int>() == 16)
                            { // X3-MIC/PRO-G2 https://github.com/simatec/ioBroker.solax/blob/master/lib/inverterData.js
                                inverterData.status = DONGLE_STATUS_OK;
                                inverterData.millis = millis();
                                inverterData.pv1Power = doc["Data"][15].as<int>();
                                inverterData.pv2Power = doc["Data"][16].as<int>() + doc["Data"][17].as<int>();
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
                                inverterData.inverterTemperature = doc["Data"][39].as<uint8_t>();
                                inverterData.sn = sn;
                                logInverterData(inverterData);
                            }
                            else if (doc["type"].as<int>() == 4)
                            { // X1 boost https://github.com/simatec/ioBroker.solax/blob/master/lib/inverterData.js
                                inverterData.status = DONGLE_STATUS_OK;
                                inverterData.millis = millis();
                                inverterData.pv1Power = doc["Data"][7].as<int>();
                                inverterData.pv2Power = doc["Data"][8].as<int>();
                                inverterData.inverterPower = doc["Data"][2].as<int>();
                                inverterData.pvTotal = ((doc["Data"][12].as<uint32_t>() << 16) + doc["Data"][11].as<uint16_t>()) / 10.0;
                                inverterData.pvToday = doc["Data"][13].as<uint16_t>() / 10.0;
                                inverterData.feedInPower = read16BitSigned(doc["Data"][48].as<uint16_t>());
                                inverterData.loadPower = inverterData.inverterPower - inverterData.feedInPower;
                                inverterData.gridSellTotal = ((doc["Data"][51].as<uint32_t>() << 16) + doc["Data"][50].as<uint16_t>()) / 100.0;
                                inverterData.loadTotal = ((doc["Data"][53].as<uint32_t>() << 16) + doc["Data"][52].as<uint16_t>()) / 100.0;
                                inverterData.hasBattery = false;
                                inverterData.sn = sn;
                                if (pvToday < 0 || inverterData.pvToday < pvToday)
                                { // day changed
                                    pvToday = inverterData.pvToday;
                                    gridBuyTotal = inverterData.gridBuyTotal;
                                    gridSellTotal = inverterData.gridSellTotal;
                                    loadTotal = inverterData.loadTotal;
                                }
                                inverterData.gridBuyToday = inverterData.gridBuyTotal - gridBuyTotal;
                                inverterData.gridSellToday = inverterData.gridSellTotal - gridSellTotal;
                                inverterData.loadToday = inverterData.loadTotal - loadTotal;

                                logInverterData(inverterData);
                            }
                            else if (doc["type"].as<int>() == 1)
                            { // wallbox
                                // wallboxData.power = doc["Data"][11].as<int>();
                                inverterData.status = DONGLE_STATUS_UNSUPPORTED_DONGLE;
                            }
                            else
                            {
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
                }
                else
                {
                    if ((millis() - lastDataTime) > SOLAX_DONGLE_TIMEOUT_MS)
                    {
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

    String
    getStatusText(DongleStatus_t status)
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

    String getUrl()
    {
        if (WiFi.localIP()[0] == 192)
        {
            return "http://192.168.10.10";
        }
        else
        {
            return "http://5.8.8.8";
        }
    }

    IPAddress getIp()
    {
        if (WiFi.localIP()[0] == 192)
        {
            return IPAddress(192, 168, 10, 10);
        }
        else
        {
            return IPAddress(5, 8, 8, 8);
        }
    }

private:

    float minimumBatteryVoltage = FLT_MAX;
    float maximumBatteryVoltage = FLT_MIN;

    double pvToday = -1; // init value
    double pvTotal = 0;
    double batteryDischargedToday = 0;
    double batteryChargedToday = 0;
    double gridBuyTotal = 0;
    double gridSellTotal = 0;
    double loadTotal = 0;

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