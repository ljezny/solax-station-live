#pragma once

#include <Arduino.h>
#include "InverterResult.hpp"
#include <CRC.h>
#include <CRC16.h>
#include <NetworkClient.h>

#define RX_BUFFER_SIZE 259

typedef struct
{
    uint16_t sequenceNumber;
    uint16_t unit;
    uint8_t functionCode;
    uint16_t address;
    uint16_t length;
    uint8_t data[RX_BUFFER_SIZE];
} ModbusTCPResponse_t;

class ModbusTCPDongleAPI
{
public:
    ModbusTCPDongleAPI()
    {
    }

protected:
    NetworkClient client;
    uint16_t sequenceNumber;

    bool connect(IPAddress ip, uint16_t port)
    {
        return client.connect(ip, port);
    }

    void disconnect()
    {
        client.stop();
    }

    ModbusTCPResponse_t sendModbusRequest(uint8_t unit, uint16_t addr, uint8_t count)
    {
        sequenceNumber++;

        byte request[] = {
            sequenceNumber >> 8,
            sequenceNumber & 0xff,
            0,
            0,
            0,
            6,    // length of following
            unit, // unit identifier
            0x03, // function code
            addr >> 8,
            addr & 0xff,
            0,
            count};

        ModbusTCPResponse_t response;
        response.sequenceNumber = sequenceNumber;

        int len = client.write(request, sizeof(request));
        if (len != sizeof(request))
        {
            log_d("Failed to send request");
            return response;
        }

        if (!awaitResponse(5000))
        {
            log_d("Response timeout");
            return response;
        }
        
        response.address = addr;

        memset(response.data, 0, RX_BUFFER_SIZE);

        len = client.read(response.data, 2);
        uint16_t sequenceNumberReceived = response.data[0] << 8 | response.data[1];
        if (sequenceNumberReceived != sequenceNumber)
        {
            log_d("Expected sequence number %d, but got %d", sequenceNumber, sequenceNumberReceived);
            client.read(response.data, RX_BUFFER_SIZE); // clear buffer
            memset(response.data, 0, RX_BUFFER_SIZE);
            return response;
        }
        response.sequenceNumber = sequenceNumberReceived;
        
        len = client.read(response.data, 2);
        if (len != 2)
        {
            log_d("Unable to read client.");
            return response;
        }
        
        len = client.read(response.data, 2); // read length
        if (len != 2)
        {
            log_d("Unable to read client.");
            return response;
        }

        len = client.read(response.data, 1); // read unit identifier
        if (len != 1)
        {
            log_d("Unable to read client.");
            return response;
        }

        len = client.read(response.data, 1); // read function code
        response.functionCode = response.data[0];
        if (response.data[0] != 0x03)
        {
            log_d("Invalid function code");
            log_d("Unit: %d, Function code: %d", unit, addr);
            log_d("Returned function code: %d", response.data[0]);
            client.read(response.data, RX_BUFFER_SIZE); // clear buffer
            memset(response.data, 0, RX_BUFFER_SIZE);
            return response;
        }

        len = client.read(response.data, 1); // read byte count
        if (len != 1)
        {
            log_d("Unable to read client.");
            return response;
        }
        response.length = response.data[0];

        memset(response.data, 0, RX_BUFFER_SIZE);
        len = client.read(response.data, response.length); // read data
        if (len != response.length)
        {
            log_d("Unable to read client.");
            return response;
        }

        //log response data as hex
        log_d("Request address: %d", addr);
        String data = "";
        for (int i = 0; i < response.length; i++)
        {
            data += String(response.data[i], HEX);
        }
        log_d("Response data: %s", data.c_str());

        return response;
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

    uint16_t readUInt16(ModbusTCPResponse_t &response, u16_t reg)
    {
        uint8_t index = reg - response.address;
        return (response.data[index * 2] << 8 | response.data[index * 2 + 1]);
    }

    int16_t readInt16(ModbusTCPResponse_t &response, u16_t reg)
    {
        return readUInt16(response, reg);
    }

    uint32_t readUInt32(ModbusTCPResponse_t &response, u16_t reg)
    {
        return ((uint32_t)readUInt16(response, reg)) << 16 | readUInt16(response, reg + 1);
    }

    uint64_t readUInt64(ModbusTCPResponse_t &response, u16_t reg)
    {
        return ((uint64_t)(response, reg)) << 32 | readUInt32(response, reg + 1);
    }

    int32_t readInt32(ModbusTCPResponse_t &response, u16_t reg)
    {
        return ((int32_t)readInt16(response, reg)) << 16 | readInt16(response, reg + 1);
    }

    float readIEEE754(ModbusTCPResponse_t &response, u16_t reg)
    {
        uint32_t v = readUInt32(response, reg);
        return *(float *)&v;
    }
};