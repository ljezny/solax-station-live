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
                inverterData.sn = responseToHexString();
                log_d("SN: %s", inverterData.sn.c_str());
           }
        }

        if(sendReadRequest(842, 2)) {
            if(readResponse()) {
                inverterData.status = DONGLE_STATUS_OK;
                inverterData.batteryPower = readInt16(842 - 842);
                inverterData.soc = readUInt16(843 - 842);
            }
        }

        logInverterData(inverterData);
        disconnect();

        return inverterData;
    }

    private:
        String responseToHexString() {
            String hexString = "";
            for (int i = 0; i < RX_BUFFER_SIZE; i++) {
                hexString += String(RX_BUFFER[i], HEX);
            }
            return hexString;
        }
};