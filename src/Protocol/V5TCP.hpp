#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <CRC.h>
#include <CRC16.h>
#include "utils/CustomNetworkClient.hpp"
#include "Inverters/InverterResult.hpp"

#define READ_TIMEOUT 2000

class V5TCP
{
private:
    CustomNetworkClient client;
    uint8_t sequenceNumber = 0;
    static constexpr int MAX_RETRIES = 3;

public:
    IPAddress ip;
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

    uint32_t readUInt32L(byte *buf, byte reg)
    {
        return (buf[3 + reg * 2 + 1] << 16 | buf[3 + reg * 2]);
    }

    int32_t readInt32(byte *buf, byte reg)
    {
        return readInt16(buf, reg) << 16 | readUInt16(buf, reg + 1);
    }

    float readIEEE754(byte *buf, byte reg)
    {
        uint32_t v = readUInt32(buf, reg);
        return *(float *)&v;
    }

    String readString(byte *buf, byte reg, uint8_t length)
    {
        String str = "";
        for (uint8_t i = 0; i < length; i++)
        {
            uint8_t index = 3 + reg * 2 + i; // 3 is the offset for the header
            if (index < RX_BUFFER_SIZE)
            {
                str += (char)buf[index];
            }
        }
        return str;
    }

    bool connect(IPAddress ip)
    {
        if (!client.connect(ip, 8899))
        {
            log_d("Failed to connect to V5TCP at %s", ip.toString().c_str());
            return false;
        }
        return true;
    }

    void disconnect()
    {
        client.stop();
    }

    bool sendReadDataRequest(uint16_t addr, uint8_t len, uint32_t sn)
    {
        if (sn == 0)
        {
            log_d("SN is zero, cannot send request");
            return false;
        }
        sequenceNumber++;

        uint8_t modbusRTURequest[] = {0x1, 0x03, 0, 0, 0, 0, 0, 0};
        modbusRTURequest[2] = addr >> 8;
        modbusRTURequest[3] = addr & 0xff;
        modbusRTURequest[5] = len;
        unsigned c = calcCRC16(modbusRTURequest, 6, 0x8005, 0xFFFF, 0, true, true);
        modbusRTURequest[6] = c;
        modbusRTURequest[7] = c >> 8;

        uint8_t request[] = {
            0xA5 /*Start of packet*/,
            0x17 /*Packet length*/,
            0x00 /*Packet length*/,
            0x10 /*Packet type, request*/,
            0x45 /*Packet type, request*/,
            (uint8_t) (sequenceNumber & 0xff) /*Request number*/,
            0 /*Request number*/,
            (uint8_t) (sn & 0xff) /*Serial number*/,
            (uint8_t) ((sn >> 8) & 0xff) /*Serial number*/,
            (uint8_t) ((sn >> 16) & 0xff) /*Serial number*/,
            (uint8_t) ((sn >> 24) & 0xff) /*Serial number*/, 0x02 /*Frame type*/,
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

        log_d("Sending solarmanv5 request. Sequence: %d, SN: %lu, Addr: %d, Len: %d",
              sequenceNumber, sn, addr, len);

        size_t requestSize = sizeof(request);

        String dump = "";
        for (size_t i = 0; i < requestSize; i++)
        {
            dump += String(request[i], HEX) + " ";
        }
        log_d("Request: %s", dump.c_str());

        bool result = client.write(request, requestSize) == requestSize;
        return result;
    }

    int readModbusRTUResponse(byte *packetBuffer, size_t bufferLength)
    {
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
        String dump = "";
        for (int i = 0; i < MODBUS_RTU_FRAME_LENGTH; i++)
        {
            dump += String(packetBuffer[i], HEX) + " ";
        }
        log_d("Request: %s", dump.c_str());
        return MODBUS_RTU_FRAME_LENGTH;
    }

    bool tryReadWithRetries(uint16_t startReg, uint16_t length, uint32_t sn, byte *buffer, std::function<void()> onSuccess)
    {
        for (int i = 0; i < MAX_RETRIES; ++i)
        {
            if (connect(ip))
            {
                if (sendReadDataRequest(startReg, length, sn))
                {
                    if (readModbusRTUResponse(buffer, 1024) > 0)
                    {
                        onSuccess();
                        disconnect();
                        return true;
                    }
                    else
                    {
                        log_d("Read failed for 0x%04X", startReg);
                    }
                }
                else
                {
                    log_d("Send request failed for 0x%04X", startReg);
                }
                disconnect();
            }
            else
            {
                log_d("Failed to connect to dongle at %s", ip.toString().c_str());
            }
        }
        return false;
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

    void ensureIPAddress(const String &ipAddress)
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
                    ip = IPAddress(10, 10, 100, 254); // fallback
            }
        }
    }
};