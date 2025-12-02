#pragma once

#include <Arduino.h>
#include <algorithm>
#include "../../Protocol/ModbusRTU.hpp"
#include "../../Protocol/ModbusTCP.hpp"
#include "Inverters/InverterResult.hpp"

class GoodweDongleAPI
{
public:
    /**
     * Returns true if this inverter supports intelligence mode control
     */
    bool supportsIntelligence() { return false; }

    InverterData_t loadData(String ipAddress)
    {
        return readData(ipAddress);
    }

private:
    ModbusRTU rtuChannel;
    //ModbusTCP tcpChannel;
    IPAddress ip = IPAddress(0, 0, 0, 0); // default IP address
    double gridBuyTotal = 0;
    double gridSellTotal = 0;
    int day = -1;
    const int RETRY_COUNT = 5;
    ModbusResponse sendSNDataRequestPacket(IPAddress ip)
    {
        // if (tcpChannel.isConnected())
        // {
        //     return tcpChannel.sendModbusRequest(0xF7, 0x03, 35003, 8);
        // }
        return rtuChannel.sendDataRequest(ip, 8899, 35003, 8);
    }

    ModbusResponse sendRunningDataRequestPacket(IPAddress ip)
    {
        // if (tcpChannel.isConnected())
        // {
        //     return tcpChannel.sendModbusRequest(0xF7, 0x03, 35100, 125);
        // }
        return rtuChannel.sendDataRequest(ip, 8899, 35100, 125);
    }

    ModbusResponse sendBMSInfoRequestPacket(IPAddress ip)
    {
        // if (tcpChannel.isConnected())
        // {
        //     return tcpChannel.sendModbusRequest(0xF7, 0x03, 37000, 8);
        // }
        return rtuChannel.sendDataRequest(ip, 8899, 37000, 8);
    }

    ModbusResponse sendSmartMeterRequestPacket(IPAddress ip)
    {
        // if (tcpChannel.isConnected())
        // {
        //     return tcpChannel.sendModbusRequest(0xF7, 0x03, 36000, 44);
        // }
        return rtuChannel.sendDataRequest(ip, 8899, 36000, 44);
    }

    InverterData_t readData(String ipAddress)
    {
        if (ip == IPAddress(0, 0, 0, 0))
        {
            if (!ipAddress.isEmpty())
            {
                ip.fromString(ipAddress);
            }

            if (ip == IPAddress(0, 0, 0, 0))
            {
                ip = discoverDongleIP();
                if (ip == IPAddress(0, 0, 0, 0))
                {
                    ip = IPAddress(10, 10, 100, 253);
                }
            }
        }

        InverterData_t inverterData{};
        log_d("Connecting to dongle...%s", ip.toString().c_str());
        if (/*tcpChannel.connect(ip, 502) || */rtuChannel.connect())
        {
            log_d("Connected.");

            for (int i = 0; i < RETRY_COUNT; i++)
            {
                ModbusResponse response = sendRunningDataRequestPacket(ip);
                if (response.isValid)
                {
                    inverterData.status = DONGLE_STATUS_OK;
                    inverterData.pv1Power = response.readUInt32(35100 + 5);
                    inverterData.pv2Power = response.readUInt32(35100 + 9);
                    inverterData.pv3Power = response.readUInt32(35100 + 13);
                    inverterData.pv4Power = response.readUInt32(35100 + 17);

                    inverterData.batteryPower -= response.readInt16(35100 + 83); // TODO: maybe sign readuw(84);
                    // _ac = readsw(40);
                    inverterData.L1Power = response.readInt16(35100 + 25) + response.readInt16(35100 + 50);
                    inverterData.L2Power = response.readInt16(35100 + 30) + response.readInt16(35100 + 56);
                    inverterData.L3Power = response.readInt16(35100 + 35) + response.readInt16(35100 + 62);
                    inverterData.loadPower = inverterData.L1Power + inverterData.L2Power + inverterData.L3Power;

                    inverterData.inverterTemperature = response.readInt16(35100 + 74) / 10;
                    inverterData.pvTotal = response.readUInt32(35100 + 91) / 10.0;
                    inverterData.pvToday = response.readUInt32(35100 + 93) / 10.0;
                    inverterData.loadToday = response.readUInt16(35100 + 105) / 10.0;
                    inverterData.loadTotal = response.readUInt32(35100 + 103) / 10.0;
                    inverterData.batteryChargedToday = response.readUInt16(35100 + 108) / 10.0;
                    inverterData.batteryDischargedToday = response.readUInt16(35100 + 111) / 10.0;
                   
                    // this is a hack - Goodwe returns incorrect day values for grid sell/buy
                    // so count it manually from total values
                    int day = (response.readUInt16(35100 + 1) >> 8) & 0xFF;
                    log_d("Day: %d", day);
                    if (this->day != day)
                    {
                        log_d("Day changed, resetting counters");
                        this->day = day;
                        gridBuyTotal = 0;
                        gridSellTotal = 0;
                    }

                    // Read RTC time from registers 35100+0 and 35100+1
                    // Format: Register 0: (Year-2000) << 8 | Month, Register 1: Day << 8 | Hour
                    // Register 2: Minute << 8 | Second (but we don't have it in this response)
                    uint16_t reg0 = response.readUInt16(35100 + 0);
                    uint16_t reg1 = response.readUInt16(35100 + 1);
                    struct tm timeinfo = {};
                    timeinfo.tm_year = ((reg0 >> 8) & 0xFF) + 100; // Year-2000 + 100 = years since 1900
                    timeinfo.tm_mon = (reg0 & 0xFF) - 1;           // Month 1-12 to 0-11
                    timeinfo.tm_mday = (reg1 >> 8) & 0xFF;
                    timeinfo.tm_hour = reg1 & 0xFF;
                    // Note: GoodWe provides minute/second in register 35100+2, but we approximate
                    timeinfo.tm_min = 0;
                    timeinfo.tm_sec = 0;
                    timeinfo.tm_isdst = -1;
                    inverterData.inverterTime = mktime(&timeinfo);
                    log_d("GoodWe RTC: %04d-%02d-%02d %02d:%02d:%02d",
                          timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                          timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

                    break;
                }
                delay(i * 300); // wait before retrying
            }

            for (int i = 0; i < RETRY_COUNT; i++)
            {
                ModbusResponse response = sendSNDataRequestPacket(ip);
                if (response.isValid)
                {
                    inverterData.sn = response.readString(35003, 8);
                    log_d("Dongle SN: %s", inverterData.sn.c_str());
                    break;
                }
                delay(i * 300); // wait before retrying
            }

            for (int i = 0; i < RETRY_COUNT; i++)
            { // it is UDP so retries are needed
                ModbusResponse response = sendSmartMeterRequestPacket(ip);
                if (response.isValid)
                {
                    inverterData.gridSellTotal = response.readIEEE754(36000 + 15) / 1000.0f;
                    inverterData.gridBuyTotal = response.readIEEE754(36000 + 17) / 1000.0f;

                    // debug logging
                    int register25 = response.readInt32(36000 + 25);
                    log_d("Register 25: %d", register25);
                    int register8 = response.readInt16(36000 + 8);
                    log_d("Register 8: %d", register8);
                    // end debug logging

                    inverterData.gridPowerL1 = response.readInt32(36000 + 19);
                    inverterData.gridPowerL2 = response.readInt32(36000 + 21);
                    inverterData.gridPowerL3 = response.readInt32(36000 + 23);
                    
                    inverterData.loadPower -= (inverterData.gridPowerL1 + inverterData.gridPowerL2 + inverterData.gridPowerL3);
                    
                    log_d("Grid sell total: %f", inverterData.gridSellTotal);
                    log_d("Grid buy total: %f", inverterData.gridBuyTotal);
                    if (gridBuyTotal == 0)
                    {
                        gridBuyTotal = inverterData.gridBuyTotal;
                    }
                    if (gridSellTotal == 0)
                    {
                        gridSellTotal = inverterData.gridSellTotal;
                    }
                    inverterData.gridSellToday = inverterData.gridSellTotal - gridSellTotal;
                    inverterData.gridBuyToday = inverterData.gridBuyTotal - gridBuyTotal;
                    break;
                }
                delay(i * 300); // wait before retrying
            }

            for (int i = 0; i < RETRY_COUNT; i++)
            { // it is UDP so retries are needed
                ModbusResponse response = sendBMSInfoRequestPacket(ip);
                if (response.isValid)
                {

                    inverterData.batteryTemperature = response.readUInt16(37000 + 3) / 10;
                    inverterData.soc = response.readUInt16(37000 + 7);
                    break;
                }
                delay(i * 300); // wait before retrying
            }
        }
        inverterData.hasBattery = inverterData.soc != 0 || inverterData.batteryPower != 0;
        //tcpChannel.disconnect();
        rtuChannel.disconnect();
        logInverterData(inverterData);

        if (inverterData.status != DONGLE_STATUS_OK)
        {
            log_d("Failed to read data from dongle, status: %d", inverterData.status);
            ip = IPAddress(0, 0, 0, 0); // reset IP address to force discovery next time
        }

        return inverterData;
    }

    IPAddress discoverDongleIP()
    {
        IPAddress dongleIP;
        WiFiUDP udp;
        String message = "WIFIKIT-214028-READ";
        udp.beginPacket(IPAddress(255, 255, 255, 255), 48899);
        udp.write((const uint8_t *)message.c_str(), (size_t)message.length());
        udp.endPacket();

        unsigned long start = millis();
        while (millis() - start < 3000)
        {
            int packetSize = udp.parsePacket();
            if (packetSize)
            {
                // On success, the inverter responses with it's IP address (as a text string) followed by it's WiFi AP name.
                char d[128] = {0};
                udp.read(d, sizeof(d));

                log_d("Received IP address: %s", String(d).c_str());
                int indexOfComma = String(d).indexOf(',');
                String ip = String(d).substring(0, indexOfComma);
                log_d("Parsed IP address: %s", ip.c_str());
                dongleIP.fromString(ip);
                log_d("Dongle IP: %s", dongleIP.toString());
                break;
            }
        }
        udp.stop();
        return dongleIP;
    }

    bool setWorkMode(const String& ipAddress, InverterMode_t mode)
    {
        // TODO: Not implemented yet
        return false;
    }
};