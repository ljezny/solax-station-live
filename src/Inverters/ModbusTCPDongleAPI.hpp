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
    uint16_t sequenceNumber;
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
        sequenceNumber++;

        byte request[] = {
                sequenceNumber >> 8, 
                sequenceNumber & 0xff, 
                0, 
                0, 
                0, 
                6, //length of following 
                0x00, //unit identifier
                0x03, //function code 
                addr >> 8, 
                addr & 0xff,
                0,
                len
        };
        
        return client.write(request, sizeof(request)) == sizeof(request);
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
        
        int len = client.read(RX_BUFFER, 2);
        if (RX_BUFFER[0] != sequenceNumber >> 8 || RX_BUFFER[1] != sequenceNumber & 0xff)
        {
            log_d("Expected sequence number %d, but got %d", sequenceNumber, RX_BUFFER[0] << 8 | RX_BUFFER[1]);
            return false;
        }

        len = client.read(RX_BUFFER, 2);
        if (len != 2)
        {
            log_d("Unable to read client.");
            return false;
        }

        len = client.read(RX_BUFFER, 2); // read length
        if (len != 2)
        {
            log_d("Unable to read client.");
            return false;
        }

        len = client.read(RX_BUFFER, 1); // read unit identifier
        if(RX_BUFFER[0] != 0)
        {
            log_d("Invalid unit identifier");
            return false;
        }

        len = client.read(RX_BUFFER, 1); // read function code
        if(RX_BUFFER[0] != 0x03)
        {
            log_d("Invalid function code");
            return false;
        }

        len = client.read(RX_BUFFER, 1); // read byte count
        if(len != 1)
        {
            log_d("Unable to read client.");
            return false;
        }

        uint8_t byteCount = RX_BUFFER[0];
        memset(RX_BUFFER, 0, RX_BUFFER_SIZE);
        len = client.read(RX_BUFFER, byteCount); // read data
        if(len != byteCount)
        {
            log_d("Unable to read client.");
            return false;
        }
        return true;
    }

    uint16_t readUInt16(byte reg)
    {
        return (RX_BUFFER[reg * 2] << 8 | RX_BUFFER[reg * 2 + 1]);
    }

    int16_t readInt16(byte reg)
    {
        return (RX_BUFFER[reg * 2] << 8 | RX_BUFFER[reg * 2 + 1]);
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