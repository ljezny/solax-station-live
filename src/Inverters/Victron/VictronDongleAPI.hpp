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

            if (lastBatteryPowerTime == 0)
            {
                lastBatteryPowerTime = inverterData.millis;
                lastBatteryPower = inverterData.batteryPower;
            }

            if (inverterData.batteryPower > 0)
            {
                batteryChargedToday += abs(inverterData.batteryPower) * (inverterData.millis - lastBatteryPowerTime) / 1000 / 3600;
            }
            else
            {
                batteryDischargedToday += abs(inverterData.batteryPower) * (inverterData.millis - lastBatteryPowerTime) / 1000 / 3600;
            }
        }

        response = sendModbusRequest(100, 808, 15);
        if (response.functionCode == 0x03)
        {
            inverterData.L1Power = max(0, ((int)readUInt16(response, 817)) - readInt16(response, 820));
            inverterData.L2Power = max(0,((int)readUInt16(response, 818)) - readInt16(response, 821));
            inverterData.L3Power = max(0,((int)readUInt16(response, 819)) - readInt16(response, 822));
            inverterData.loadPower = readUInt16(response, 817) + readUInt16(response, 818) + readUInt16(response, 819);
            inverterData.inverterPower = inverterData.L1Power + inverterData.L2Power + inverterData.L3Power;
            inverterData.feedInPower = -1 * (readInt16(response, 820) + readInt16(response, 821) + readInt16(response, 822));
        }

        for (int i = 0; i < sizeof(vebusUnits); i++)
        {
            if (vebusUnits[i] == 0)
            {
                continue;
            }
            response = sendModbusRequest(vebusUnits[i], 23, 3);
            if (response.functionCode == 0x03)
            {
                // inverterData.L1Power = readUInt16(response, 23) * 10;
                // inverterData.L2Power = readUInt16(response, 24) * 10;
                // inverterData.L3Power = readUInt16(response, 25) * 10;
                // inverterData.inverterPower = inverterData.L1Power + inverterData.L2Power + inverterData.L3Power - inverterData.feedInPower;

                response = sendModbusRequest(vebusUnits[i], 74, 20);
                if (response.functionCode == 0x03)
                {
                    inverterData.gridBuyTotal = readUInt32(response, 74) / 100.0 + readUInt32(response, 76) / 100.0; // total grid use
                    inverterData.gridSellTotal = readUInt32(response, 86) / 100.0;
                    inverterData.batteryChargedTotal = readUInt32(response, 76) / 100.0;
                    inverterData.batteryDischargedTotal = readUInt32(response, 90) / 100.0; // it seems that it is battery + solar
                    // inverterData.pvTotal = readUInt32(response, 90) / 100.0;
                    inverterData.loadTotal = readUInt32(response, 74) / 100.0 + readUInt32(response, 90) / 100.0;
                }
            }
            else
            {
                vebusUnits[i] = 0;
            }
        }

        int solarChargerIndex = 0;
        for (int i = 0; i < sizeof(solarChargerUnits); i++)
        {
            if (solarChargerUnits[i] == 0)
            {
                continue;
            }

            response = sendModbusRequest(solarChargerUnits[i], 3728, 3);
            if (response.functionCode == 0x03)
            {
                int pvPower = readUInt16(response, 3730);
                int pvTotal = readUInt32(response, 3728);
                switch (solarChargerIndex)
                {
                case 0:
                    inverterData.pv1Power = pvPower;
                    break;
                case 1:
                    inverterData.pv2Power = pvPower;
                    break;
                case 2:
                    inverterData.pv3Power = pvPower;
                    break;
                case 3:
                    inverterData.pv4Power = pvPower;
                    break;
                default:
                    inverterData.pv4Power += pvPower;
                    break;
                }
                inverterData.pvTotal += pvTotal;
                solarChargerIndex++;
            }
            else
            {
                solarChargerUnits[i] = 0;
            }
        }

        response = sendModbusRequest(225, 262, 16);
        if (response.functionCode == 0x03)
        {
            inverterData.batteryTemperature = readUInt16(response, 262) / 10;
        }

        response = sendModbusRequest(100, 830, 4);
        if (response.functionCode == 0x03)
        {
            time_t time = readUInt64(response, 830);

            log_d("Time: %s", ctime(&time));
            log_d("Day: %d", day);
            struct tm *tm = localtime(&time);
            int day = tm->tm_mday;
            if (this->day != day)
            {
                log_d("Day changed, resetting counters");
                this->day = day;
                pvTotal = inverterData.pvTotal;
                gridBuyTotal = inverterData.gridBuyTotal;
                gridSellTotal = inverterData.gridSellTotal;
                loadTotal = inverterData.loadTotal;
                batteryChargedToday = 0;
                batteryDischargedToday = 0;
            }

            // if system was restarted, reset counters
            if (inverterData.pvTotal < pvTotal)
            {
                pvTotal = inverterData.pvTotal;
            }
            if (inverterData.gridBuyTotal < gridBuyTotal)
            {
                gridBuyTotal = inverterData.gridBuyTotal;
            }
            if (inverterData.gridSellTotal < gridSellTotal)
            {
                gridSellTotal = inverterData.gridSellTotal;
            }
            if (inverterData.loadTotal < loadTotal)
            {
                loadTotal = inverterData.loadTotal;
            }
            inverterData.pvToday = inverterData.pvTotal - pvTotal;
            inverterData.gridBuyToday = inverterData.gridBuyTotal - gridBuyTotal;
            inverterData.gridSellToday = inverterData.gridSellTotal - gridSellTotal;
            inverterData.loadToday = inverterData.loadTotal - loadTotal;

            inverterData.batteryChargedToday = batteryChargedToday / 1000.0; // convert to kWh
            inverterData.batteryDischargedToday = batteryDischargedToday / 1000.0; // convert to kWh
        }

        logInverterData(inverterData);
        disconnect();

        return inverterData;
    }

private:
    double pvTotal = 0;
    double batteryDischargedToday = 0; //in Wh
    double batteryChargedToday = 0; //in Wh
    double gridBuyTotal = 0;
    double gridSellTotal = 0;
    double loadTotal = 0;
    double lastBatteryPower = 0;
    time_t lastBatteryPowerTime = 0;
    int day = -1;
    uint8_t solarChargerUnits[100] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
                                      11, 12, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
                                      31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43,
                                      44, 45, 46, 100, 101, 204, 205, 206, 207, 208, 209,
                                      210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220,
                                      221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231,
                                      232, 233, 234, 235, 236, 237, 238, 239, 242, 243, 245,
                                      246, 247};
    uint8_t vebusUnits[100] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
                               11, 12, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
                               31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43,
                               44, 45, 46, 100, 101, 204, 205, 206, 207, 208, 209,
                               210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220,
                               221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231,
                               232, 233, 234, 235, 236, 237, 238, 239, 242, 243, 245,
                               246, 247};
};