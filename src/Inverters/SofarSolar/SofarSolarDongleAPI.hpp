#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <CRC.h>
#include <CRC16.h>
#include "Inverters/InverterResult.hpp"

class SofarSolarDongleAPI
{
public:
    InverterData_t loadData(String sn)
    {
        return readData(sn);
    }

private:
    WiFiClient client;

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

    float readIEEE754(byte *buf, byte reg)
    {
        uint32_t v = readUInt32(buf, reg);
        return *(float *)&v;
    }

    bool connect()
    {
        if (!client.connect(IPAddress(10, 10, 100, 254), 8899))
        {
            log_d("Failed to begin packet");
            return false;
        }
    }

    void disconnect()
    {
        client.stop();
    }

    bool sendDataRequest(uint16_t addr, uint8_t len)
    {
        byte d[] = {0x1, 0x03, 0, 0, 0, 0, 0, 0};
        d[2] = addr >> 8;
        d[3] = addr & 0xff;
        d[5] = len;
        unsigned c = crc16(d, 6, 0x8005, 0xFFFF, 0, true, true);
        d[6] = c;
        d[7] = c >> 8;
        log_d("Sending packet: ");
        for (int i = 0; i < sizeof(d); i++)
        {
            log_d("%02X ", d[i]);
        }
        client.write(d, sizeof(d));
        client.flush();

        // if (!udp.endPacket())
        // {
        //     log_d("Failed to send packet");
        //     return false;
        // }

        return true;
    }

    bool sendRunningDataRequestPacket()
    {
        return sendDataRequest(0x0200 , 69);
    }

    bool awaitPacket(int timeout)
    {
        return client.available() > 0;
        // unsigned long start = millis();
        // while (millis() - start < timeout)
        // {
        //     int packetSize = udp.parsePacket();
        //     if (packetSize)
        //     {
        //         return true;
        //     }
        // }
        // return false;
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
                    int len = client.read(packetBuffer, PACKET_SIZE);
                    if (len > 0)
                    {
                        unsigned c = crc16(packetBuffer + 2, len - 2, 0x8005, 0xFFFF, 0, true, true);
                        if (c == 0)
                        {
                            if (len == 7 + 2 * 69)
                            {
                                inverterData.status = DONGLE_STATUS_OK;
                                inverterData.millis = millis();
                                inverterData.pv1Power = readUInt32(packetBuffer, 0x14);
                                
                                // inverterData.inverterPower = readInt16(packetBuffer, 38);
                                // inverterData.batteryPower -= readInt16(packetBuffer, 83); // TODO: maybe sign readuw(packetBuffer, 84);
                                // // _ac = readsw(packetBuffer, 40);
                                // inverterData.L1Power = readInt16(packetBuffer, 25); // - readInt16(packetBuffer, 64) + readInt16(packetBuffer, 50);
                                // inverterData.L2Power = readInt16(packetBuffer, 30); // - readInt16(packetBuffer, 66) + readInt16(packetBuffer, 56);
                                // inverterData.L3Power = readInt16(packetBuffer, 35); // - readInt16(packetBuffer, 68) + readInt16(packetBuffer, 62);
                                // inverterData.feedInPower =
                                //     readInt16(packetBuffer, 25) + readInt16(packetBuffer, 30) + readInt16(packetBuffer, 35) - readInt16(packetBuffer, 64) - readInt16(packetBuffer, 50) - readInt16(packetBuffer, 66) - readInt16(packetBuffer, 56) - readInt16(packetBuffer, 68) - readInt16(packetBuffer, 62);
                                // inverterData.loadPower = readInt16(packetBuffer, 72) + readInt16(packetBuffer, 70);
                                // inverterData.inverterTemperature = readInt16(packetBuffer, 74) / 10;
                                // inverterData.pvTotal = readUInt32(packetBuffer, 91) / 10.0;
                                // inverterData.pvToday = readUInt32(packetBuffer, 93) / 10.0;
                                // inverterData.loadToday = readUInt16(packetBuffer, 105) / 10.0;
                                // inverterData.loadTotal = readUInt32(packetBuffer, 103) / 10.0;
                                // inverterData.batteryChargedToday = readUInt16(packetBuffer, 108) / 10.0;
                                // inverterData.batteryDischargedToday = readUInt16(packetBuffer, 111) / 10.0;
                                inverterData.sn = sn;
                                logInverterData(inverterData);

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
        }
        disconnect();
        return inverterData;
    }
};