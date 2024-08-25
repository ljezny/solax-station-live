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
        return readData();
    }

private:
    WiFiUDP udp;

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

    bool connect()
    {
        return udp.begin(WiFi.localIP(), 8899);
    }

    void disconnect()
    {
        udp.stop();
    }

    bool sendRunningDataRequestPacket()
    {
        if (!udp.beginPacket(IPAddress(10, 10, 100, 253), 8899))
        {
            log_d("Failed to begin packet");
            return false;
        }

        byte d[] = {0xF7, 0x03, 0, 0, 0, 0, 0, 0};
        uint16_t addr = 35100;
        uint8_t len = 125;
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

    bool sendBMSInfoRequestPacket()
    {
        if (!udp.beginPacket(IPAddress(10, 10, 100, 253), 8899))
        {
            log_d("Failed to begin packet");
            return false;
        }

        byte d[] = {0xF7, 0x03, 0, 0, 0, 0, 0, 0};
        uint16_t addr = 37000;
        uint8_t len = 8;
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

    InverterData_t readData()
    {
        InverterData_t inverterData;
        log_d("Connecting to dongle...");
        if (connect())
        {
            log_d("Connected.");
            int PACKET_SIZE = 259; // WHY?
            byte packetBuffer[PACKET_SIZE];
            
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
                            inverterData.pv1Power = readUInt32(packetBuffer, 5);
                            inverterData.pv2Power = readUInt32(packetBuffer, 9);
                            inverterData.pv3Power = readUInt32(packetBuffer, 13);
                            inverterData.pv4Power = readUInt32(packetBuffer, 17); 
                            inverterData.inverterPower = readInt16(packetBuffer, 38);
                            inverterData.batteryPower -= readInt16(packetBuffer, 83); //TODO: maybe sign readuw(packetBuffer, 84);
                            // _ac = readsw(packetBuffer, 40);
                            inverterData.L1Power = readInt16(packetBuffer, 25);// - readInt16(packetBuffer, 64) + readInt16(packetBuffer, 50);
                            inverterData.L2Power = readInt16(packetBuffer, 30);// - readInt16(packetBuffer, 66) + readInt16(packetBuffer, 56);
                            inverterData.L3Power = readInt16(packetBuffer, 35);// - readInt16(packetBuffer, 68) + readInt16(packetBuffer, 62);
                            inverterData.feedInPower = 
                                readInt16(packetBuffer, 25) 
                                + readInt16(packetBuffer, 30) 
                                + readInt16(packetBuffer, 35)
                                - readInt16(packetBuffer, 64) - readInt16(packetBuffer, 50)
                                - readInt16(packetBuffer, 66) - readInt16(packetBuffer, 56)
                                - readInt16(packetBuffer, 68) - readInt16(packetBuffer, 62);
                            inverterData.loadPower = readInt16(packetBuffer, 72) + readInt16(packetBuffer, 70);
                            inverterData.inverterTemperature = readInt16(packetBuffer, 76) / 10;
                            inverterData.pvTotal = readUInt32(packetBuffer, 91) / 10;
                            inverterData.pvToday = readUInt32(packetBuffer, 93) / 10;
                            inverterData.loadToday = readUInt16(packetBuffer, 105) / 10;
                            inverterData.batteryChargedToday = readUInt16(packetBuffer, 108) / 10;
                            inverterData.batteryDischargedToday = readUInt16(packetBuffer, 111) / 10;
                            inverterData.gridBuyToday = readUInt16(packetBuffer, 102) / 10;
                            inverterData.gridSellToday = readUInt16(packetBuffer, 99) / 10;                            
                        }
                    }
                }
            }

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
                        }
                    }
                }
            }
        }
        disconnect();
        return inverterData;
    }
};