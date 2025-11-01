#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <CRC.h>
#include <CRC16.h>

#include "ModbusResponse.hpp"

class ModbusRTU
{
private:
    WiFiUDP udp;
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

public:
    ModbusRTU()
    {
    }

    bool connect()
    {
        return udp.begin(WiFi.localIP(), 8899);
    }

    void disconnect()
    {
        udp.stop();
    }

    ModbusResponse sendDataRequest(IPAddress ipAddress, int port, uint16_t addr, uint8_t len)
    {
        ModbusResponse response;
        udp.clear();
        if (!udp.beginPacket(ipAddress, port))
        {
            log_d("Failed to begin packet");
            return response;
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
            return response;
        }

        response.sequenceNumber = 0;

        if (!awaitPacket(5000))
        {
            log_d("Response timeout");
            return response;
        }

        int respLen = udp.read(response.data, RX_BUFFER_SIZE);
        if (respLen < 7)
        {
            log_d("Invalid response length: %d", len);
            memset(response.data, 0, RX_BUFFER_SIZE);
            udp.clear();
            return response;
        }

        c = crc16(response.data + 2, respLen - 2, 0x8005, 0xFFFF, 0, true, true);
        if (c != 0)
        {
            log_d("CRC error: %04X", c);
            memset(response.data, 0, RX_BUFFER_SIZE);
            udp.clear();
            return response;
        }

        response.unit = response.data[0];
        response.functionCode = response.data[1];
        response.address = addr;
        response.length = response.data[2];
        log_d("Response: unit=%d, functionCode=%d, address=%d, length=%d", response.unit, response.functionCode, response.address, response.length);
        //shift data N bytes (header)
        int skip = 5;
        for (int i = 0; i < respLen - skip; i++)
        {
            response.data[i] = response.data[i + skip];
        }
        response.length -= skip;
        response.isValid = true;
        log_d("Received response: %d bytes", response.length);
        return response;
    }
};