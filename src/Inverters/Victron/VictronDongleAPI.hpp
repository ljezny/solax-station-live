#pragma once

#include <Arduino.h>
#include "../../Protocol/ModbusTCP.hpp"

class VictronDongleAPI
{
private:
    double pvTotal = 0;
    double batteryDischargedToday = 0; // in Wh
    double batteryChargedToday = 0;    // in Wh
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
    bool solarChargerUnitsInitialized = false;
    uint8_t vebusUnits[100] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
                               11, 12, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
                               31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43,
                               44, 45, 46, 100, 101, 204, 205, 206, 207, 208, 209,
                               210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220,
                               221, 222, 223, 224, 225, 226, 227, 228, 229, 230, 231,
                               232, 233, 234, 235, 236, 237, 238, 239, 242, 243, 245,
                               246, 247};
    bool vebusUnitsInitialized = false;

    ModbusTCP channel;

public:
    VictronDongleAPI()
    {
    }

    InverterData_t loadData(String ipAddress)
    {
        InverterData_t inverterData;
        IPAddress ip = IPAddress(ipAddress.c_str());
        if (ip == IPAddress(0, 0, 0, 0))
        {
            ip = IPAddress(172, 24, 24, 1); // Default Victron dongle IP
        }
        if (!channel.connect(ip, 502))
        {
            log_d("Failed to connect to Victron dongle");
            inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
            channel.disconnect();
            return inverterData;
        }

        inverterData.millis = millis();
        ModbusResponse response;

        response = channel.sendModbusRequest(100, 0x03, 800, 12);
        if (response.isValid)
        {
            inverterData.status = DONGLE_STATUS_OK;
            inverterData.sn = String((char *)response.data);
            log_d("SN: %s", inverterData.sn.c_str());
        }

        response = channel.sendModbusRequest(100, 0x03, 842, 2);
        if (response.isValid)
        {
            inverterData.batteryPower = response.readInt16(842);
            inverterData.soc = response.readUInt16(843);

            if (lastBatteryPowerTime != 0)
            {
                if (inverterData.batteryPower > 0)
                {
                    batteryChargedToday += abs(inverterData.batteryPower) * (inverterData.millis - lastBatteryPowerTime) / 1000.0 / 3600.0;
                }
                else
                {
                    batteryDischargedToday += abs(inverterData.batteryPower) * (inverterData.millis - lastBatteryPowerTime) / 1000.0 / 3600.0;
                }
            }
            lastBatteryPowerTime = inverterData.millis;
            lastBatteryPower = inverterData.batteryPower;
        }

        response = channel.sendModbusRequest(100, 0x03, 808, 15);
        if (response.isValid)
        {
            inverterData.L1Power = max(0, ((int)response.readUInt16(817)) - response.readInt16(820));
            inverterData.L2Power = max(0, ((int)response.readUInt16(818)) - response.readInt16(821));
            inverterData.L3Power = max(0, ((int)response.readUInt16(819)) - response.readInt16(822));
            inverterData.loadPower = response.readUInt16(817) + response.readUInt16(818) + response.readUInt16(819);
            inverterData.inverterPower = inverterData.L1Power + inverterData.L2Power + inverterData.L3Power;
            inverterData.feedInPower = -1 * (response.readInt16(820) + response.readInt16(821) + response.readInt16(822));
        }

        for (int i = 0; i < sizeof(vebusUnits); i++)
        {
            if (vebusUnits[i] == 0)
            {
                continue;
            }
            response = channel.sendModbusRequest(vebusUnits[i], 0x03, 23, 3);
            if (response.isValid)
            {
                // inverterData.L1Power = readUInt16(23) * 10;
                // inverterData.L2Power = readUInt16(24) * 10;
                // inverterData.L3Power = readUInt16(25) * 10;
                // inverterData.inverterPower = inverterData.L1Power + inverterData.L2Power + inverterData.L3Power - inverterData.feedInPower;

                response = channel.sendModbusRequest(vebusUnits[i], 0x03, 74, 20);
                if (response.isValid)
                {
                    /*
                    Energy from AC-In 1 to AC-out	74
                    Energy from AC-In 1 to battery	76
                    Energy from AC-In 2 to AC-out	78
                    Energy from AC-In 2 to battery	80
                    Energy from AC-out to AC-in 1 (reverse fed PV)	82
                    Energy from AC-out to AC-in 2 (reverse fed PV)	84
                    Energy from battery to AC-in 1	86
                    Energy from battery to AC-in 2	88
                    Energy from battery to AC-out	90
                    Energy from AC-out to battery (typically from PV-inverter)	92
                    */
                    double energyACIn1ToACOut = response.readUInt32(74) / 100.0;
                    double energyACIn1ToBattery = response.readUInt32(76) / 100.0;
                    double energyACIn2ToACOut = response.readUInt32(78) / 100.0;
                    double energyACIn2ToBattery = response.readUInt32(80) / 100.0;
                    double energyACOutToACIn1 = response.readUInt32(82) / 100.0;
                    double energyACOutToACIn2 = response.readUInt32(84) / 100.0;
                    double energyBatteryToACIn1 = response.readUInt32(86) / 100.0;
                    double energyBatteryToACIn2 = response.readUInt32(88) / 100.0;
                    double energyBatteryToACOut = response.readUInt32(90) / 100.0;
                    double energyACOutToBattery = response.readUInt32(92) / 100.0;

                    inverterData.gridBuyTotal = energyACIn1ToACOut + energyACIn1ToBattery; // total grid use
                    inverterData.gridSellTotal = energyBatteryToACIn1;
                    inverterData.batteryChargedTotal = energyACIn1ToBattery;
                    inverterData.batteryDischargedTotal = energyBatteryToACOut; // it seems that it is battery + solar
                    // inverterData.pvTotal = readUInt32(90) / 100.0;
                    inverterData.loadTotal = energyACIn1ToACOut + energyBatteryToACOut;
                }
            }
            else
            {
                if (!vebusUnitsInitialized)
                {
                    vebusUnits[i] = 0;
                }
            }
        }
        vebusUnitsInitialized = true;

        int solarChargerIndex = 0;
        for (int i = 0; i < sizeof(solarChargerUnits); i++)
        {
            if (solarChargerUnits[i] == 0)
            {
                continue;
            }

            response = channel.sendModbusRequest(solarChargerUnits[i], 0x03, 3728, 3);
            if (response.isValid)
            {
                int pvPower = response.readUInt16(3730);
                int pvTotal = response.readUInt32(3728);
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
                if (!solarChargerUnitsInitialized)
                {
                    solarChargerUnits[i] = 0;
                }
            }
        }
        solarChargerUnitsInitialized = true;

        response = channel.sendModbusRequest(225, 0x03, 262, 16);
        if (response.isValid)
        {
            inverterData.batteryTemperature = response.readUInt16(262) / 10;
        }

        response = channel.sendModbusRequest(100, 0x03, 830, 4);
        if (response.isValid)
        {
            time_t time = response.readUInt64(830);

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

            inverterData.batteryChargedToday = batteryChargedToday / 1000.0;       // convert to kWh
            inverterData.batteryDischargedToday = batteryDischargedToday / 1000.0; // convert to kWh
        }
        inverterData.hasBattery = inverterData.soc != 0 || inverterData.batteryPower != 0;
        logInverterData(inverterData);
        channel.disconnect();

        return inverterData;
    }
};