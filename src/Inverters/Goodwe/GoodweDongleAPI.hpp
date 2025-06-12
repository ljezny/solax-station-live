#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <CRC.h>
#include <CRC16.h>
#include "Inverters/InverterResult.hpp"

class GoodweDongleAPI
{
public:
    InverterData_t loadData(String sn)
    {
        return readData(sn);
    }

private:
    WiFiUDP udp;

    double gridBuyTotal = 0;
    double gridSellTotal = 0;
    int day = -1;

    uint16_t readUInt16(byte *buf, byte reg)
    {
        return (buf[5 + reg * 2] << 8 | buf[6 + reg * 2]);
    }

    int16_t readInt16(byte *buf, byte reg)
    {
        return (buf[5 + reg * 2] << 8 | buf[6 + reg * 2]);
    }

    uint32_t readUInt32(byte *buf, byte reg)
    {
        return readUInt16(buf, reg) << 16 | readUInt16(buf, reg + 1);
    }

    int32_t readInt32(byte *buf, byte reg)
    {
        return readInt16(buf, reg) << 16 | readInt16(buf, reg + 1);
    }

    float readIEEE754(byte *buf, byte reg)
    {
        uint32_t v = readUInt32(buf, reg);
        return *(float *)&v;
    }

    bool connect()
    {
        return udp.begin(WiFi.localIP(), 8899);
    }

    void disconnect()
    {
        udp.stop();
    }

    bool sendDataRequest(uint16_t addr, uint8_t len)
    {
        udp.clear(); // clear rx buffer
        if (!udp.beginPacket(IPAddress(10, 10, 100, 253), 8899))
        {
            log_d("Failed to begin packet");
            return false;
        }

        byte d[] = {0xF7, 0x03, 0, 0, 0, 0, 0, 0};
        d[2] = addr >> 8;
        d[3] = addr & 0xff;
        d[5] = len;
        unsigned c = crc16(d, 6, 0x8005, 0xFFFF, 0, true, true);
        d[6] = c;
        d[7] = c >> 8;
        udp.write(d, sizeof(d));

        if (!udp.endPacket())
        {
            log_d("Failed to send packet");
            return false;
        }

        return true;
    }

    bool sendRunningDataRequestPacket()
    {
        return sendDataRequest(35100, 125);
    }

    bool sendBMSInfoRequestPacket()
    {
        return sendDataRequest(37000, 8);
    }

    bool sendSmartMeterRequestPacket()
    {
        return sendDataRequest(36000, 44);
    }

    bool awaitPacket(int timeout)
    {
        unsigned long start = millis();
        while (millis() - start < timeout)
        {
            int packetSize = udp.parsePacket();
            if (packetSize)
            {
                return true;
            }
        }
        return false;
    }

    InverterData_t readData(String sn)
    {
        InverterData_t inverterData;
        log_d("Connecting to dongle...");
        if (connect())
        {
            log_d("Connected.");
            int PACKET_SIZE = 259; // WHY?
            byte packetBuffer[PACKET_SIZE];

            for (int i = 0; i < 3; i++)
            {
                sendRunningDataRequestPacket();
                if (awaitPacket(3000))
                {
                    int len = udp.read(packetBuffer, PACKET_SIZE);
                    if (len > 0)
                    {
                        unsigned c = crc16(packetBuffer + 2, len - 2, 0x8005, 0xFFFF, 0, true, true);
                        if (c == 0)
                        {
                            if (len == 7 + 2 * 125)
                            {
                                inverterData.status = DONGLE_STATUS_OK;
                                inverterData.millis = millis();
                                inverterData.pv1Power = readUInt32(packetBuffer, 5);
                                inverterData.pv2Power = readUInt32(packetBuffer, 9);
                                inverterData.pv3Power = readUInt32(packetBuffer, 13);
                                inverterData.pv4Power = readUInt32(packetBuffer, 17);

                                inverterData.batteryPower -= readInt16(packetBuffer, 83); // TODO: maybe sign readuw(packetBuffer, 84);
                                // _ac = readsw(packetBuffer, 40);
                                inverterData.L1Power = readInt16(packetBuffer, 25) + readInt16(packetBuffer, 50);
                                inverterData.L2Power = readInt16(packetBuffer, 30) + readInt16(packetBuffer, 56);
                                inverterData.L3Power = readInt16(packetBuffer, 35) + readInt16(packetBuffer, 62);
                                inverterData.inverterPower = inverterData.L1Power + inverterData.L2Power + inverterData.L3Power; // readInt16(packetBuffer, 38); //38 - total inverter power
                                bool backupConnectedToLoad = readUInt16(packetBuffer, 60) == 0x00;                               // 0x00 - backup is connected to load, 0x01 - inverter disconnects to load
                                if (backupConnectedToLoad)
                                {
                                    inverterData.loadPower = readInt16(packetBuffer, 72); // total load power
                                }
                                else
                                {
                                    inverterData.loadPower = readInt16(packetBuffer, 70)    // total backup load power
                                                             + readInt16(packetBuffer, 72); // total load power
                                }
                                inverterData.inverterTemperature = readInt16(packetBuffer, 74) / 10;
                                inverterData.pvTotal = readUInt32(packetBuffer, 91) / 10.0;
                                inverterData.pvToday = readUInt32(packetBuffer, 93) / 10.0;
                                inverterData.loadToday = readUInt16(packetBuffer, 105) / 10.0;
                                inverterData.loadTotal = readUInt32(packetBuffer, 103) / 10.0; 
                                inverterData.batteryChargedToday = readUInt16(packetBuffer, 108) / 10.0;
                                inverterData.batteryDischargedToday = readUInt16(packetBuffer, 111) / 10.0;
                                inverterData.sn = sn;
                                logInverterData(inverterData);

                                // this is a hack - Goodwe returns incorrect day values for grid sell/buy
                                // so count it manually from total values
                                int day = (readUInt16(packetBuffer, 1) >> 8) & 0xFF;
                                log_d("Day: %d", day);
                                if (this->day != day)
                                {
                                    log_d("Day changed, resetting counters");
                                    this->day = day;
                                    gridBuyTotal = 0;
                                    gridSellTotal = 0;
                                }
                                break;
                            }
                            else
                            {
                                log_d("Invalid packet size: %d", len);
                            }
                        }
                        else
                        {
                            log_d("CRC error");
                        }
                    }
                    else
                    {
                        log_d("No data received");
                    }
                }
            }

            for (int i = 0; i < 3; i++)
            { // it is UDP so retries are needed
                sendSmartMeterRequestPacket();
                if (awaitPacket(3000))
                {
                    int len = udp.read(packetBuffer, PACKET_SIZE);
                    if (len > 0)
                    {
                        unsigned c = crc16(packetBuffer + 2, len - 2, 0x8005, 0xFFFF, 0, true, true);
                        if (c == 0)
                        {
                            if (len == 7 + 2 * 44)
                            {
                                inverterData.gridSellTotal = readIEEE754(packetBuffer, 15) / 1000.0f;
                                inverterData.gridBuyTotal = readIEEE754(packetBuffer, 17) / 1000.0f;

                                // debug logging
                                int register25 = readInt32(packetBuffer, 25);
                                log_d("Register 25: %d", register25);
                                int register8 = readInt16(packetBuffer, 8);
                                log_d("Register 8: %d", register8);
                                // end debug logging

                                inverterData.feedInPower = readInt32(packetBuffer, 25);

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
                        }
                    }
                }
            }

            for (int i = 0; i < 3; i++)
            { // it is UDP so retries are needed
                sendBMSInfoRequestPacket();
                if (awaitPacket(3000))
                {
                    int len = udp.read(packetBuffer, PACKET_SIZE);
                    if (len > 0)
                    {
                        unsigned c = crc16(packetBuffer + 2, len - 2, 0x8005, 0xFFFF, 0, true, true);
                        if (c == 0)
                        {
                            if (len == 7 + 2 * 8)
                            {
                                inverterData.batteryTemperature = readUInt16(packetBuffer, 3) / 10;
                                inverterData.soc = readUInt16(packetBuffer, 7);
                                break;
                            }
                        }
                    }
                }
            }
        }
        inverterData.hasBattery = inverterData.soc != 0 || inverterData.batteryPower != 0;
        disconnect();
        logInverterData(inverterData);
        return inverterData;
    }
};