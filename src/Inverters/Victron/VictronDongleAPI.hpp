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
        
        inverterData.millis = millis();

        if(sendReadRequest(800, 12)) {
           if(readResponse()) {
                inverterData.status = DONGLE_STATUS_OK;
                inverterData.sn = String((char *) RX_BUFFER);
                log_d("SN: %s", inverterData.sn.c_str());
           }
        }

        if(sendReadRequest(842, 2)) {
            if(readResponse()) {
                inverterData.batteryPower = readInt16(842 - 842);
                inverterData.soc = readUInt16(843 - 842);
            }
        }

        if(sendReadRequest(817, 3)) {
            if(readResponse()) {
                inverterData.loadPower = readUInt16(817 - 817) + readUInt16(818 - 817) + readUInt16(819 - 817);
                inverterData.feedInPower = readInt16(820 - 817) + readInt16(821 - 817) + readInt16(822 - 817);
            }
        }

        if(sendReadRequest(868, 16)) {
            if(readResponse()) {
                inverterData.inverterPower = readInt32(870 - 868);
                inverterData.L1Power = readInt32(878 - 868);
                inverterData.L2Power = readInt32(880 - 868);
                inverterData.L3Power = readInt32(882 - 868);
            }
        }

        logInverterData(inverterData);
        disconnect();

        return inverterData;
    }

    private:        
};