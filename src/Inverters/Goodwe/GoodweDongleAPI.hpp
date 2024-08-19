#pragma once

#include <Arduino.h>
#include <Wifi.h>
#include <WiFiUdp.h>
#include <CRC.h>
#include <CRC16.h>
#include "Inverters/InverterResult.hpp"

class GoodweDongleAPI
{
public:
    InverterData_t loadData(String sn)
    {
        InverterData_t inverterData;
        return inverterData;
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
        return udp.begin(IPAddress(10, 10, 100, 253), 8899);
    }

    void disconnect()
    {
        udp.stop();
    }

    bool sendPacket()
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
        if (connect())
        {
            sendPacket();
            if (awaitPacket(3000))
            {
                int PACKET_SIZE = 259; // WHY?
                byte packetBuffer[PACKET_SIZE];
                int len = udp.read(packetBuffer, PACKET_SIZE);
                if (len > 0)
                {
                    unsigned c = crc16(packetBuffer + 2, len - 2, 0x8005, 0xFFFF, 0, true, true);
                    if (c == 0)
                    {
                        if (len == 7 + 2 * 125)
                        {
                            inverterData.status = DONGLE_STATUS_OK;
                            inverterData.pv1Power = readUInt16(packetBuffer, 5);
                            inverterData.pv2Power = readUInt16(packetBuffer, 9);
                            // _g1 = readsw(packetBuffer, 25);
                            // _g2 = readsw(packetBuffer, 30);
                            // _g3 = readsw(packetBuffer, 35);
                            // _inv = readsw(packetBuffer, 38);
                            // _ac = readsw(packetBuffer, 40);
                            // _b1 = readsw(packetBuffer, 50);
                            // _b2 = readsw(packetBuffer, 56);
                            // _b3 = readsw(packetBuffer, 62);
                            inverterData.L1Power = readInt16(packetBuffer, 64) + readInt16(packetBuffer, 50);
                            inverterData.L2Power = readInt16(packetBuffer, 66) + readInt16(packetBuffer, 56);
                            inverterData.L3Power = readInt16(packetBuffer, 68) + readInt16(packetBuffer, 62);
                            inverterData.loadPower = readInt16(packetBuffer, 72) + readInt16(packetBuffer, 70);
                            // _batt = readsw(packetBuffer, 83);
                            // _bam = readuw(packetBuffer, 84);
                        }
                    }
                }
            }
        }
        disconnect();
        return inverterData;
    }
};