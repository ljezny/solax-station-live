#pragma once

#include <Arduino.h>
#include "../ModbusTCPDongleAPI.hpp"

class VictronDongleAPI: public ModbusTCPDongleAPI
{
public:
    VictronDongleAPI()
    {
    }

    InverterData_t loadData(String sn)
    {
        InverterData_t inverterData;

        if(!connect(IPAddress(172, 24, 24, 1), 502)) {
            log_d("Failed to connect to Victron dongle");
            inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
            return inverterData;
        }

        if(sendReadRequest(801, 6)) {
           if(readResponse()) {
                inverterData.status = DONGLE_STATUS_OK;
                char buffer[7] = {0};
                buffer[0] = RX_BUFFER[5 + 0];
                buffer[1] = RX_BUFFER[5 + 1];
                buffer[2] = RX_BUFFER[5 + 2];
                buffer[3] = RX_BUFFER[5 + 3];
                buffer[4] = RX_BUFFER[5 + 4];
                buffer[5] = RX_BUFFER[5 + 5];
                inverterData.sn = String(buffer);
                log_d("SN: %s", inverterData.sn.c_str());
           }
        }

        if(sendReadRequest(842, 2)) {
            if(readResponse()) {
                inverterData.status = DONGLE_STATUS_OK;
                inverterData.batteryPower = readInt16(842 - 842);
                inverterData.soc = (((uint32_t) readUInt16(843 - 842)) * 100) / 65535;
            }
        }

        logInverterData(inverterData);
        disconnect();

        return inverterData;
    }
};