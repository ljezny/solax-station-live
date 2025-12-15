#pragma once

#include "../../Protocol/V5TCP.hpp"
#include "../../utils/RemoteLogger.hpp"

class SofarSolarDongleAPI
{
public:
    /**
     * Returns true if this inverter supports intelligence mode control
     */
    bool supportsIntelligence() { return false; }

    InverterData_t loadData(String ipAddress, String sn)
    {
        return readData(ipAddress, sn);
    }

private:
    V5TCP channel;

    InverterData_t readData(String ipAddress, String dongleSN)
    {
        InverterData_t inverterData{};
        
        // Validate SN length - should be max 10 digits for uint32_t
        if (dongleSN.length() > 10) {
            log_e("Dongle SN '%s' is too long (%d chars). Max 10 digits allowed. Truncating.", 
                  dongleSN.c_str(), dongleSN.length());
            dongleSN = dongleSN.substring(0, 10);
        }
        
        uint32_t sn = strtoul(dongleSN.c_str(), NULL, 10);
        
        // Check for overflow
        if (sn == ULONG_MAX || sn == 0) {
            log_e("Invalid SN conversion: '%s' -> %lu. Check SN format.", dongleSN.c_str(), sn);
        }
        
        log_d("SN: %lu (from string: '%s')", sn, dongleSN.c_str());
        inverterData.sn = dongleSN;
        byte packetBuffer[1024];

        channel.ensureIPAddress(ipAddress);

        // PV Input
        if (!channel.tryReadWithRetries(0x586, 0x58F - 0x586 + 1, sn, packetBuffer, [&]()
                                {
            inverterData.millis = millis();
            inverterData.pv1Power = channel.readUInt16(packetBuffer, 0) * 10;  // 0x586
            inverterData.pv2Power = channel.readUInt16(packetBuffer, 3) * 10;  // 0x589
            inverterData.pv3Power = channel.readUInt16(packetBuffer, 6) * 10;  // 0x58C
            inverterData.pv4Power = channel.readUInt16(packetBuffer, 9) * 10;  // 0x58F
            }))
            return inverterData;

        // Battery Input
        if (!channel.tryReadWithRetries(0x667, 2, sn, packetBuffer, [&]()
                                {
            inverterData.batteryPower = channel.readInt16(packetBuffer, 0) * 100;
            inverterData.soc = channel.readUInt16(packetBuffer, 1); }))
            return inverterData;

        // Battery Temperature
        channel.tryReadWithRetries(0x607, 1, sn, packetBuffer, [&]()
                           { inverterData.batteryTemperature = channel.readInt16(packetBuffer, 0); });

        // Inverter Temperature
        channel.tryReadWithRetries(0x418, 1, sn, packetBuffer, [&]()
                           { inverterData.inverterTemperature = channel.readInt16(packetBuffer, 0); });

        // Grid Input
        if (!channel.tryReadWithRetries(0x484, 0x4BC - 0x484 + 1, sn, packetBuffer, [&]()
                                {
            inverterData.inverterOutpuPowerL1 = channel.readInt16(packetBuffer, 0x485 - 0x484) * 10;
            inverterData.loadPower = channel.readInt16(packetBuffer, 0x04AF - 0x484) * 10;
            //inverterData.gridPower = channel.readInt16(packetBuffer, 0x0488 - 0x484) * 10;
            inverterData.gridPowerL1 = channel.readInt16(packetBuffer, 0x493 - 0x484) * 10;
            inverterData.gridPowerL2 = channel.readInt16(packetBuffer, 0x49E - 0x484) * 10;
            inverterData.gridPowerL3 = channel.readInt16(packetBuffer, 0x4A9 - 0x484) * 10;
            inverterData.inverterOutpuPowerL1 = channel.readInt16(packetBuffer, 0x48F - 0x484) * 10;
            inverterData.inverterOutpuPowerL2 = channel.readInt16(packetBuffer, 0x49A - 0x484) * 10;
            inverterData.inverterOutpuPowerL3 = channel.readInt16(packetBuffer, 0x4A9 - 0x484) * 10; }))
            return inverterData;

        // Stats
        if (!channel.tryReadWithRetries(0x684, 0x698 - 0x684 + 2, sn, packetBuffer, [&]()
                                {
            inverterData.pvToday = channel.readUInt32(packetBuffer, 0x684 - 0x684) / 100.0f;
            inverterData.pvTotal = channel.readUInt32(packetBuffer, 0x686 - 0x684) / 10.0f;
            inverterData.loadToday = channel.readUInt32(packetBuffer, 0x688 - 0x684) / 100.0f;
            inverterData.loadTotal = channel.readUInt32(packetBuffer, 0x68A - 0x684) / 10.0f;
            inverterData.batteryChargedToday = channel.readUInt32(packetBuffer, 0x694 - 0x684) / 100.0f;
            inverterData.batteryDischargedToday = channel.readUInt32(packetBuffer, 0x698 - 0x684) / 100.0f;
            inverterData.gridBuyToday = channel.readUInt32(packetBuffer, 0x68C - 0x684) / 100.0f;
            inverterData.gridBuyTotal = channel.readUInt32(packetBuffer, 0x68E - 0x684) / 10.0f;
            inverterData.gridSellToday = channel.readUInt32(packetBuffer, 0x690 - 0x684) / 100.0f;
            inverterData.gridSellTotal = channel.readUInt32(packetBuffer, 0x692 - 0x684) / 10.0f;
            inverterData.status = DONGLE_STATUS_OK; }))
            return inverterData;

        // Read RTC time from SofarSolar inverter
        readInverterRTC(sn, inverterData);

        inverterData.hasBattery = inverterData.soc != 0 || inverterData.batteryPower != 0;
        logInverterData(inverterData, millis() - inverterData.millis);
        return inverterData;
    }

    void readInverterRTC(uint32_t sn, InverterData_t &inverterData)
    {
        // SofarSolar RTC registers: 0x42C-0x431 (holding registers)
        // Format: Year (% 100), Month, Day, Hour, Minute, Second
        byte packetBuffer[16];
        channel.tryReadWithRetries(0x42C, 6, sn, packetBuffer, [&]()
                                   {
                                       struct tm timeinfo = {};
                                       // Rok je uložen jako year % 100 (např. 25 pro 2025)
                                       timeinfo.tm_year = channel.readUInt16(packetBuffer, 0) + 100;  // +100 protože tm_year je od 1900
                                       timeinfo.tm_mon = channel.readUInt16(packetBuffer, 1) - 1;     // Month 1-12 to 0-11
                                       timeinfo.tm_mday = channel.readUInt16(packetBuffer, 2);
                                       timeinfo.tm_hour = channel.readUInt16(packetBuffer, 3);
                                       timeinfo.tm_min = channel.readUInt16(packetBuffer, 4);
                                       timeinfo.tm_sec = channel.readUInt16(packetBuffer, 5);
                                       timeinfo.tm_isdst = -1;

                                       inverterData.inverterTime = mktime(&timeinfo);
                                   });
    }

    /**
     * Sets the work mode of the SofarSolar inverter for intelligent battery control
     * 
     * SofarSolar Work Modes (register 0x1200 / 4608):
     * 0 = Self-Use
     * 1 = Time of Use
     * 2 = Timing Mode
     * 3 = Passive Mode
     * 4 = Peak Cut Mode
     * 
     * For charge/discharge control:
     * - Register 0x1201 (4609): Passive Mode target power (signed, negative=charge)
     * - Register 0x1202 (4610): Min SOC
     * - Register 0x1203 (4611): Max SOC
     * 
     * @param ipAddress IP address of the dongle
     * @param dongleSN Serial number of the dongle
     * @param mode Desired inverter mode
     * @return true if mode was set successfully
     */
    bool setWorkMode(const String& ipAddress, const String& dongleSN, InverterMode_t mode)
    {
        uint32_t sn = strtoul(dongleSN.c_str(), NULL, 10);
        if (sn == 0)
        {
            return false;
        }

        channel.ensureIPAddress(ipAddress);

        bool success = false;

        switch (mode)
        {
        case INVERTER_MODE_SELF_USE:
            // Self-Use mode
            success = writeRegister(sn, SOFAR_REG_WORK_MODE, SOFAR_MODE_SELF_USE);
            break;

        case INVERTER_MODE_HOLD_BATTERY:
            // Passive Mode with 0 power target = hold battery
            success = writeRegister(sn, SOFAR_REG_WORK_MODE, SOFAR_MODE_PASSIVE);
            if (success)
            {
                writeRegister(sn, SOFAR_REG_PASSIVE_POWER, 0);  // 0W = hold
            }
            break;

        case INVERTER_MODE_CHARGE_FROM_GRID:
            // Passive Mode with negative power target = charge
            success = writeRegister(sn, SOFAR_REG_WORK_MODE, SOFAR_MODE_PASSIVE);
            if (success)
            {
                // Set negative power to charge (e.g., -3000W)
                // Sofar uses signed 16-bit value, negative = charge from grid
                writeRegister(sn, SOFAR_REG_PASSIVE_POWER, (uint16_t)(-3000 & 0xFFFF));
            }
            break;

        case INVERTER_MODE_DISCHARGE_TO_GRID:
            // Passive Mode with positive power target = discharge
            success = writeRegister(sn, SOFAR_REG_WORK_MODE, SOFAR_MODE_PASSIVE);
            if (success)
            {
                writeRegister(sn, SOFAR_REG_PASSIVE_POWER, 5000);  // +5000W discharge
            }
            break;

        default:
            break;
        }

        return success;
    }

    // Overload for compatibility - without dongleSN parameter
    bool setWorkMode(const String& ipAddress, InverterMode_t mode)
    {
        return false;
    }

private:
    // SofarSolar Modbus register addresses
    static constexpr uint16_t SOFAR_REG_WORK_MODE = 0x1200;      // Work Mode (4608)
    static constexpr uint16_t SOFAR_REG_PASSIVE_POWER = 0x1201;  // Passive Mode Power Target (4609)
    static constexpr uint16_t SOFAR_REG_MIN_SOC = 0x1202;        // Min SOC (4610)
    static constexpr uint16_t SOFAR_REG_MAX_SOC = 0x1203;        // Max SOC (4611)
    
    // SofarSolar Work Mode values
    static constexpr uint16_t SOFAR_MODE_SELF_USE = 0;           // Self-Use
    static constexpr uint16_t SOFAR_MODE_TIME_OF_USE = 1;        // Time of Use
    static constexpr uint16_t SOFAR_MODE_TIMING = 2;             // Timing Mode
    static constexpr uint16_t SOFAR_MODE_PASSIVE = 3;            // Passive Mode
    static constexpr uint16_t SOFAR_MODE_PEAK_CUT = 4;           // Peak Cut Mode

    /**
     * Write a single register using Solarman V5 protocol
     */
    bool writeRegister(uint32_t sn, uint16_t addr, uint16_t value)
    {
        
        if (!channel.connect(channel.ip))
        {
            return false;
        }
        
        bool success = channel.writeSingleRegister(addr, value, sn);
        channel.disconnect();
        return success;
    }
};