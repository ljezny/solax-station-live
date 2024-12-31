#pragma once

#include <Arduino.h>
#include "../ModbusTCPDongleAPI.hpp"

class VictronDongleAPI : public ModbusTCPDongleAPI
{
public:
    VictronDongleAPI()
    {
    }

    InverterData_t loadData(String sn)
    {
        InverterData_t inverterData;

        if (!connect(IPAddress(172, 24, 24, 1), 502))
        {
            log_d("Failed to connect to Victron dongle");
            inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
            return inverterData;
        }

        inverterData.millis = millis();
        ModbusTCPResponse_t response;

        response = sendModbusRequest(100, 800, 12);
        if (response.functionCode == 0x03)
        {
            inverterData.status = DONGLE_STATUS_OK;
            inverterData.sn = String((char *)response.data);
            log_d("SN: %s", inverterData.sn.c_str());
        }

        response = sendModbusRequest(100, 842, 2);
        if (response.functionCode == 0x03)
        {
            inverterData.batteryPower = readInt16(response, 842);
            inverterData.soc = readUInt16(response, 843);
        }

        response = sendModbusRequest(100, 808, 15);
        if (response.functionCode == 0x03)
        {
            inverterData.L1Power = readInt32(response, 808);
            inverterData.L2Power = readInt32(response, 809);
            inverterData.L3Power = readInt32(response, 810);
            inverterData.inverterPower = inverterData.L1Power + inverterData.L2Power + inverterData.L3Power;
            inverterData.loadPower = readUInt16(response, 817) + readUInt16(response, 818) + readUInt16(response, 819);
            inverterData.feedInPower = readInt16(response, 820) + readInt16(response, 821) + readInt16(response, 822);
        }

        response = sendModbusRequest(100, 776, 2);
        if (response.functionCode == 0x03)
        {
            int pvPower = readUInt16(response, 776);
            pvPower = pvPower * readInt16(response, 777);
            pvPower = pvPower / 10 / 100;
            inverterData.pv1Power = pvPower;
        }

        response = sendModbusRequest(100, 830, 4);
        if (response.functionCode == 0x03)
        {
            time_t time = readUInt64(response, 830);
            log_d("Time: %s", ctime(&time));
        }

        response = sendModbusRequest(100, 790, 1);
        if (response.functionCode == 0x03)
        {
            inverterData.pvTotal = readUInt16(response, 790) / 10;
        }

        response = sendModbusRequest(225, 262, 16);
        if (response.functionCode == 0x03)
        {
            inverterData.batteryTemperature = readUInt16(response, 262) / 10;
        }

        logInverterData(inverterData);
        disconnect();

        return inverterData;
    }

private:
    double pvTotal = 0;
    int day = -1;
};