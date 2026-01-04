#pragma once

#include "../../Protocol/V5TCP.hpp"

class DeyeDongleAPI
{
public:
    /**
     * Returns true if this inverter supports intelligence mode control
     */
    bool supportsIntelligence() { return false; }

    InverterData_t loadData(String ipAddress, String dongleSN)
    {
        return readData(ipAddress, dongleSN);
    }

private:
    V5TCP channel;
    IPAddress ip;

    InverterData_t readData(String ipAddress, String dongleSN)
    {
        int powerMultiplier = 1;
        uint16_t deviceType = 0;
        InverterData_t inverterData;
        LOGD("Connecting to dongle...");
        
        // Validate SN length - should be max 10 digits for uint32_t
        if (dongleSN.length() > 10) {
            LOGE("Dongle SN '%s' is too long (%d chars). Max 10 digits allowed. Truncating.", 
                  dongleSN.c_str(), dongleSN.length());
            dongleSN = dongleSN.substring(0, 10);
        }
        
        uint32_t sn = strtoul(dongleSN.c_str(), NULL, 10);
        
        // Check for overflow (strtoul returns ULONG_MAX on overflow)
        if (sn == ULONG_MAX || sn == 0) {
            LOGE("Invalid SN conversion: '%s' -> %lu. Check SN format.", dongleSN.c_str(), sn);
        }
        
        LOGD("SN: %lu (from string: '%s')", sn, dongleSN.c_str());
        byte packetBuffer[1024];
        channel.ensureIPAddress(ipAddress);

        if (!channel.tryReadWithRetries(0, 8 - 0 + 1, sn, packetBuffer, [&]()
                                        {
                    inverterData.millis = millis();
                    inverterData.status = DONGLE_STATUS_OK;
                    deviceType = channel.readUInt16(packetBuffer, 0);
                    LOGD("Device type: %s", String(deviceType, HEX));
                    uint16_t commProtoVer = channel.readUInt16(packetBuffer, 2);
                    LOGD("Comm protocol version: %s", String(commProtoVer, HEX));
                    String inverterSN = channel.readString(packetBuffer, 3, 10);
                    LOGD("Inverter SN: %s", inverterSN.c_str());
                    inverterData.sn = inverterSN; }))
            return inverterData;

        if (deviceType == 2 || deviceType == 3)
        {
            loadMicroInverter(sn, inverterData);
        } else {
            load3PhaseInverter(deviceType, sn, inverterData);
        }

        // Read RTC time from Deye inverter (registers 22-27)
        readInverterRTC(sn, inverterData);

        inverterData.hasBattery = inverterData.soc != 0 || inverterData.batteryPower != 0;
        logInverterData(inverterData);

        return inverterData;
    }

    void loadMicroInverter(uint32_t sn, InverterData_t &inverterData)
    {
        LOGD("Loading micro inverter data...");
        byte packetBuffer[1024];
        if (!channel.tryReadWithRetries(150, 197 - 150 + 1, sn, packetBuffer, [&]()
                                        {
                                            inverterData.pv1Power = channel.readUInt16(packetBuffer, 186 - 150);
                                            inverterData.pv2Power = channel.readUInt16(packetBuffer, 187 - 150);
                                            inverterData.pv3Power = channel.readUInt16(packetBuffer, 188 - 150);
                                            inverterData.pv4Power = channel.readUInt16(packetBuffer, 189 - 150);
                                            inverterData.inverterOutpuPowerL1 = channel.readInt16(packetBuffer, 175 - 150);
                                            inverterData.loadPower = channel.readInt16(packetBuffer, 178 - 150);
                                            inverterData.batteryTemperature = (channel.readInt16(packetBuffer, 182 - 150) - 1000) / 10;
                                            inverterData.soc = channel.readUInt16(packetBuffer, 184 - 150);
                                            inverterData.batteryPower = -1 * channel.readInt16(packetBuffer, 190 - 150);
                                            inverterData.gridPowerL1 = -1 * channel.readInt16(packetBuffer, 169 - 150);
                                        }))
            return;

                if (!channel.tryReadWithRetries(60, 99 - 60 + 1, sn, packetBuffer, [&]()
                                        {
                inverterData.inverterTemperature = (channel.readInt16(packetBuffer, 90 - 60) - 1000) / 10;
                inverterData.pvToday = channel.readUInt16(packetBuffer, 60 - 60) / 10.0f;
                inverterData.pvTotal = channel.readUInt16(packetBuffer, 63 - 60) / 10.0f;
                inverterData.loadToday = channel.readUInt16(packetBuffer, 84 - 60) / 10.0f;
                inverterData.batteryChargedToday = channel.readUInt16(packetBuffer, 70 - 60) / 10.0f;
                inverterData.batteryDischargedToday = channel.readUInt16(packetBuffer, 71 - 60) / 10.0f;
                inverterData.gridBuyToday = channel.readUInt16(packetBuffer, 76 - 60) / 10.0f;
                inverterData.gridSellToday = channel.readUInt16(packetBuffer, 77 - 60) / 10.0f;
                                        }))
            return;
    }

    void readInverterRTC(uint32_t sn, InverterData_t &inverterData)
    {
        // Deye RTC registers: 22-27 (holding registers)
        // 22: Year, 23: Month, 24: Day, 25: Hour, 26: Minute, 27: Second
        byte packetBuffer[16];
        channel.tryReadWithRetries(22, 6, sn, packetBuffer, [&]()
                                   {
                                       struct tm timeinfo = {};
                                       timeinfo.tm_year = channel.readUInt16(packetBuffer, 0) - 1900;  // Year - 1900
                                       timeinfo.tm_mon = channel.readUInt16(packetBuffer, 1) - 1;     // Month 1-12 to 0-11
                                       timeinfo.tm_mday = channel.readUInt16(packetBuffer, 2);
                                       timeinfo.tm_hour = channel.readUInt16(packetBuffer, 3);
                                       timeinfo.tm_min = channel.readUInt16(packetBuffer, 4);
                                       timeinfo.tm_sec = channel.readUInt16(packetBuffer, 5);
                                       timeinfo.tm_isdst = -1;

                                       inverterData.inverterTime = mktime(&timeinfo);
                                       LOGD("Deye RTC: %04d-%02d-%02d %02d:%02d:%02d",
                                             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                                             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
                                   });
    }

    void load3PhaseInverter(int deviceType, uint32_t sn, InverterData_t &inverterData)
    {
        LOGD("Loading 3-phase inverter data...");
        int powerMultiplier = (deviceType == 6) ? 10 : 1;
        byte packetBuffer[1024];
        if (!channel.tryReadWithRetries(672, 675 - 672 + 1, sn, packetBuffer, [&]()
                                        {
                inverterData.pv1Power = powerMultiplier * channel.readUInt16(packetBuffer, 672 - 672);
                inverterData.pv2Power = powerMultiplier * channel.readUInt16(packetBuffer, 673 - 672);
                inverterData.pv3Power = powerMultiplier * channel.readUInt16(packetBuffer, 674 - 672);
                inverterData.pv4Power = powerMultiplier * channel.readUInt16(packetBuffer, 675 - 672); }))
            return;

        if (!channel.tryReadWithRetries(586, 591 - 586 + 1, sn, packetBuffer, [&]()
                                        {
                                            inverterData.batteryTemperature = (channel.readInt16(packetBuffer, 586 - 586) - 1000) / 10;
                                            inverterData.soc = channel.readUInt16(packetBuffer, 588 - 586);
                                            inverterData.batteryPower = -1 * powerMultiplier * channel.readInt16(packetBuffer, 590 - 586); // Battery power flow - negative for charging, positive for discharging
                                        }))
            return;

        // Read max charge/discharge current from registers 108/109
        // Scale is 0.1A according to Deye documentation
        channel.tryReadWithRetries(108, 2, sn, packetBuffer, [&]()
                                   {
                                       float maxChargeCurrent = channel.readUInt16(packetBuffer, 108 - 108) * 0.1f;
                                       float maxDischargeCurrent = channel.readUInt16(packetBuffer, 109 - 108) * 0.1f;
                                       // Estimate battery voltage from battery power and assume ~50V for HV battery
                                       float batteryVoltage = 50.0f; // Default for HV batteries
                                       inverterData.maxChargePowerW = (uint16_t)(maxChargeCurrent * batteryVoltage);
                                       inverterData.maxDischargePowerW = (uint16_t)(maxDischargeCurrent * batteryVoltage);
                                       LOGD("Deye max charge current: %.1f A, max discharge current: %.1f A", maxChargeCurrent, maxDischargeCurrent);
                                       LOGD("Max charge power: %d W, max discharge power: %d W", inverterData.maxChargePowerW, inverterData.maxDischargePowerW);
                                   });

        if (!channel.tryReadWithRetries(598, 655 - 598 + 1, sn, packetBuffer, [&]()
                                        {
                inverterData.inverterOutpuPowerL1 = channel.readInt16(packetBuffer, 633 - 598);
                inverterData.inverterOutpuPowerL2 = channel.readInt16(packetBuffer, 634 - 598);
                inverterData.inverterOutpuPowerL3 = channel.readInt16(packetBuffer, 635 - 598);
                //inverterData.inverterPower = channel.readInt16(packetBuffer, 636 - 598);
                inverterData.loadPower = channel.readInt16(packetBuffer, 653 - 598);
                inverterData.gridPowerL1 = -1 * channel.readInt16(packetBuffer, 622 - 598);
                inverterData.gridPowerL2 = -1 * channel.readInt16(packetBuffer, 623 - 598);
                inverterData.gridPowerL3 = -1 * channel.readInt16(packetBuffer, 624 - 598);
                //inverterData.gridPower = -1 * channel.readInt16(packetBuffer, 625 - 598); 
            }))
            return;

        channel.tryReadWithRetries(541, 1, sn, packetBuffer, [&]()
                                   { inverterData.inverterTemperature = (channel.readInt16(packetBuffer, 0) - 1000) / 10; });

        if (!channel.tryReadWithRetries(514, 535 - 514 + 2, sn, packetBuffer, [&]()
                                        {
                                            inverterData.pvToday = channel.readUInt16(packetBuffer, 529 - 514) / 10.0f;
                inverterData.pvTotal = channel.readUInt16(packetBuffer, 534 - 514) / 10.0f;
                inverterData.loadToday = channel.readUInt16(packetBuffer, 526 - 514) / 10.0f;
                inverterData.loadTotal = channel.readUInt32(packetBuffer, 527 - 514) / 10.0f;
                inverterData.batteryChargedToday = channel.readUInt16(packetBuffer, 514 - 514) / 10.0f;
                inverterData.batteryDischargedToday = channel.readUInt16(packetBuffer, 515 - 514) / 10.0f;
                inverterData.gridBuyToday = channel.readUInt16(packetBuffer, 520 - 514) / 10.0f;
                inverterData.gridBuyTotal = channel.readUInt32(packetBuffer, 522 - 514) / 10.0f;
                inverterData.gridSellToday = channel.readUInt16(packetBuffer, 521 - 514) / 10.0f;
                inverterData.gridSellTotal = channel.readUInt32(packetBuffer, 524 - 514) / 10.0f; }))
            return;
    }

    /**
     * Sets the work mode of the Deye inverter for intelligent battery control
     * 
     * Deye Work Modes (register 142):
     * 0 = Selling First (Self-Use)
     * 1 = Zero Export to Load (Hold Battery)
     * 2 = Zero Export to CT
     * 3 = Time of Use
     * 
     * For charge/discharge control, Deye uses Time of Use mode with:
     * - Register 148-149: Grid Charge enabled and power
     * - Register 166-167: Gen Charge settings
     * - SOC limits in registers 143-147
     * 
     * @param ipAddress IP address of the dongle
     * @param dongleSN Serial number of the dongle
     * @param mode Desired inverter mode
     * @return true if mode was set successfully
     */
    bool setWorkMode(const String& ipAddress, const String& dongleSN, SolarInverterMode_t mode)
    {
        uint32_t sn = strtoul(dongleSN.c_str(), NULL, 10);
        if (sn == 0)
        {
            LOGD("Invalid dongle SN for setWorkMode");
            return false;
        }

        channel.ensureIPAddress(ipAddress);
        LOGD("Setting Deye work mode to %d", mode);

        bool success = false;

        switch (mode)
        {
        case SI_MODE_SELF_USE:
            // Selling First mode - work_mode = 0
            success = writeRegister(sn, DEYE_REG_WORK_MODE, DEYE_MODE_SELLING_FIRST);
            break;

        case SI_MODE_HOLD_BATTERY:
            // Zero Export to Load - work_mode = 1 (battery won't discharge to grid)
            success = writeRegister(sn, DEYE_REG_WORK_MODE, DEYE_MODE_ZERO_EXPORT_LOAD);
            break;

        case SI_MODE_CHARGE_FROM_GRID:
            // Enable grid charging via Time of Use mode
            success = setGridCharging(sn, true, 100);  // 100% SOC target
            break;

        case SI_MODE_DISCHARGE_TO_GRID:
            // Selling First with max export
            success = writeRegister(sn, DEYE_REG_WORK_MODE, DEYE_MODE_SELLING_FIRST);
            // Also disable battery SOC protection to allow full discharge
            if (success)
            {
                writeRegister(sn, DEYE_REG_SOC_DISCHARGE_LIMIT, 10);  // Allow discharge to 10%
            }
            break;

        default:
            LOGD("Unknown mode: %d", mode);
            break;
        }

        return success;
    }

    // Overload for compatibility - without dongleSN parameter
    bool setWorkMode(const String& ipAddress, SolarInverterMode_t mode)
    {
        LOGD("setWorkMode called without dongleSN - not supported for Deye");
        return false;
    }

private:
    // Deye Modbus register addresses
    static constexpr uint16_t DEYE_REG_WORK_MODE = 142;           // Work Mode register
    static constexpr uint16_t DEYE_REG_SOC_DISCHARGE_LIMIT = 143; // Min SOC for discharge
    static constexpr uint16_t DEYE_REG_SOC_CHARGE_LIMIT = 144;    // Max SOC for charge
    static constexpr uint16_t DEYE_REG_GRID_CHARGE_ENABLE = 148;  // Grid charge enable
    static constexpr uint16_t DEYE_REG_GRID_CHARGE_CURRENT = 149; // Grid charge current (A)
    
    // Deye Work Mode values
    static constexpr uint16_t DEYE_MODE_SELLING_FIRST = 0;        // Self-Use / Selling First
    static constexpr uint16_t DEYE_MODE_ZERO_EXPORT_LOAD = 1;     // Zero Export to Load
    static constexpr uint16_t DEYE_MODE_ZERO_EXPORT_CT = 2;       // Zero Export to CT
    static constexpr uint16_t DEYE_MODE_TIME_OF_USE = 3;          // Time of Use

    /**
     * Write a single register using Solarman V5 protocol
     */
    bool writeRegister(uint32_t sn, uint16_t addr, uint16_t value)
    {
        LOGD("Writing Deye register %d = %d", addr, value);
        
        if (!channel.connect(channel.ip))
        {
            LOGD("Failed to connect for write");
            return false;
        }
        
        bool success = channel.writeSingleRegister(addr, value, sn);
        channel.disconnect();
        return success;
    }

    /**
     * Enable or disable grid charging
     */
    bool setGridCharging(uint32_t sn, bool enable, uint8_t targetSoc)
    {
        LOGD("Setting grid charge: enable=%d, targetSoc=%d", enable, targetSoc);
        
        // First set Time of Use mode
        if (!writeRegister(sn, DEYE_REG_WORK_MODE, DEYE_MODE_TIME_OF_USE))
        {
            return false;
        }
        
        // Enable/disable grid charging
        if (!writeRegister(sn, DEYE_REG_GRID_CHARGE_ENABLE, enable ? 1 : 0))
        {
            return false;
        }
        
        // Set SOC limit
        if (enable)
        {
            writeRegister(sn, DEYE_REG_SOC_CHARGE_LIMIT, targetSoc);
        }
        
        return true;
    }
};