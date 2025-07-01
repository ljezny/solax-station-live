#pragma once

#include <Arduino.h>

#define RX_BUFFER_SIZE 259
class ModbusResponse
{
public:
    uint16_t sequenceNumber;
    uint16_t unit;
    uint8_t functionCode;
    uint16_t address;
    uint16_t length;
    uint8_t data[RX_BUFFER_SIZE];
    bool isValid = false;

    uint16_t readUInt16(uint16_t reg)
    {
        uint8_t index = reg - address;
        return (data[index * 2] << 8 | data[index * 2 + 1]);
    }

    int16_t readInt16(uint16_t reg)
    {
        return readUInt16(reg);
    }

    uint32_t readUInt32(uint16_t reg)
    {
        return ((uint32_t)readUInt16(reg)) << 16 | readUInt16(reg + 1);
    }

    uint32_t readUInt32LSB(uint16_t reg)
    {
        return ((uint32_t)readUInt16(reg + 1)) << 16 | readUInt16(reg);
    }

    uint64_t readUInt64(uint16_t reg)
    {
        return ((uint64_t)readUInt32(reg)) << 32 | readUInt32(reg + 2);
    }

    int32_t readInt32(uint16_t reg)
    {
        return ((int32_t)readInt16(reg)) << 16 | readInt16(reg + 1);
    }

    int32_t readInt32LSB(uint16_t reg)
    {
        return ((int32_t)readInt16(reg + 1)) << 16 | readInt16(reg);
    }

    float readIEEE754(uint16_t reg)
    {
        uint32_t v = readUInt32(reg);
        return *(float *)&v;
    }

    String readString(uint16_t reg, uint8_t length)
    {
        String str = "";
        for (uint8_t i = 0; i < length; i++)
        {
            uint8_t index = 2 * (reg - address) + i;
            if (index < RX_BUFFER_SIZE)
            {
                str += (char)data[index];
            }
        }
        return str;
    }
};
