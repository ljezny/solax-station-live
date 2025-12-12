#pragma once

#include <Arduino.h>
#include "../utils/RemoteLogger.hpp"
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
            // Yield to RTOS to prevent watchdog timeout
            vTaskDelay(pdMS_TO_TICKS(5));
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
        ModbusResponse response{};
        udp.clear();
        if (!udp.beginPacket(ipAddress, port))
        {
            LOGD("Failed to begin packet");
            return response;
        }

        byte d[] = {0xF7, 0x03, 0, 0, 0, 0, 0, 0};
        d[2] = addr >> 8;
        d[3] = addr & 0xff;
        d[5] = len;
        unsigned c = calcCRC16(d, 6, 0x8005, 0xFFFF, 0, true, true);
        d[6] = c;
        d[7] = c >> 8;
        udp.write(d, sizeof(d));

        if (!udp.endPacket())
        {
            LOGD("Failed to send packet");
            return response;
        }

        response.sequenceNumber = 0;

        if (!awaitPacket(5000))
        {
            LOGD("Response timeout");
            return response;
        }

        int respLen = udp.read(response.data, RX_BUFFER_SIZE);
        if (respLen < 7)
        {
            LOGD("Invalid response length: %d", len);
            memset(response.data, 0, RX_BUFFER_SIZE);
            udp.clear();
            return response;
        }
        
        LOGD("Request address: %d", addr);
        String dataHex = "";
        for (int i = 0; i < respLen; i++) {
            dataHex += String(response.data[i], HEX);
            dataHex += " ";
        }
        LOGD("Response data: %s", dataHex.c_str());

        c = calcCRC16(response.data + 2, respLen - 2, 0x8005, 0xFFFF, 0, true, true);
        if (c != 0)
        {
            LOGD("CRC error: %04X", c);
            memset(response.data, 0, RX_BUFFER_SIZE);
            udp.clear();
            return response;
        }
        // AA55 is first two bytes
        response.unit = response.data[2];
        response.functionCode = response.data[3];
        
        response.address = addr;
        response.length = response.data[4];
        LOGD("Response: unit=%d, functionCode=%d, address=%d, length=%d", response.unit, response.functionCode, response.address, response.length);
        if(response.length != len * 2) {
            LOGD("Warning: Expected length %d, but got %d", len * 2, response.length);
            return response;
        }
        // shift data N bytes (header)
        int skip = 5;
        for (int i = 0; i < respLen - skip; i++)
        {
            response.data[i] = response.data[i + skip];
        }
        response.length -= skip;
        response.isValid = true;
        LOGD("Received response: %d bytes", response.length);
        return response;
    }

    /**
     * Modbus Function Code 0x06 - Write Single Register
     * Writes a single 16-bit value to a holding register via RTU over UDP
     * 
     * @param ipAddress Target IP address
     * @param port Target UDP port (usually 8899 for GoodWe)
     * @param unit Unit/Slave ID (0xF7 for GoodWe)
     * @param addr Register address
     * @param value 16-bit value to write
     * @return true if write was successful
     */
    bool writeSingleRegister(IPAddress ipAddress, int port, uint8_t unit, uint16_t addr, uint16_t value)
    {
        udp.clear();
        if (!udp.beginPacket(ipAddress, port))
        {
            LOGD("Failed to begin packet for write");
            return false;
        }

        // RTU frame: Unit ID, Function Code (0x06), Address (2 bytes), Value (2 bytes), CRC (2 bytes)
        byte d[] = {unit, 0x06, 0, 0, 0, 0, 0, 0};
        d[2] = addr >> 8;
        d[3] = addr & 0xff;
        d[4] = value >> 8;
        d[5] = value & 0xff;
        unsigned c = calcCRC16(d, 6, 0x8005, 0xFFFF, 0, true, true);
        d[6] = c;
        d[7] = c >> 8;
        udp.write(d, sizeof(d));

        if (!udp.endPacket())
        {
            LOGD("Failed to send write packet");
            return false;
        }

        if (!awaitPacket(5000))
        {
            LOGD("Write response timeout");
            return false;
        }

        uint8_t response[16];
        int respLen = udp.read(response, sizeof(response));
        if (respLen < 8)
        {
            LOGD("Invalid write response length: %d", respLen);
            return false;
        }

        // Log response
        String dataHex = "";
        for (int i = 0; i < respLen; i++) {
            dataHex += String(response[i], HEX);
            dataHex += " ";
        }
        LOGD("Write response: %s", dataHex.c_str());

        // Check for exception response (function code with high bit set)
        // Response format for RTU: [header bytes][unit][function][addr_hi][addr_lo][value_hi][value_lo][crc_lo][crc_hi]
        // GoodWe uses AA55 header, so skip first 2 bytes
        int offset = (response[0] == 0xAA && response[1] == 0x55) ? 2 : 0;
        
        if (response[offset + 1] == (0x06 | 0x80))
        {
            LOGD("Modbus write exception: code %d", response[offset + 2]);
            return false;
        }

        if (response[offset + 1] != 0x06)
        {
            LOGD("Invalid function code in write response: expected 0x06, got 0x%02X", response[offset + 1]);
            return false;
        }

        LOGD("Successfully wrote value %d to register 0x%04X", value, addr);
        return true;
    }

    /**
     * Modbus Function Code 0x10 - Write Multiple Registers
     * Writes multiple 16-bit values to consecutive holding registers via RTU over UDP
     * 
     * @param ipAddress Target IP address
     * @param port Target UDP port
     * @param unit Unit/Slave ID
     * @param startAddr Starting register address
     * @param values Pointer to array of bytes (raw data, already in network byte order)
     * @param byteCount Number of bytes to write
     * @return true if write was successful
     */
    bool writeMultipleRegisters(IPAddress ipAddress, int port, uint8_t unit, uint16_t startAddr, const uint8_t* values, uint8_t byteCount)
    {
        if (byteCount == 0 || byteCount > 246 || (byteCount % 2) != 0)
        {
            LOGD("Invalid byte count: %d", byteCount);
            return false;
        }

        uint16_t regCount = byteCount / 2;

        udp.clear();
        if (!udp.beginPacket(ipAddress, port))
        {
            LOGD("Failed to begin packet for write multiple");
            return false;
        }

        // RTU frame: Unit ID, Function Code (0x10), Start Addr (2), Reg Count (2), Byte Count (1), Data (N), CRC (2)
        int frameLen = 7 + byteCount + 2;  // header + data + CRC
        uint8_t* frame = new uint8_t[frameLen];
        frame[0] = unit;
        frame[1] = 0x10;  // Write Multiple Registers
        frame[2] = startAddr >> 8;
        frame[3] = startAddr & 0xff;
        frame[4] = regCount >> 8;
        frame[5] = regCount & 0xff;
        frame[6] = byteCount;
        memcpy(frame + 7, values, byteCount);
        
        unsigned c = calcCRC16(frame, 7 + byteCount, 0x8005, 0xFFFF, 0, true, true);
        frame[7 + byteCount] = c;
        frame[8 + byteCount] = c >> 8;

        udp.write(frame, frameLen);
        delete[] frame;

        if (!udp.endPacket())
        {
            LOGD("Failed to send write multiple packet");
            return false;
        }

        if (!awaitPacket(5000))
        {
            LOGD("Write multiple response timeout");
            return false;
        }

        uint8_t response[16];
        int respLen = udp.read(response, sizeof(response));
        
        // Log response first for debugging
        String dataHex = "";
        for (int i = 0; i < respLen; i++) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%02X ", response[i]);
            dataHex += buf;
        }
        LOGD("Write multiple response (%d bytes): %s", respLen, dataHex.c_str());
        
        // GoodWe may return shorter response (7 bytes without leading unit ID in some cases)
        // Minimum valid response: FC(1) + Addr(2) + RegCount(2) + CRC(2) = 7 bytes
        // Or with Unit ID: Unit(1) + FC(1) + Addr(2) + RegCount(2) + CRC(2) = 8 bytes
        if (respLen < 7)
        {
            LOGD("Invalid write multiple response length: %d (expected >= 7)", respLen);
            return false;
        }

        // Check response - GoodWe may use AA55 header
        int offset = (response[0] == 0xAA && response[1] == 0x55) ? 2 : 0;
        
        // If response doesn't start with unit ID, adjust offset
        // Check if first byte is function code 0x10 or exception 0x90
        if (offset == 0 && (response[0] == 0x10 || response[0] == 0x90))
        {
            // Response starts directly with function code (no unit ID)
            offset = -1;  // Will check response[offset+1] = response[0]
        }

        if (response[offset + 1] == (0x10 | 0x80))
        {
            LOGD("Modbus write multiple exception: code %d", response[offset + 2]);
            return false;
        }

        if (response[offset + 1] != 0x10)
        {
            LOGD("Invalid function code in write multiple response: expected 0x10, got 0x%02X", response[offset + 1]);
            return false;
        }

        LOGD("Successfully wrote %d registers starting at 0x%04X", regCount, startAddr);
        return true;
    }
};