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

    uint16_t sequenceNumber = 0;

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
        return true;
    }

    void disconnect()
    {
        client.stop();
    }

    bool sendReadDataRequest(uint16_t sequenceNumber, uint16_t addr, uint8_t len, uint32_t sn)
    {
        byte solarmanv5Header[] = {
            0xA5 /*Start of packet*/,
            0x00 /*Packet length*/,
            0x00 /*Packet length*/,
            0x10 /*Packet type, request*/,
            0x45 /*Packet type, request*/,
            sequenceNumber & 0xff /*Request number*/,
            sequenceNumber >> 8 /*Request number*/,
            sn & 0xff /*Serial number*/,
            (sn >> 8) & 0xff /*Serial number*/,
            (sn >> 16) & 0xff /*Serial number*/,
            (sn >> 24) & 0xff /*Serial number*/,
        };

        byte solarmanv5Payload[] = {
            0x02 /*Frame type*/,
            0x00 /*Sensor type*/,
            0x00 /*Sensor type*/,
            0x00 /*Sensor type*/,
            0x00 /*Total Working Time*/,
            0x00 /*Total Working Time*/,
            0x00 /*Total Working Time*/,
            0x00 /*Total Working Time*/,
            0x00 /*Power on Time*/,
            0x00 /*Power on Time*/,
            0x00 /*Power on Time*/,
            0x00 /*Power on Time*/,
            0x00 /*Offset time*/,
            0x00 /*Offset time*/,
            0x00 /*Offset time*/,
            0x00 /*Offset time*/,
        };

        byte modbusRTURequest[] = {0x1, 0x03, 0, 0, 0, 0, 0, 0};
        modbusRTURequest[2] = addr >> 8;
        modbusRTURequest[3] = addr & 0xff;
        modbusRTURequest[5] = len;
        unsigned c = crc16(modbusRTURequest, 6, 0x8005, 0xFFFF, 0, true, true);
        modbusRTURequest[6] = c;
        modbusRTURequest[7] = c >> 8;
        
        byte solarmanv5Trailer[] = {
            0x00 /*cheksum*/,
            0x15,
        };

        //compute length
        uint16_t packetLength = sizeof(solarmanv5Payload) + sizeof(modbusRTURequest);
        solarmanv5Header[1] = packetLength & 0xff;
        solarmanv5Header[2] = packetLength >> 8;
        
        int checksum = 0;
        for (int i = 1; i < sizeof(solarmanv5Header); i++)
        {
            checksum += solarmanv5Header[i] & 0xff;
        }
        for (int i = 0; i < sizeof(solarmanv5Payload); i++)
        {
            checksum += solarmanv5Payload[i] & 0xff;
        }
        for (int i = 0; i < sizeof(modbusRTURequest); i++)
        {
            checksum += modbusRTURequest[i] & 0xff;
        }
        solarmanv5Trailer[0] = checksum & 0xff;

        log_d("Sending solarmanv5 request: ");
        for (int i = 0; i < sizeof(solarmanv5Header); i++)
        {
            log_d("%02X ", solarmanv5Header[i]);
        }
        for (int i = 0; i < sizeof(solarmanv5Payload); i++)
        {
            log_d("%02X ", solarmanv5Payload[i]);
        }
        for (int i = 0; i < sizeof(modbusRTURequest); i++)
        {
            log_d("%02X ", modbusRTURequest[i]);
        }
        for (int i = 0; i < sizeof(solarmanv5Trailer); i++)
        {
            log_d("%02X ", solarmanv5Trailer[i]);
        }   

        client.write(solarmanv5Header, sizeof(solarmanv5Header));
        client.write(solarmanv5Payload, sizeof(solarmanv5Payload));
        client.write(modbusRTURequest, sizeof(modbusRTURequest));
        client.write(solarmanv5Trailer, sizeof(solarmanv5Trailer));
        client.flush();

        return true;
    }

    bool sendRunningDataRequestPacket(uint32_t sn)
    {
        return sendReadDataRequest(sequenceNumber, 0x0200 , 0x44, sn);
    }

    bool awaitPacket(int timeout)
    {
        unsigned long start = millis();
        while (millis() - start < timeout)
        {
            int packetSize = client.available();
            if (packetSize)
            {
                return true;
            }
        }
        return false;
    }

    InverterData_t readData(String dongleSN)
    {
        InverterData_t inverterData;
        log_d("Connecting to dongle...");
        uint32_t sn = dongleSN.toInt();
        log_d("SN: %d", sn);
        if (connect())
        {
            log_d("Connected.");
            int PACKET_SIZE = 259; // WHY?
            byte packetBuffer[PACKET_SIZE];

            for (int i = 0; i < 3; i++)
            {
                sendRunningDataRequestPacket(sn);
                sequenceNumber++;
                if (awaitPacket(3000))
                {
                    int len = client.read(packetBuffer, PACKET_SIZE);
                    if (len > 0)
                    {
                        log_d("Received packet: ");
                        for (int i = 0; i < len; i++)
                        {
                            log_d("%02X ", packetBuffer[i]);
                        }
                        
                        inverterData.status = DONGLE_STATUS_OK;
                        inverterData.millis = millis();
                        //inverterData.pv1Power = readUInt32(packetBuffer, 0x14);
                        
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
                    }
                }
            }
        }
        disconnect();
        return inverterData;
    }
};