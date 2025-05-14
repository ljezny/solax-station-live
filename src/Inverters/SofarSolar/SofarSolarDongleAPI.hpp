#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <CRC.h>
#include <CRC16.h>
#include <NetworkClient.h>
#include "Inverters/InverterResult.hpp"

#define READ_TIMEOUT 5000

class SofarSolarDongleAPI
{
public:
    InverterData_t loadData(String sn)
    {
        return readData(sn);
    }

private:
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

    bool connect(NetworkClient &client)
    {
        if (!client.connect(IPAddress(10, 10, 100, 254), 8899))
        {
            log_d("Failed to begin packet");
            return false;
        }
        return true;
    }

    void disconnect(NetworkClient &client)
    {
        client.stop();
    }

    bool sendReadDataRequest(NetworkClient &client, uint8_t sequenceNumber, uint16_t addr, uint8_t len, uint32_t sn)
    {
        sequenceNumber++;

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
            (sn >> 24) & 0xff /*Serial number*/, 0x02 /*Frame type*/,
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
            0x15};

        int checksum = 0;
        for (int i = 1; i < sizeof(request) - 2; i++)
        {
            checksum += request[i] & 0xff;
        }
        request[sizeof(request) - 2] = checksum & 0xff;

        log_d("Sending solarmanv5 request: ");
        // for (int i = 0; i < sizeof(request); i++)
        // {
        //     log_d("%02X ", request[i]);
        // }

        size_t requestSize = sizeof(request);
        return client.write(request, requestSize) == requestSize;
    }

    bool awaitPacket(NetworkClient &client, int timeout)
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

    int readModbusRTUResponse(NetworkClient &client, byte *packetBuffer, size_t bufferLength)
    {
        if (!awaitPacket(client, READ_TIMEOUT))
        {
            log_d("Response timeout");
            return -1;
        }
        if (client.read(packetBuffer, 1) != 1)
        {
            log_d("Unable to read client.");
            return -1;
        }
        if (packetBuffer[0] != 0xA5)
        {
            log_d("Invalid header");
            return -1;
        }
        if (client.read(packetBuffer, 2) != 2)
        {
            log_d("Unable to read client.");
            return -1;
        }
        uint16_t length = packetBuffer[0] | (packetBuffer[1] << 8);
        if (length > bufferLength)
        {
            log_d("Buffer too small");
            return -1;
        }

        log_d("Payload length: %d", length);
        if (client.read(packetBuffer, 8) != 8)
        { // read rest of header
            log_d("Unable to read client.");
            return -1;
        }

        if (packetBuffer[0] != 0x10 || packetBuffer[1] != 0x15)
        {
            log_d("Invalid response");
            return -1;
        }

        int PAYLOAD_HEADER = 14;

        if (client.read(packetBuffer, PAYLOAD_HEADER) != PAYLOAD_HEADER)
        { // payload header
            log_d("Unable to read client.");
            return -1;
        }

        if (packetBuffer[0] != 0x02 || packetBuffer[1] != 0x01)
        {

            log_d("Invalid sensor in response");
            return -1;
        }

        int MODBUS_RTU_FRAME_LENGTH = length - PAYLOAD_HEADER;
        if (client.read(packetBuffer, MODBUS_RTU_FRAME_LENGTH) != MODBUS_RTU_FRAME_LENGTH)
        { // modbus rtu packet
            log_d("Unable to read client.");
            return -1;
        }

        // for (int i = 0; i < MODBUS_RTU_FRAME_LENGTH; i++)
        // {
        //     log_d("%02X ", packetBuffer[i]);
        // }

        byte trailerBuffer[2];
        if (client.read(trailerBuffer, 2) != 2)
        {
            // read trailer
            log_d("Unable to read client.");
        }

        return MODBUS_RTU_FRAME_LENGTH;
    }

    InverterData_t readData(String dongleSN)
    {
        // https://github.com/wills106/homeassistant-solax-modbus/blob/main/custom_components/solax_modbus/plugin_sofar.py
        InverterData_t inverterData;
        NetworkClient client;
        log_d("Connecting to dongle...");
        uint32_t sn = strtoul(dongleSN.c_str(), NULL, 10);
        log_d("SN: %d", sn);
        if (connect(client))
        {
            log_d("Connected.");
            byte packetBuffer[1024];

            inverterData.sn = sn;

            // pv input
            // 0x0580 - 0x05FF
            // but we need only few
            if (sendReadDataRequest(client, sequenceNumber, 0x586, 0x58F - 0x586 + 1, sn))
            {
                if (readModbusRTUResponse(client, packetBuffer, sizeof(packetBuffer)) > 0)
                {
                    inverterData.status = DONGLE_STATUS_OK;
                    inverterData.millis = millis();
                    inverterData.pv1Power = readUInt16(packetBuffer, 0x586 - 0x586) * 10;
                    inverterData.pv2Power = readUInt16(packetBuffer, 0x589 - 0x586) * 10;
                    inverterData.pv3Power = readUInt16(packetBuffer, 0x58B - 0x586) * 10;
                    inverterData.pv4Power = readUInt16(packetBuffer, 0x58F - 0x586) * 10;

                    // inverterData.feedInPower =
                    //     readInt16(packetBuffer, 25) + readInt16(packetBuffer, 30) + readInt16(packetBuffer, 35) - readInt16(packetBuffer, 64) - readInt16(packetBuffer, 50) - readInt16(packetBuffer, 66) - readInt16(packetBuffer, 56) - readInt16(packetBuffer, 68) - readInt16(packetBuffer, 62);
                    // inverterData.loadPower = readInt16(packetBuffer, 72) + readInt16(packetBuffer, 70);
                }
                else
                {
                    disconnect(client);
                    inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                    return inverterData;
                }
            }
            else
            {
                log_d("Failed to send request");
                disconnect(client);
                inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                return inverterData;
            }

            // battery input
            // 0x0600 - 0x067F
            if (sendReadDataRequest(client, sequenceNumber, 0x667, 2, sn))
            {
                if (readModbusRTUResponse(client, packetBuffer, sizeof(packetBuffer)) > 0)
                {
                    inverterData.batteryPower = readInt16(packetBuffer, 0) * 100;
                    inverterData.soc = readUInt16(packetBuffer, 1);
                }
                else
                {
                    disconnect(client);
                    inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                    return inverterData;
                }
            }
            else
            {
                log_d("Failed to send request");
                disconnect(client);
                inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                return inverterData;
            }

            if (sendReadDataRequest(client, sequenceNumber, 0x607, 0x607 - 0x607 + 1, sn))
            {
                if (readModbusRTUResponse(client, packetBuffer, sizeof(packetBuffer)) > 0)
                {
                    inverterData.batteryTemperature = readInt16(packetBuffer, 0x607 - 0x607);
                }
                else
                {
                    disconnect(client);
                    inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                    return inverterData;
                }
            }
            else
            {
                log_d("Failed to send request");
                disconnect(client);
                inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                return inverterData;
            }

            // module info
            // 0x404 - 0x44F
            if (sendReadDataRequest(client, sequenceNumber, 0x418, 0x418 - 0x418 + 1, sn))
            {
                if (readModbusRTUResponse(client, packetBuffer, sizeof(packetBuffer)) > 0)
                {
                    inverterData.inverterTemperature = readInt16(packetBuffer, 0x418 - 0x418);
                }
                else
                {
                    disconnect(client);
                    inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                    return inverterData;
                }
            }
            else
            {
                log_d("Failed to send request");
                disconnect(client);
                inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                return inverterData;
            }

            // on grid input
            // 0x484 - 0x4BC
            if (sendReadDataRequest(client, sequenceNumber, 0x484, 0x4BC - 0x484 + 1, sn))
            {
                if (readModbusRTUResponse(client, packetBuffer, sizeof(packetBuffer)) > 0)
                {
                    inverterData.inverterPower = readInt16(packetBuffer, 0x485 - 0x484) * 10;
                    inverterData.loadPower = readInt16(packetBuffer, 0x04AF - 0x484) * 10;
                    inverterData.feedInPower = readInt16(packetBuffer, 0x0488 - 0x484) * 10;
                    inverterData.L1Power = readInt16(packetBuffer, 0x48F - 0x484) * 10;
                    inverterData.L2Power = readInt16(packetBuffer, 0x49A - 0x484) * 10;
                    inverterData.L3Power = readInt16(packetBuffer, 0x4A5 - 0x484) * 10;
                }
                else
                {
                    disconnect(client);
                    return inverterData;
                }
            }
            else
            {
                log_d("Failed to send request");
                disconnect(client);
                inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                return inverterData;
            }

            // //stats
            // 0x0680 - 0x06BF
            if (sendReadDataRequest(client, sequenceNumber, 0x684, 0x698 - 0x684 + 2, sn))
            {
                if (readModbusRTUResponse(client, packetBuffer, sizeof(packetBuffer)) > 0)
                {
                    inverterData.pvToday = readUInt32(packetBuffer, 0x684 - 0x684) / 100.0f;
                    inverterData.pvTotal = readUInt32(packetBuffer, 0x686 - 0x684) / 10.0f;
                    inverterData.loadToday = readUInt32(packetBuffer, 0x688 - 0x684) / 100.0f;
                    inverterData.loadTotal = readUInt32(packetBuffer, 0x68A - 0x684) / 10.0f;
                    inverterData.batteryChargedToday = readUInt32(packetBuffer, 0x694 - 0x684) / 100.0f;
                    inverterData.batteryDischargedToday = readUInt32(packetBuffer, 0x698 - 0x684) / 100.0f;
                    inverterData.gridBuyToday = readUInt32(packetBuffer, 0x68C - 0x684) / 100.0f;
                    inverterData.gridBuyTotal = readUInt32(packetBuffer, 0x68E - 0x684) / 10.0f;
                    inverterData.gridSellToday = readUInt32(packetBuffer, 0x690 - 0x684) / 100.0f;
                    inverterData.gridSellTotal = readUInt32(packetBuffer, 0x692 - 0x684) / 10.0f;
                }
                else
                {
                    disconnect(client);
                    inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                    return inverterData;
                }
            } else {
                log_d("Failed to send request");
                disconnect(client);
                inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                return inverterData;
            }
        }

        inverterData.hasBattery = inverterData.soc != 0 || inverterData.batteryPower != 0;
        logInverterData(inverterData);

        disconnect(client);
        return inverterData;
    }
};