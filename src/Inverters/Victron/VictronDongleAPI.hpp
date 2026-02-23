#pragma once

#include <Arduino.h>
#include <RemoteLogger.hpp>
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
    double pvTodayIntegrated = 0;  // Wh, for fallback local integration
    time_t lastPvPowerTime = 0;
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
    
    // Multi RS units for PV reading (registers 4598-4601)
    uint8_t multiRsUnits[50] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 20, 21, 22, 23, 24, 25,
                                226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237};
    bool multiRsUnitsInitialized = false;
    
    // RS Smart Inverter units for PV reading (registers 3164-3167)
    uint8_t rsInverterUnits[50] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 20, 21, 22, 23, 24, 25,
                                   226, 227, 228, 229, 230, 231, 232, 233, 234, 235, 236, 237};
    bool rsInverterUnitsInitialized = false;

    ModbusTCP channel;

public:
    VictronDongleAPI()
    {
    }

    /**
     * Returns true if this inverter supports intelligence mode control
     */
    bool supportsIntelligence() { return false; }

    InverterData_t loadData(String ipAddress)
    {
        InverterData_t inverterData{};
        IPAddress ip;
        if (!ipAddress.isEmpty())
        {
            ip.fromString(ipAddress);
            if (!channel.connect(ip, 502))
            {
                inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                inverterData.errorDescription = String("Victron: Failed to connect to ") + ipAddress + ":502 (Modbus TCP)";
                channel.disconnect();
                return inverterData;
            }
        }
        else
        {
            if (!channel.connect(String("venus.local"), 502))
            {
                inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                inverterData.errorDescription = "Victron: Failed to connect to venus.local:502 (mDNS). Set IP manually if mDNS not working.";
                channel.disconnect();
                return inverterData;
            }
        }

        inverterData.millis = millis();
        ModbusResponse response;

        response = channel.sendModbusRequest(100, 0x03, 800, 12);
        if (response.isValid)
        {
            inverterData.status = DONGLE_STATUS_OK;
            inverterData.sn = String((char *)response.data);
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
        int acPvOnOutputL1 = 0, acPvOnOutputL2 = 0, acPvOnOutputL3 = 0;
        if (response.isValid)
        {
            // Log all system AC registers for diagnostics
            // 808-810: AC Input L1-L3 (from grid/genset)
            // 811-813: AC PV on Output L1-L3 (AC-coupled PV inverter on AC-out)
            // 814-816: AC Consumption L1-L3
            // 817-819: AC Output L1-L3 (load)
            // 820-822: AC Grid L1-L3
            int acInputL1 = response.readUInt16(808);
            int acInputL2 = response.readUInt16(809);
            int acInputL3 = response.readUInt16(810);
            acPvOnOutputL1 = response.readUInt16(811);
            acPvOnOutputL2 = response.readUInt16(812);
            acPvOnOutputL3 = response.readUInt16(813);
            int acConsumptionL1 = response.readUInt16(814);
            int acConsumptionL2 = response.readUInt16(815);
            int acConsumptionL3 = response.readUInt16(816);
            LOGD("Victron: System AC regs - AcIn:%d/%d/%d PvOnOut:%d/%d/%d AcCons:%d/%d/%d Load:%d/%d/%d Grid:%d/%d/%d",
                 acInputL1, acInputL2, acInputL3,
                 acPvOnOutputL1, acPvOnOutputL2, acPvOnOutputL3,
                 acConsumptionL1, acConsumptionL2, acConsumptionL3,
                 response.readUInt16(817), response.readUInt16(818), response.readUInt16(819),
                 response.readInt16(820), response.readInt16(821), response.readInt16(822));

            inverterData.inverterOutpuPowerL1 = max(0, ((int)response.readUInt16(817)) - response.readInt16(820));
            inverterData.inverterOutpuPowerL2 = max(0, ((int)response.readUInt16(818)) - response.readInt16(821));
            inverterData.inverterOutpuPowerL3 = max(0, ((int)response.readUInt16(819)) - response.readInt16(822));
            inverterData.loadPower = response.readUInt16(817) + response.readUInt16(818) + response.readUInt16(819);
            inverterData.gridPowerL1 = -1 * response.readInt16(820);
            inverterData.gridPowerL2 = -1 * response.readInt16(821);
            inverterData.gridPowerL3 = -1 * response.readInt16(822);
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
                    inverterData.loadTotal = energyACIn1ToACOut + energyBatteryToACOut;
                    // AC PV total = reverse-fed PV (AC-out to AC-in) + PV to battery (AC-out to battery)
                    // This covers AC-coupled PV energy: part goes to grid (reverse feed), part charges battery
                    double acPvTotalEnergy = energyACOutToACIn1 + energyACOutToACIn2 + energyACOutToBattery;
                    LOGD("Victron: VE.Bus energy - ACIn1ToOut=%.2f ACIn1ToBat=%.2f ACOutToACIn1=%.2f ACOutToACIn2=%.2f BatToOut=%.2f OutToBat=%.2f acPvTotal=%.2f kWh",
                         energyACIn1ToACOut, energyACIn1ToBattery, energyACOutToACIn1, energyACOutToACIn2,
                         energyBatteryToACOut, energyACOutToBattery, acPvTotalEnergy);
                    // Use AC PV total as fallback pvTotal (only if no DC PV source set it later)
                    if (inverterData.pvTotal == 0 && acPvTotalEnergy > 0)
                    {
                        inverterData.pvTotal = acPvTotalEnergy;
                        LOGD("Victron: Using VE.Bus AC PV energy as pvTotal fallback: %.2f kWh", acPvTotalEnergy);
                    }
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
        LOGD("Victron: VE.Bus scan complete, loadTotal=%.2f kWh", inverterData.loadTotal);

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
                int pvYieldTotal = response.readUInt32(3728);
                LOGD("Victron: Solar Charger unit %d: PV=%dW, Total=%d kWh", solarChargerUnits[i], pvPower, pvYieldTotal);
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
                inverterData.pvTotal += pvYieldTotal;
                
                // Read daily yield from register 784 (/History/Daily/0/Yield)
                ModbusResponse dailyResponse = channel.sendModbusRequest(solarChargerUnits[i], 0x03, 784, 1);
                if (dailyResponse.isValid)
                {
                    double dailyYield = dailyResponse.readUInt16(784) / 10.0;  // kWh, scale 10
                    inverterData.pvToday += dailyYield;
                    LOGD("Victron: Solar Charger unit %d daily yield: %.2f kWh", solarChargerUnits[i], dailyYield);
                }
                else
                {
                    LOGD("Victron: Solar Charger unit %d daily yield read failed", solarChargerUnits[i]);
                }
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
        LOGD("Victron: Solar Charger scan complete, found %d chargers, pvToday=%.2f kWh", solarChargerIndex, inverterData.pvToday);

        // If no PV from Solar Chargers, try Multi RS
        int totalPvPower = inverterData.pv1Power + inverterData.pv2Power + 
                           inverterData.pv3Power + inverterData.pv4Power;

        if (totalPvPower == 0)
        {
            LOGD("Victron: No PV from Solar Chargers, trying Multi RS...");
            // Multi RS - registers 4598-4601 (/Pv/0-3/P)
            for (int i = 0; i < sizeof(multiRsUnits); i++)
            {
                if (multiRsUnits[i] == 0)
                {
                    continue;
                }

                response = channel.sendModbusRequest(multiRsUnits[i], 0x03, 4598, 4);
                if (response.isValid)
                {
                    inverterData.pv1Power = response.readUInt16(4598);
                    inverterData.pv2Power = response.readUInt16(4599);
                    inverterData.pv3Power = response.readUInt16(4600);
                    inverterData.pv4Power = response.readUInt16(4601);
                    LOGD("Victron: PV from Multi RS unit %d: %d/%d/%d/%d W",
                         multiRsUnits[i], inverterData.pv1Power, inverterData.pv2Power,
                         inverterData.pv3Power, inverterData.pv4Power);
                    
                    // Read daily yield from register 4574 (/History/Daily/0/Yield)
                    ModbusResponse dailyResponse = channel.sendModbusRequest(multiRsUnits[i], 0x03, 4574, 1);
                    if (dailyResponse.isValid)
                    {
                        inverterData.pvToday = dailyResponse.readUInt16(4574) / 10.0;  // kWh, scale 10
                        LOGD("Victron: Multi RS daily yield: %.2f kWh", inverterData.pvToday);
                    }
                    else
                    {
                        LOGD("Victron: Multi RS daily yield read failed");
                    }
                    
                    // Read total yield from register 4603 (/Yield/User)
                    ModbusResponse totalResponse = channel.sendModbusRequest(multiRsUnits[i], 0x03, 4603, 2);
                    if (totalResponse.isValid)
                    {
                        inverterData.pvTotal = totalResponse.readUInt32(4603);
                        LOGD("Victron: Multi RS total yield: %.2f kWh", inverterData.pvTotal);
                    }
                    break; // Found Multi RS
                }
                else if (!multiRsUnitsInitialized)
                {
                    multiRsUnits[i] = 0;
                }
            }
            multiRsUnitsInitialized = true;
        }

        // If still no PV, try RS Smart Inverter
        totalPvPower = inverterData.pv1Power + inverterData.pv2Power +
                       inverterData.pv3Power + inverterData.pv4Power;

        if (totalPvPower == 0)
        {
            LOGD("Victron: No PV from Multi RS, trying RS Smart Inverter...");
            // RS Smart Inverter - registers 3164-3167 (/Pv/0-3/P)
            for (int i = 0; i < sizeof(rsInverterUnits); i++)
            {
                if (rsInverterUnits[i] == 0)
                {
                    continue;
                }

                response = channel.sendModbusRequest(rsInverterUnits[i], 0x03, 3164, 4);
                if (response.isValid)
                {
                    inverterData.pv1Power = response.readUInt16(3164);
                    inverterData.pv2Power = response.readUInt16(3165);
                    inverterData.pv3Power = response.readUInt16(3166);
                    inverterData.pv4Power = response.readUInt16(3167);
                    LOGD("Victron: PV from RS Inverter unit %d: %d/%d/%d/%d W",
                         rsInverterUnits[i], inverterData.pv1Power, inverterData.pv2Power,
                         inverterData.pv3Power, inverterData.pv4Power);
                    
                    // Read daily yield per string from registers 3148-3151 (/History/Daily/0/Pv/0-3/Yield)
                    ModbusResponse dailyResponse = channel.sendModbusRequest(rsInverterUnits[i], 0x03, 3148, 4);
                    if (dailyResponse.isValid)
                    {
                        inverterData.pvToday = (dailyResponse.readUInt16(3148) + dailyResponse.readUInt16(3149) +
                                               dailyResponse.readUInt16(3150) + dailyResponse.readUInt16(3151)) / 10.0;  // kWh, scale 10
                        LOGD("Victron: RS Inverter daily yield: %.2f kWh", inverterData.pvToday);
                    }
                    else
                    {
                        LOGD("Victron: RS Inverter daily yield read failed");
                    }
                    
                    // Read total yield from registers 3134+3136 (/Energy/SolarToAcOut + /Energy/SolarToBattery)
                    ModbusResponse totalResponse = channel.sendModbusRequest(rsInverterUnits[i], 0x03, 3134, 4);
                    if (totalResponse.isValid)
                    {
                        inverterData.pvTotal = (totalResponse.readUInt32(3134) + totalResponse.readUInt32(3136)) / 100.0;  // kWh, scale 100
                        LOGD("Victron: RS Inverter total yield: %.2f kWh", inverterData.pvTotal);
                    }
                    break; // Found RS Inverter
                }
                else if (!rsInverterUnitsInitialized)
                {
                    rsInverterUnits[i] = 0;
                }
            }
            rsInverterUnitsInitialized = true;
        }

        // Final fallback to system aggregate DC PV + AC-coupled PV
        totalPvPower = inverterData.pv1Power + inverterData.pv2Power +
                       inverterData.pv3Power + inverterData.pv4Power;

        if (totalPvPower == 0)
        {
            LOGD("Victron: No PV from RS Inverter, trying system PV fallbacks...");
            // Read system DC PV (reg 850) - single register to avoid exception on missing regs
            int systemDcPv = 0;
            response = channel.sendModbusRequest(100, 0x03, 850, 2);
            if (response.isValid)
            {
                systemDcPv = response.readUInt16(850);
                LOGD("Victron: System DC PV (reg 850): %d W, DC PV current (reg 851): %d", systemDcPv, response.readUInt16(851));
            }

            // Read AC PV on Grid input (reg 855-857) - separate request
            int acPvOnGridL1 = 0, acPvOnGridL2 = 0, acPvOnGridL3 = 0;
            response = channel.sendModbusRequest(100, 0x03, 855, 3);
            if (response.isValid)
            {
                acPvOnGridL1 = response.readUInt16(855);
                acPvOnGridL2 = response.readUInt16(856);
                acPvOnGridL3 = response.readUInt16(857);
                LOGD("Victron: AC PV on Grid (reg 855-857): %d/%d/%d W", acPvOnGridL1, acPvOnGridL2, acPvOnGridL3);
            }

            // Read AC PV on Genset input (reg 860-862) - separate request
            int acPvOnGensetL1 = 0, acPvOnGensetL2 = 0, acPvOnGensetL3 = 0;
            response = channel.sendModbusRequest(100, 0x03, 860, 3);
            if (response.isValid)
            {
                acPvOnGensetL1 = response.readUInt16(860);
                acPvOnGensetL2 = response.readUInt16(861);
                acPvOnGensetL3 = response.readUInt16(862);
                LOGD("Victron: AC PV on Genset (reg 860-862): %d/%d/%d W", acPvOnGensetL1, acPvOnGensetL2, acPvOnGensetL3);
            }

            // Sum all PV sources: DC + AC on Output + AC on Grid + AC on Genset
            int acPvOnOutput = acPvOnOutputL1 + acPvOnOutputL2 + acPvOnOutputL3;
            int acPvOnGrid = acPvOnGridL1 + acPvOnGridL2 + acPvOnGridL3;
            int acPvOnGenset = acPvOnGensetL1 + acPvOnGensetL2 + acPvOnGensetL3;
            int totalSystemPv = systemDcPv + acPvOnOutput + acPvOnGrid + acPvOnGenset;
            LOGD("Victron: Total system PV: DC=%d + AcOut=%d + AcGrid=%d + AcGenset=%d = %d W",
                 systemDcPv, acPvOnOutput, acPvOnGrid, acPvOnGenset, totalSystemPv);

            if (totalSystemPv > 0)
            {
                inverterData.pv1Power = totalSystemPv;
                LOGD("Victron: Using system PV fallback: %d W", totalSystemPv);

                // Local integration for daily yield (fallback only)
                if (lastPvPowerTime != 0)
                {
                    double addedWh = totalSystemPv * (inverterData.millis - lastPvPowerTime) / 1000.0 / 3600.0;
                    pvTodayIntegrated += addedWh;
                    LOGD("Victron: System PV fallback integration: +%.2f Wh, total=%.2f Wh", addedWh, pvTodayIntegrated);
                }
                lastPvPowerTime = inverterData.millis;
            }
        }

        response = channel.sendModbusRequest(225, 0x03, 262, 16);
        if (response.isValid)
        {
            inverterData.batteryTemperature = response.readUInt16(262) / 10;
        }

        response = channel.sendModbusRequest(100, 0x03, 830, 4);
        if (response.isValid)
        {
            time_t time = response.readUInt64(830);
            
            // Store inverter RTC time
            inverterData.inverterTime = time;

            struct tm *tm = localtime(&time);
            int day = tm->tm_mday;
            if (this->day != day)
            {
                this->day = day;
                pvTotal = inverterData.pvTotal;
                gridBuyTotal = inverterData.gridBuyTotal;
                gridSellTotal = inverterData.gridSellTotal;
                loadTotal = inverterData.loadTotal;
                batteryChargedToday = 0;
                batteryDischargedToday = 0;
                pvTodayIntegrated = 0;  // Reset local PV integration at midnight
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
            // Use pvToday from registers if available, otherwise use local calculation or integration
            if (inverterData.pvToday == 0)
            {
                if (pvTodayIntegrated > 0)
                {
                    inverterData.pvToday = pvTodayIntegrated / 1000.0;  // Convert Wh to kWh
                    LOGD("Victron: Using integrated pvToday: %.2f kWh", inverterData.pvToday);
                }
                else
                {
                    inverterData.pvToday = inverterData.pvTotal - pvTotal;  // Fallback to local calculation
                    LOGD("Victron: Using calculated pvToday: %.2f kWh (pvTotal=%.2f - stored=%.2f)", 
                         inverterData.pvToday, inverterData.pvTotal, pvTotal);
                }
            }
            inverterData.gridBuyToday = inverterData.gridBuyTotal - gridBuyTotal;
            inverterData.gridSellToday = inverterData.gridSellTotal - gridSellTotal;
            inverterData.loadToday = inverterData.loadTotal - loadTotal;
            LOGD("Victron: Daily stats - pvToday=%.2f, gridBuy=%.2f, gridSell=%.2f, load=%.2f kWh", 
                 inverterData.pvToday, inverterData.gridBuyToday, inverterData.gridSellToday, inverterData.loadToday);

            inverterData.batteryChargedToday = batteryChargedToday / 1000.0;       // convert to kWh
            inverterData.batteryDischargedToday = batteryDischargedToday / 1000.0; // convert to kWh
        }
        inverterData.hasBattery = inverterData.soc != 0 || inverterData.batteryPower != 0;
        LOGD("Victron: Final PV power: %d/%d/%d/%d W", inverterData.pv1Power, inverterData.pv2Power, inverterData.pv3Power, inverterData.pv4Power);
        logInverterData(inverterData, millis() - inverterData.millis);
        channel.disconnect();

        return inverterData;
    }

    /**
     * Sets the work mode of the Victron system for intelligent battery control
     * 
     * Victron ESS Modes (via VE.Bus registers):
     * The Victron system uses ESS (Energy Storage System) mode control:
     * - Register 2700 (unit 100): ESS Mode (1=Optimized with BatteryLife, 2=Optimized without BatteryLife, 3=Keep charged)
     * - Register 2701 (unit 100): Minimum SOC (0-100%)
     * - Register 2702 (unit 100): Active SOC Limit (0-100%)
     * 
     * For force charge/discharge, Victron uses:
     * - Unit 246 (VE.Bus): Control registers
     * - Register 37 (246): Switch position (1=Charger only, 2=Inverter only, 3=On, 4=Off)
     * - Register 38 (246): Charge current limit
     * 
     * @param ipAddress IP address of the Venus GX / Cerbo GX
     * @param mode Desired inverter mode
     * @return true if mode was set successfully
     */
    bool setWorkMode(const String& ipAddress, SolarInverterMode_t mode)
    {
        IPAddress ip;
        if (!ipAddress.isEmpty())
        {
            ip.fromString(ipAddress);
            if (!channel.connect(ip, 502))
            {
                channel.disconnect();
                return false;
            }
        }
        else
        {
            if (!channel.connect(String("venus.local"), 502))
            {
                channel.disconnect();
                return false;
            }
        }

        bool success = false;

        switch (mode)
        {
        case SI_MODE_SELF_USE:
            // ESS Mode: Optimized with BatteryLife (mode 1)
            success = channel.writeSingleRegister(VICTRON_UNIT_SYSTEM, VICTRON_REG_ESS_MODE, VICTRON_ESS_OPTIMIZED_BATTERYLIFE);
            if (success)
            {
                // Set reasonable min SOC (e.g., 20%)
                channel.writeSingleRegister(VICTRON_UNIT_SYSTEM, VICTRON_REG_ESS_MIN_SOC, 20);
            }
            break;

        case SI_MODE_HOLD_BATTERY:
            // ESS Mode: Keep charged (mode 3) - battery stays full
            success = channel.writeSingleRegister(VICTRON_UNIT_SYSTEM, VICTRON_REG_ESS_MODE, VICTRON_ESS_KEEP_CHARGED);
            break;

        case SI_MODE_CHARGE_FROM_GRID:
            // Set VE.Bus to Charger Only mode
            success = channel.writeSingleRegister(VICTRON_UNIT_VEBUS, VICTRON_REG_VEBUS_SWITCH, VICTRON_VEBUS_CHARGER_ONLY);
            break;

        case SI_MODE_DISCHARGE_TO_GRID:
            // ESS Mode: Optimized without BatteryLife (mode 2) + low min SOC
            success = channel.writeSingleRegister(VICTRON_UNIT_SYSTEM, VICTRON_REG_ESS_MODE, VICTRON_ESS_OPTIMIZED_NO_BATTERYLIFE);
            if (success)
            {
                // Set very low min SOC to allow deep discharge
                channel.writeSingleRegister(VICTRON_UNIT_SYSTEM, VICTRON_REG_ESS_MIN_SOC, 10);
            }
            break;

        default:
            break;
        }

        channel.disconnect();
        return success;
    }

private:
    // Victron Modbus Unit IDs
    static constexpr uint8_t VICTRON_UNIT_SYSTEM = 100;   // System / Venus
    static constexpr uint8_t VICTRON_UNIT_VEBUS = 246;    // VE.Bus System
    
    // Victron ESS registers (Unit 100)
    static constexpr uint16_t VICTRON_REG_ESS_MODE = 2700;     // ESS Mode
    static constexpr uint16_t VICTRON_REG_ESS_MIN_SOC = 2701;  // Min SOC %
    static constexpr uint16_t VICTRON_REG_ESS_ACTIVE_SOC = 2702; // Active SOC Limit
    
    // Victron VE.Bus registers (Unit 246)
    static constexpr uint16_t VICTRON_REG_VEBUS_SWITCH = 37;   // Switch Position
    static constexpr uint16_t VICTRON_REG_VEBUS_CHARGE_CURRENT = 38; // Charge Current Limit
    
    // Victron ESS Mode values
    static constexpr uint16_t VICTRON_ESS_OPTIMIZED_BATTERYLIFE = 1;     // Optimized with BatteryLife
    static constexpr uint16_t VICTRON_ESS_OPTIMIZED_NO_BATTERYLIFE = 2;  // Optimized without BatteryLife
    static constexpr uint16_t VICTRON_ESS_KEEP_CHARGED = 3;              // Keep batteries charged
    
    // Victron VE.Bus Switch Position values
    static constexpr uint16_t VICTRON_VEBUS_CHARGER_ONLY = 1;   // Charger Only
    static constexpr uint16_t VICTRON_VEBUS_INVERTER_ONLY = 2;  // Inverter Only
    static constexpr uint16_t VICTRON_VEBUS_ON = 3;             // On (Normal)
    static constexpr uint16_t VICTRON_VEBUS_OFF = 4;            // Off
};