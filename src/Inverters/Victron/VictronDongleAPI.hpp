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
                log_d("Failed to connect to Victron dongle");
                inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                channel.disconnect();
                return inverterData;
            }
        }
        else
        {
            if (!channel.connect(String("venus.local"), 502))
            {
                log_d("Failed to connect to Victron dongle");
                inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
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
            //inverterData.inverterPower = inverterData.L1Power + inverterData.L2Power + inverterData.L3Power;
            inverterData.gridPowerL1 = -1 * response.readInt16(820);
            inverterData.gridPowerL2 = -1 * response.readInt16(821);
            inverterData.gridPowerL3 = -1 * response.readInt16(822);
            //inverterData.gridPower = inverterData.gridPowerL1 + inverterData.gridPowerL2 + inverterData.gridPowerL3;
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
            
            // Store inverter RTC time
            inverterData.inverterTime = time;

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
    bool setWorkMode(const String& ipAddress, InverterMode_t mode)
    {
        IPAddress ip;
        if (!ipAddress.isEmpty())
        {
            ip.fromString(ipAddress);
            if (!channel.connect(ip, 502))
            {
                log_d("Failed to connect to Victron for setWorkMode");
                channel.disconnect();
                return false;
            }
        }
        else
        {
            if (!channel.connect(String("venus.local"), 502))
            {
                log_d("Failed to connect to Victron for setWorkMode");
                channel.disconnect();
                return false;
            }
        }

        log_d("Setting Victron work mode to %d", mode);
        bool success = false;

        switch (mode)
        {
        case INVERTER_MODE_SELF_USE:
            // ESS Mode: Optimized with BatteryLife (mode 1)
            success = channel.writeSingleRegister(VICTRON_UNIT_SYSTEM, VICTRON_REG_ESS_MODE, VICTRON_ESS_OPTIMIZED_BATTERYLIFE);
            if (success)
            {
                // Set reasonable min SOC (e.g., 20%)
                channel.writeSingleRegister(VICTRON_UNIT_SYSTEM, VICTRON_REG_ESS_MIN_SOC, 20);
            }
            break;

        case INVERTER_MODE_HOLD_BATTERY:
            // ESS Mode: Keep charged (mode 3) - battery stays full
            success = channel.writeSingleRegister(VICTRON_UNIT_SYSTEM, VICTRON_REG_ESS_MODE, VICTRON_ESS_KEEP_CHARGED);
            break;

        case INVERTER_MODE_CHARGE_FROM_GRID:
            // Set VE.Bus to Charger Only mode
            success = channel.writeSingleRegister(VICTRON_UNIT_VEBUS, VICTRON_REG_VEBUS_SWITCH, VICTRON_VEBUS_CHARGER_ONLY);
            break;

        case INVERTER_MODE_DISCHARGE_TO_GRID:
            // ESS Mode: Optimized without BatteryLife (mode 2) + low min SOC
            success = channel.writeSingleRegister(VICTRON_UNIT_SYSTEM, VICTRON_REG_ESS_MODE, VICTRON_ESS_OPTIMIZED_NO_BATTERYLIFE);
            if (success)
            {
                // Set very low min SOC to allow deep discharge
                channel.writeSingleRegister(VICTRON_UNIT_SYSTEM, VICTRON_REG_ESS_MIN_SOC, 10);
            }
            break;

        default:
            log_d("Unknown mode: %d", mode);
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