#pragma once

#include "../ModbusTCPDongleAPI.hpp"
#include <WiFi.h>

class SolaxModbusDongleAPI : public ModbusTCPDongleAPI
{
public:
    SolaxModbusDongleAPI() : ModbusTCPDongleAPI()
    {
    }

    InverterData_t loadData(String sn)
    {
        InverterData_t inverterData;
        if (!connect(getIp(), 502))
        {
            log_d("Failed to connect to Solax Modbus dongle");
            inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
            return inverterData;
        }

        if (!isSupportedDongle)
        {
            inverterData.status = DONGLE_STATUS_UNSUPPORTED_DONGLE;
            log_d("Unsupported dongle");
            return inverterData;
        }

        inverterData.millis = millis();
        ModbusTCPResponse_t response;

        response = sendModbusRequest(1, 0x03, 0x0, 0x014 - 0x00 + 1);
        if (response.functionCode == 0x03)
        {
            inverterData.status = DONGLE_STATUS_OK;
            inverterData.sn = readString(response, 0x00, 14);
            log_d("SN: %s", inverterData.sn.c_str());
            String factoryName = readString(response, 0x07, 14);
            String moduleName = readString(response, 0x0E, 14);
            log_d("Factory Name: %s, Module Name: %s", factoryName.c_str(), moduleName.c_str());
        }

        response = sendModbusRequest(1, 0x04, 0x0, 0x004B - 0x00 + 1);
        if (response.functionCode == 0x04)
        {
            inverterData.pv1Power = readUInt16(response, 0x0A);
            inverterData.pv2Power = readUInt16(response, 0x0B);
            inverterData.inverterTemperature = readInt16(response, 0x08);
            inverterData.soc = readUInt16(response, 0x1C);
            inverterData.batteryPower = readInt16(response, 0x16);
            inverterData.batteryVoltage = readInt16(response, 0x14) / 10.0f;
            inverterData.feedInPower = readInt32LSB(response, 0x46);
            inverterData.batteryChargedToday = readInt32LSB(response, 0x23) / 10.0f;
            inverterData.batteryDischargedToday = readInt32LSB(response, 0x20) / 10.0f;
            inverterData.batteryTemperature = readInt16(response, 0x18);
            inverterData.gridBuyTotal = readUInt32LSB(response, 0x4A) / 100.0f;
            inverterData.gridSellTotal = readUInt32LSB(response, 0x48) / 100.0f;
        }

        response = sendModbusRequest(1, 0x04, 0x91, 0x0120 - 0x91 + 1);
        if (response.functionCode == 0x04)
        {
            inverterData.pvTotal = readUInt32LSB(response, 0x94 - 0x91) / 10.0f;
            inverterData.pvToday = readUInt32LSB(response, 0x96 - 0x91) / 10.0f;
            inverterData.gridBuyToday = readUInt32LSB(response, 0x9A - 0x91) / 100.0f;
            inverterData.gridSellToday = readUInt32LSB(response, 0x98 - 0x91) / 100.0f;
        }
        logInverterData(inverterData);
        return inverterData; // Placeholder for actual implementation
    }

protected:
    bool isSupportedDongle = true;

    IPAddress getIp()
    {
        if (WiFi.localIP()[0] == 192)
        {
            return IPAddress(192, 168, 10, 10);
        }
        else
        {
            return IPAddress(5, 8, 8, 8);
        }
    }
};