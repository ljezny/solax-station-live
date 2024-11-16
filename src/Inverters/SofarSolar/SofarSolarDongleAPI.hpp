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

    uint8_t sequenceNumber = 0;

    uint16_t readUInt16(byte *buf, byte reg)
    {
        return (buf[3 + reg * 2] << 8 | buf[3 + reg * 2 + 1]);
    }

    int16_t readInt16(byte *buf, byte reg)
    {
        return (buf[3 + reg * 2] << 8 | buf[3 + reg * 2 + 1]);
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

    bool sendReadDataRequest(uint8_t sequenceNumber, uint16_t addr, uint8_t len, uint32_t sn)
    {
        byte modbusRTURequest[] = {0x1, 0x03, 0, 0, 0, 0, 0, 0};
        modbusRTURequest[2] = addr >> 8;
        modbusRTURequest[3] = addr & 0xff;
        modbusRTURequest[5] = len;
        unsigned c = crc16(modbusRTURequest, 6, 0x8005, 0xFFFF, 0, true, true);
        modbusRTURequest[6] = c;
        modbusRTURequest[7] = c >> 8;

        byte request[] = {
            0xA5 /*Start of packet*/,
            0x17 /*Packet length*/,
            0x00 /*Packet length*/,
            0x10 /*Packet type, request*/,
            0x45 /*Packet type, request*/,
            sequenceNumber & 0xff /*Request number*/,
            0 /*Request number*/,
            sn & 0xff /*Serial number*/,
            (sn >> 8) & 0xff /*Serial number*/,
            (sn >> 16) & 0xff /*Serial number*/,
            (sn >> 24) & 0xff /*Serial number*/,0x02 /*Frame type*/,
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
            modbusRTURequest[0],
            modbusRTURequest[1],
            modbusRTURequest[2],
            modbusRTURequest[3],
            modbusRTURequest[4],
            modbusRTURequest[5],
            modbusRTURequest[6],
            modbusRTURequest[7],
            0,
            0x15
        };
        
        int checksum = 0;
        for (int i = 1; i < sizeof(request) - 2; i++)
        {
            checksum += request[i] & 0xff;
        }
        request[sizeof(request) - 2] = checksum & 0xff;

        log_d("Sending solarmanv5 request: ");
        for (int i = 0; i < sizeof(request); i++)
        {
            log_d("%02X ", request[i]);
        }
        
        client.write(request, sizeof(request));
        return true;
    }

    bool sendRunningDataRequestPacket(uint32_t sn)
    {
        return sendReadDataRequest(sequenceNumber, 0x0668, 1, sn);
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
            delay(10);
        }
        return false;
    }

    InverterData_t readData(String dongleSN)
    {
        InverterData_t inverterData;
        log_d("Connecting to dongle...");
        uint32_t sn = strtoul(dongleSN.c_str(), NULL, 10);
        log_d("SN: %d", sn);
        if (connect())
        {
            log_d("Connected.");
            byte packetBuffer[1024];
            
            sequenceNumber++;
            sendRunningDataRequestPacket(sn);

            if(!awaitPacket(3000)) {
                log_d("Response timeout");
                disconnect();
                return inverterData;
            }
            if(client.read(packetBuffer, 1) != 1) {
                log_d("Unable to read client.");
                disconnect();
                return inverterData;
            }
            if(packetBuffer[0] != 0xA5) {
                log_d("Invalid header");
                disconnect();
                return inverterData;
            }
            if(client.read(packetBuffer, 2) != 2) {
                log_d("Unable to read client.");
                disconnect();
                return inverterData;
            }
            uint16_t length = packetBuffer[0] | (packetBuffer[1] << 8);
            log_d("Payload length: %d", length);
            if(client.read(packetBuffer, 8) != 8) { //read rest of header
                log_d("Unable to read client.");
                disconnect();
                return inverterData;
            }

            if(packetBuffer[0] != 0x10 || packetBuffer[1] != 0x15) {
                log_d("Invalid response");
                disconnect();
                return inverterData;
            }
            
            int PAYLOAD_HEADER = 14;

            if(client.read(packetBuffer, PAYLOAD_HEADER) != PAYLOAD_HEADER) { //payload header
                log_d("Unable to read client.");
                disconnect();
                return inverterData;
            }
            
            if(packetBuffer[0] != 0x02 || packetBuffer[1] != 0x01) {

                log_d("Invalid sensor in response");
                disconnect();
                return inverterData;
            }

            int MODBUS_RTU_FRAME_LENGTH = length - PAYLOAD_HEADER;
            if(client.read(packetBuffer, MODBUS_RTU_FRAME_LENGTH) != MODBUS_RTU_FRAME_LENGTH ) { //modbus rtu packet
                log_d("Unable to read client.");
                disconnect();
                return inverterData;
            }

            for (int i = 0; i < MODBUS_RTU_FRAME_LENGTH; i++)
            {
                log_d("%02X ", packetBuffer[i]);
            } 

            inverterData.status = DONGLE_STATUS_OK;
            inverterData.millis = millis();
            inverterData.soc = readUInt16(packetBuffer, 0);
            //inverterData.pv1Power = readUInt32(packetBuffer, 0x0) * 10;
            //inverterData.pv2Power = readUInt32(packetBuffer, 0x1);
            //inverterData.pv3Power = readUInt32(packetBuffer, 0x2);

            if(client.read(packetBuffer, 2) != 2) { //read trailer
                log_d("Unable to read client.");
                disconnect();
                return inverterData;
            }
            
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
        disconnect();
        return inverterData;
    }
};