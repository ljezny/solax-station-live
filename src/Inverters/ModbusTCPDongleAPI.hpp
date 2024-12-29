#pragma once

#include <Arduino.h>
#include "InverterResult.hpp"
#include <CRC.h>
#include <CRC16.h>
#include <NetworkClient.h>

#define RX_BUFFER_SIZE 259
class ModbusTCPDongleAPI
{
public:
    ModbusTCPDongleAPI()
    {
    }

protected:
    NetworkClient client;    
    byte RX_BUFFER[RX_BUFFER_SIZE];

    bool connect(IPAddress ip, uint16_t port)
    {
        return client.connect(ip, port);
    }

    void disconnect()
    {
        client.stop();
    }

    bool sendReadRequest(uint16_t addr, uint8_t len)
    {
        byte d[] = {0xF7, 0x03, 0, 0, 0, 0, 0, 0};
        d[2] = addr >> 8;
        d[3] = addr & 0xff;
        d[5] = len;
        unsigned c = crc16(d, 6, 0x8005, 0xFFFF, 0, true, true);
        d[6] = c;
        d[7] = c >> 8;
        client.write(d, sizeof(d));
    }

    bool awaitResponse(int timeout)
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

    bool readResponse() {
        if (!awaitResponse(5000))
        {
            log_d("Response timeout");
            return false;
        }
        memset(RX_BUFFER, 0, RX_BUFFER_SIZE);

        int len = client.read(RX_BUFFER, RX_BUFFER_SIZE);
        if (RX_BUFFER[0] != 0xA5)
        {
            log_d("Invalid header");
            return false;
        }
        unsigned c = crc16(RX_BUFFER + 2, len - 2, 0x8005, 0xFFFF, 0, true, true);
        if (c != 0)
        {
            log_d("CRC error");
            return false;
        }    
        return true;
    }

    uint16_t readUInt16(byte reg)
    {
        return (RX_BUFFER[5 + reg * 2] << 8 | RX_BUFFER[6 + reg * 2]);
    }

    int16_t readInt16(byte reg)
    {
        return (RX_BUFFER[5 + reg * 2] << 8 | RX_BUFFER[6 + reg * 2]);
    }

    uint32_t readUInt32(byte reg)
    {
        return readUInt16(reg) << 16 | readUInt16(reg + 1);
    }

    float readIEEE754(byte reg)
    {
        uint32_t v = readUInt32(reg);
        return *(float *)&v;
    }
};