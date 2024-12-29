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

        if(sendReadRequest(801, 6)) {
           if(readResponse()) {
                inverterData.status = DONGLE_STATUS_OK;
                inverterData.sn = macAddressToString();
                log_d("SN: %s", inverterData.sn.c_str());
           }
        }

        if(sendReadRequest(842, 2)) {
            if(readResponse()) {
                inverterData.batteryPower = readInt16(842 - 842);
                inverterData.soc = readUInt16(843 - 842);
            }
        }

        logInverterData(inverterData);
        disconnect();

        return inverterData;
    }

    private:
        String macAddressToString() {
            String mac = "";
            for (int i = 0; i < 6; i++) {
                mac += String(RX_BUFFER[i], HEX);                
            }
            return mac;
        }        
};