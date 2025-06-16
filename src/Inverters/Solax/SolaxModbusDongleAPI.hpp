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

        if (!isSupportedDongle)
        {
            inverterData.status = DONGLE_STATUS_UNSUPPORTED_DONGLE;
            log_d("Unsupported dongle");
            return inverterData;
        }

        if (!client.connected())
        {
            if (!connect(getIp(), 502))
            {
                log_d("Failed to connect to Solax Modbus dongle");
                inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                disconnect();
                return inverterData;
            }
        }

        inverterData.millis = millis();
        ModbusTCPResponse_t response;

        response = sendModbusRequest(1, 0x03, 0x0, 0x014 - 0x00 + 1);
        if (response.isValid)
        {
            inverterData.status = DONGLE_STATUS_OK;
            inverterData.sn = readString(response, 0x00, 14);
            log_d("SN: %s", inverterData.sn.c_str());
            String factoryName = readString(response, 0x07, 14);
            String moduleName = readString(response, 0x0E, 14);
            log_d("Factory Name: %s, Module Name: %s", factoryName.c_str(), moduleName.c_str());
        }
        else
        {
            inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
            log_d("Failed to read SN or module name");
            disconnect();
            return inverterData;
        }

        response = sendModbusRequest(1, 0x04, 0x0, 0x54 - 0x00 + 1);
        if (response.isValid)
        {
            inverterData.inverterPower = readInt16(response, 0x02);
            inverterData.pv1Power = readUInt16(response, 0x0A);
            inverterData.pv2Power = readUInt16(response, 0x0B);
            inverterData.inverterTemperature = readInt16(response, 0x08);
            if(isGen5(inverterData.sn))
            {
                inverterData.inverterTemperature /= 10;
            }
            inverterData.soc = readUInt16(response, 0x1C);
            inverterData.batteryPower = readInt16(response, 0x16);
            inverterData.batteryVoltage = readInt16(response, 0x14) / 10.0f;
            inverterData.feedInPower = readInt32LSB(response, 0x46);
            inverterData.batteryChargedToday = readUInt16(response, 0x23) / 10.0f;
            inverterData.batteryDischargedToday = readUInt16(response, 0x20) / 10.0f;
            inverterData.batteryTemperature = readInt16(response, 0x18);
            inverterData.gridBuyTotal = readUInt32LSB(response, 0x4A) / 100.0f;
            inverterData.gridSellTotal = readUInt32LSB(response, 0x48) / 100.0f;
            inverterData.loadPower = inverterData.inverterPower - inverterData.feedInPower;
            inverterData.pvTotal = readUInt32LSB(response, 0x52) / 10.0f;
            inverterData.loadToday = readUInt16(response, 0x50) / 10.0f;
        }
        else
        {
            inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
            log_d("Failed to read inverter data");
            disconnect();
            return inverterData;
        }

        response = sendModbusRequest(1, 0x04, 0x91, 0x9A - 0x91 + 2);
        if (response.isValid)
        {            
            inverterData.gridBuyToday = readUInt32LSB(response, 0x98) / 100.0f;
            inverterData.gridSellToday = readUInt32LSB(response, 0x9A) / 100.0f;
            inverterData.loadToday -= inverterData.gridSellToday; // Adjust load today by grid sell
            inverterData.loadToday += inverterData.gridBuyToday; // Adjust load today by grid buy
            inverterData.pvToday = readUInt16(response, 0x96) / 10.0f;
        }
        else
        {
            inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
            log_d("Failed to read PV data");
            disconnect();
            return inverterData;
        }

        response = sendModbusRequest(1, 0x04, 0x6A, 0x84 - 0x6A + 2);
        if (response.isValid)
        {
            inverterData.L1Power = readUInt16(response, 0x6C);
            inverterData.L2Power = readUInt16(response, 0x70);
            inverterData.L3Power = readUInt16(response, 0x74);

            uint16_t offgridL1Power = readUInt16(response, 0x78);
            uint16_t offgridL2Power = readUInt16(response, 0x7C);
            uint16_t offgridL3Power = readUInt16(response, 0x80);

            inverterData.inverterPower += offgridL1Power + offgridL2Power + offgridL3Power;
            inverterData.L1Power += offgridL1Power;
            inverterData.L2Power += offgridL2Power;
            inverterData.L3Power += offgridL3Power;

            inverterData.inverterPower = inverterData.L1Power + inverterData.L2Power + inverterData.L3Power;
            inverterData.loadPower = inverterData.inverterPower - inverterData.feedInPower;
        }
        else
        {
            inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
            log_d("Failed to read inverter data");
            disconnect();
            return inverterData;
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

    bool isGen5(String sn)
    {
        return sn.startsWith("H35") || sn.startsWith("H3B");
    }
};