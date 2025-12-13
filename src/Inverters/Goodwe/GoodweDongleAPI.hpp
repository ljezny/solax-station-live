#pragma once

#include <Arduino.h>
#include <algorithm>
#include "../../Protocol/ModbusRTU.hpp"
#include "../../Protocol/ModbusTCP.hpp"
#include "Inverters/InverterResult.hpp"

class GoodweDongleAPI
{
public:
    /**
     * Returns true if this inverter supports intelligence mode control
     */
    bool supportsIntelligence() { return true; }

    InverterData_t loadData(String ipAddress)
    {
        return readData(ipAddress);
    }

    /**
     * Sets the work mode of the GoodWe inverter for intelligent battery control
     * 
     * GoodWe Work Modes (register 47000):
     * 0 = General Mode (Self-Use)
     * 1 = Off-Grid Mode
     * 2 = Backup Mode (Hold Battery)
     * 3 = Eco Mode (for charge/discharge control via eco_mode_1)
     * 4 = Peak Shaving Mode
     * 5 = Self-Use Mode (745 platform)
     * 
     * For charge/discharge we use Eco Mode V2 with eco_mode_1 register (47547):
     * - eco_mode_1 V2 is 12 bytes: start_h, start_m, end_h, end_m, on_off, day_bits, power(2), soc(2), months(2)
     * - For 24/7 operation: 00:00 - 23:59, all days (0x7F), all months (0x0000)
     * - Power: negative for charge, positive for discharge (in %)
     * - SOC: target SOC for charging, 100 for discharge
     * - Note: V2 requires ARM firmware 19+
     * 
     * @param ipAddress IP address of the dongle
     * @param mode Desired inverter mode
     * @param minSoc Minimum SOC for discharge (used when discharging)
     * @param maxSoc Maximum SOC for charge (target SOC when charging)
     * @return true if mode was set successfully
     */
    bool setWorkMode(const String& ipAddress, InverterMode_t mode, int minSoc = 10, int maxSoc = 100)
    {
        IPAddress targetIp;
        if (!ipAddress.isEmpty())
        {
            targetIp.fromString(ipAddress);
        }
        else if (ip != IPAddress(0, 0, 0, 0))
        {
            targetIp = ip;
        }
        else
        {
            log_d("No IP address available for setWorkMode");
            return false;
        }

        log_d("Setting GoodWe work mode to %d at %s", mode, targetIp.toString().c_str());

        if (!rtuChannel.connect())
        {
            log_d("Failed to connect for setWorkMode");
            return false;
        }

        bool success = false;

        switch (mode)
        {
        case INVERTER_MODE_SELF_USE:
            // General Mode - work_mode = 0
            success = writeWorkModeRegister(targetIp, GOODWE_WORK_MODE_GENERAL);
            break;

        case INVERTER_MODE_HOLD_BATTERY:
            // Hold Battery - use Eco Mode V2 with discharge at 1% power (effectively holds battery)
            success = setEcoModeDischarge(targetIp, 1, minSoc);  // 1% power = hold battery
            break;

        case INVERTER_MODE_CHARGE_FROM_GRID:
            // Eco Mode V2 with 24/7 charging, target SOC = maxSoc
            success = setEcoModeCharge(targetIp, 99, maxSoc);  // 100% power, charge to maxSoc
            break;

        case INVERTER_MODE_DISCHARGE_TO_GRID:
            // Eco Mode V2 with 24/7 discharging, stop at minSoc
            success = setEcoModeDischarge(targetIp, 99, minSoc);  // 100% power, discharge to minSoc
            break;

        default:
            log_d("Unknown mode: %d", mode);
            break;
        }

        rtuChannel.disconnect();
        return success;
    }

private:
    ModbusRTU rtuChannel;
    //ModbusTCP tcpChannel;
    IPAddress ip = IPAddress(0, 0, 0, 0); // default IP address
    double gridBuyTotal = 0;
    double gridSellTotal = 0;
    int day = -1;
    const int RETRY_COUNT = 5;
    
    // GoodWe Modbus constants
    static constexpr uint8_t GOODWE_UNIT_ID = 0xF7;           // GoodWe uses unit ID 247
    static constexpr int GOODWE_UDP_PORT = 8899;              // UDP port for Modbus RTU
    
    // GoodWe Work Mode register and values
    static constexpr uint16_t REG_WORK_MODE = 47000;          // Work Mode register
    static constexpr uint16_t GOODWE_WORK_MODE_GENERAL = 0;   // General/Self-Use mode
    static constexpr uint16_t GOODWE_WORK_MODE_OFFGRID = 1;   // Off-Grid mode
    static constexpr uint16_t GOODWE_WORK_MODE_BACKUP = 2;    // Backup mode
    static constexpr uint16_t GOODWE_WORK_MODE_ECO = 3;       // Eco mode
    static constexpr uint16_t GOODWE_WORK_MODE_PEAKSHAVING = 4; // Peak Shaving mode
    static constexpr uint16_t GOODWE_WORK_MODE_SELFUSE = 5;   // Self-Use mode (745 platform)
    
    // Eco Mode V1 registers (8 bytes = 4 registers per group) - older firmware
    static constexpr uint16_t REG_ECO_MODE_V1_1 = 47515;      // Eco Mode V1 Group 1 start
    static constexpr uint16_t REG_ECO_MODE_V1_2 = 47519;      // Eco Mode V1 Group 2 start
    static constexpr uint16_t REG_ECO_MODE_V1_3 = 47523;      // Eco Mode V1 Group 3 start
    static constexpr uint16_t REG_ECO_MODE_V1_4 = 47527;      // Eco Mode V1 Group 4 start
    static constexpr uint16_t REG_ECO_MODE_V1_REGS = 4;       // V1 uses 4 registers (8 bytes)
    
    // Eco Mode V2 registers (12 bytes = 6 registers per group) - ARM firmware 19+
    static constexpr uint16_t REG_ECO_MODE_V2_1 = 47547;      // Eco Mode V2 Group 1 start
    static constexpr uint16_t REG_ECO_MODE_V2_2 = 47553;      // Eco Mode V2 Group 2 start
    static constexpr uint16_t REG_ECO_MODE_V2_3 = 47559;      // Eco Mode V2 Group 3 start
    static constexpr uint16_t REG_ECO_MODE_V2_4 = 47565;      // Eco Mode V2 Group 4 start
    static constexpr uint16_t REG_ECO_MODE_V2_REGS = 6;       // V2 uses 6 registers (12 bytes)

    ModbusResponse sendSNDataRequestPacket(IPAddress ip)
    {
        return rtuChannel.sendDataRequest(ip, 8899, 35003, 8);
    }

    ModbusResponse sendRunningDataRequestPacket(IPAddress ip)
    {
        return rtuChannel.sendDataRequest(ip, 8899, 35100, 125);
    }

    ModbusResponse sendBMSInfoRequestPacket(IPAddress ip)
    {
        return rtuChannel.sendDataRequest(ip, 8899, 37000, 8);
    }

    ModbusResponse sendWorkModeRequestPacket(IPAddress ip)
    {
        // Read work_mode register (47000) - single register, holding register uses 0x03 (default)
        return rtuChannel.sendDataRequest(ip, 8899, 47000, 1);
    }

    ModbusResponse sendEcoMode1RequestPacket(IPAddress ip)
    {
        // Read eco_mode_1 V2 registers (47547-47552) - 6 registers = 12 bytes
        // Format V2: start_h, start_m, end_h, end_m, on_off, day_bits, power_hi, power_lo, soc_hi, soc_lo, month_hi, month_lo
        return rtuChannel.sendDataRequest(ip, 8899, REG_ECO_MODE_V2_1, REG_ECO_MODE_V2_REGS);
    }

    ModbusResponse sendSmartMeterRequestPacket(IPAddress ip)
    {
        return rtuChannel.sendDataRequest(ip, 8899, 36000, 44);
    }

    /**
     * Converts GoodWe work_mode register value to InverterMode_t
     */
    InverterMode_t goodweModeToInverterMode(uint16_t goodweMode, int16_t ecoModePower = 0, bool ecoModeEnabled = false)
    {
        switch (goodweMode)
        {
        case GOODWE_WORK_MODE_GENERAL:
        case GOODWE_WORK_MODE_OFFGRID:
        case GOODWE_WORK_MODE_PEAKSHAVING:
        case GOODWE_WORK_MODE_SELFUSE:
            return INVERTER_MODE_SELF_USE;
        
        case GOODWE_WORK_MODE_BACKUP:
            return INVERTER_MODE_HOLD_BATTERY;
        
        case GOODWE_WORK_MODE_ECO:
            // Eco mode - determine charge/discharge from eco_mode_1 power value
            if (!ecoModeEnabled || ecoModePower == 0)
            {
                return INVERTER_MODE_SELF_USE;
            }
            else if (ecoModePower < 0)
            {
                // Negative power = charging from grid
                return INVERTER_MODE_CHARGE_FROM_GRID;
            }
            else if (ecoModePower <= 1)
            {
                // Power = 1% = hold battery mode
                return INVERTER_MODE_HOLD_BATTERY;
            }
            else
            {
                // Positive power > 1 = discharging to grid
                return INVERTER_MODE_DISCHARGE_TO_GRID;
            }
        
        default:
            log_d("Unknown GoodWe work_mode: %d", goodweMode);
            return INVERTER_MODE_UNKNOWN;
        }
    }

    /**
     * Write to the work_mode register (47000)
     */
    bool writeWorkModeRegister(IPAddress targetIp, uint16_t workMode)
    {
        log_d("Writing work_mode register: %d", workMode);
        for (int retry = 0; retry < RETRY_COUNT; retry++)
        {
            if (rtuChannel.writeSingleRegister(targetIp, GOODWE_UDP_PORT, GOODWE_UNIT_ID, REG_WORK_MODE, workMode))
            {
                return true;
            }
            delay(retry * 200);
        }
        log_d("Failed to write work_mode register after %d retries", RETRY_COUNT);
        return false;
    }

    /**
     * Set Eco Mode V2 for charging from grid
     * @param targetIp IP address of the inverter
     * @param powerPercent Charging power percentage (0-100)
     * @param targetSoc Target SOC percentage (0-100)
     */
    bool setEcoModeCharge(IPAddress targetIp, int powerPercent, int targetSoc)
    {
        log_d("Setting Eco Mode V2 CHARGE at %d%%, target SOC=%d%%", powerPercent, targetSoc);
        
        // Power is negative for charge, encoded as signed int16
        int16_t powerValue = -abs(powerPercent);  // Negative for charge
        uint16_t powerEncoded = (uint16_t)powerValue;  // 2's complement
        
        // EcoModeV2 format (12 bytes = 6 registers):
        // Byte 0: start_h, Byte 1: start_m
        // Byte 2: end_h, Byte 3: end_m
        // Byte 4: on_off (0xFF = 255-0 = ECO_MODE enabled), Byte 5: day_bits (0x7F = all days)
        // Byte 6-7: power (signed int16, negative=charge)
        // Byte 8-9: SOC (uint16)
        // Byte 10-11: month_bits (0x0000 = all months)
        // Python: "0000173b{:02x}7f{:04x}{:04x}{:04x}".format(255-schedule_type, power, soc, months)
        uint8_t ecoModeData[12] = {
            0x00, 0x00,  // Start time: 00:00
            0x17, 0x3B,  // End time: 23:59
            0xFF,        // on_off: 255 - ECO_MODE(0) = 0xFF (enabled)
            0x7F,        // day_bits: all 7 days (Sun-Sat)
            (uint8_t)(powerEncoded >> 8),   // Power high byte
            (uint8_t)(powerEncoded & 0xFF), // Power low byte
            (uint8_t)(targetSoc >> 8),      // SOC high byte
            (uint8_t)(targetSoc & 0xFF),    // SOC low byte
            0x00, 0x00   // month_bits: 0 = all months
        };
        
        return writeEcoModeV2AndActivate(targetIp, ecoModeData);
    }

    /**
     * Set Eco Mode V2 for discharging to grid
     * @param targetIp IP address of the inverter
     * @param powerPercent Discharging power percentage (0-100)
     * @param minSoc Minimum SOC percentage to discharge to (0-100)
     */
    bool setEcoModeDischarge(IPAddress targetIp, int powerPercent, int minSoc)
    {
        log_d("Setting Eco Mode V2 DISCHARGE at %d%%, min SOC=%d%%", powerPercent, minSoc);
        
        // Power is positive for discharge
        uint16_t powerEncoded = abs(powerPercent);
        
        // For discharge, SOC is set to 100 (0x0064) as target, but minSoc is the floor
        // Actually Python uses: soc=100 fixed for discharge (0x0064)
        uint16_t socValue = 100;  // Python uses 0x0064 = 100 for discharge
        
        // EcoModeV2 format (12 bytes = 6 registers):
        uint8_t ecoModeData[12] = {
            0x00, 0x00,  // Start time: 00:00
            0x17, 0x3B,  // End time: 23:59
            0xFF,        // on_off: 255 - ECO_MODE(0) = 0xFF (enabled)
            0x7F,        // day_bits: all 7 days (Sun-Sat)
            (uint8_t)(powerEncoded >> 8),   // Power high byte
            (uint8_t)(powerEncoded & 0xFF), // Power low byte
            (uint8_t)(socValue >> 8),       // SOC high byte (0x00)
            (uint8_t)(socValue & 0xFF),     // SOC low byte (0x64 = 100)
            0x00, 0x00   // month_bits: 0 = all months
        };
        
        return writeEcoModeV2AndActivate(targetIp, ecoModeData);
    }

    /**
     * Write eco_mode_1 V2 data (12 bytes) at register 47547 and switch to Eco Mode
     */
    bool writeEcoModeV2AndActivate(IPAddress targetIp, const uint8_t* ecoModeData)
    {
        // Log the eco mode data we're writing (12 bytes for V2)
        String dataHex = "";
        for (int i = 0; i < 12; i++) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%02X ", ecoModeData[i]);
            dataHex += buf;
        }
        log_d("Writing eco_mode_1 V2 data at register %d: %s", REG_ECO_MODE_V2_1, dataHex.c_str());
        
        // Step 1: Write eco_mode_1 V2 (6 registers = 12 bytes) at register 47547
        bool success = false;
        for (int retry = 0; retry < RETRY_COUNT; retry++)
        {
            if (rtuChannel.writeMultipleRegisters(targetIp, GOODWE_UDP_PORT, GOODWE_UNIT_ID, 
                                                   REG_ECO_MODE_V2_1, ecoModeData, 12))
            {
                success = true;
                break;
            }
            delay(retry * 200);
        }
        
        if (!success)
        {
            log_d("Failed to write eco_mode_1 V2");
            return false;
        }
        
        // Step 2: Disable other eco mode V2 groups (2, 3, 4) by writing disabled schedule
        // V2 disabled format: "30003000{schedule_type}00{power}00640000"
        // For ECO_MODE (0): "300030000000006400640000"
        uint8_t disabledGroupV2[12] = {
            0x30, 0x00,  // start: 48:00 (invalid)
            0x30, 0x00,  // end: 48:00 (invalid)
            0x00,        // on_off=0 (disabled)
            0x00,        // day_bits=0
            0x00, 0x64,  // power=100 (0x0064)
            0x00, 0x64,  // soc=100 (0x0064)
            0x00, 0x00   // month_bits=0
        };
        
        for (int retry = 0; retry < RETRY_COUNT; retry++)
        {
            if (rtuChannel.writeMultipleRegisters(targetIp, GOODWE_UDP_PORT, GOODWE_UNIT_ID, REG_ECO_MODE_V2_2, disabledGroupV2, 12))
                break;
            delay(retry * 100);
        }
        for (int retry = 0; retry < RETRY_COUNT; retry++)
        {
            if (rtuChannel.writeMultipleRegisters(targetIp, GOODWE_UDP_PORT, GOODWE_UNIT_ID, REG_ECO_MODE_V2_3, disabledGroupV2, 12))
                break;
            delay(retry * 100);
        }
        for (int retry = 0; retry < RETRY_COUNT; retry++)
        {
            if (rtuChannel.writeMultipleRegisters(targetIp, GOODWE_UDP_PORT, GOODWE_UNIT_ID, REG_ECO_MODE_V2_4, disabledGroupV2, 12))
                break;
            delay(retry * 100);
        }
        
        // Step 3: Set work_mode to Eco (3)
        bool workModeSuccess = writeWorkModeRegister(targetIp, GOODWE_WORK_MODE_ECO);
        if (!workModeSuccess)
        {
            log_d("Failed to set work_mode to Eco");
            return false;
        }
        
        log_d("Eco mode V2 activated successfully");
        return true;
    }

    InverterData_t readData(String ipAddress)
    {
        if (ip == IPAddress(0, 0, 0, 0))
        {
            if (!ipAddress.isEmpty())
            {
                ip.fromString(ipAddress);
            }

            if (ip == IPAddress(0, 0, 0, 0))
            {
                ip = discoverDongleIP();
                if (ip == IPAddress(0, 0, 0, 0))
                {
                    ip = IPAddress(10, 10, 100, 253);
                }
            }
        }

        InverterData_t inverterData{};
        log_d("Connecting to dongle...%s", ip.toString().c_str());
        if (rtuChannel.connect())
        {
            log_d("Connected.");

            for (int i = 0; i < RETRY_COUNT; i++)
            {
                ModbusResponse response = sendRunningDataRequestPacket(ip);
                if (response.isValid)
                {
                    inverterData.status = DONGLE_STATUS_OK;
                    inverterData.pv1Power = response.readUInt32(35100 + 5);
                    inverterData.pv2Power = response.readUInt32(35100 + 9);
                    inverterData.pv3Power = response.readUInt32(35100 + 13);
                    inverterData.pv4Power = response.readUInt32(35100 + 17);

                    inverterData.batteryPower -= response.readInt16(35100 + 83); // TODO: maybe sign readuw(84);
                    
                    // Inverter output power per phase (pgrid L1-L3: registers 35125, 35130, 35135)
                    inverterData.inverterOutpuPowerL1 = response.readInt16(35100 + 25); // pgrid - On-grid L1 Power
                    inverterData.inverterOutpuPowerL2 = response.readInt16(35100 + 30); // pgrid2 - On-grid L2 Power
                    inverterData.inverterOutpuPowerL3 = response.readInt16(35100 + 35); // pgrid3 - On-grid L3 Power
                    
                    // Load power = on-grid load (35172) + backup load (35170)
                    // Register 35172 (offset 72) = load_ptotal (on-grid load, backup NOT included)
                    // Register 35170 (offset 70) = backup_ptotal (backup/EPS load)
                    int loadOnGrid = response.readInt16(35100 + 72);   // load_ptotal - on-grid load without backup
                    int loadBackup = response.readInt16(35100 + 70);   // backup_ptotal - backup/EPS load
                    //inverterData.loadPower = loadOnGrid /*+ loadBackup*/;  // total house consumption
                    inverterData.loadPower = inverterData.pv1Power + inverterData.pv2Power + inverterData.pv3Power + inverterData.pv4Power - inverterData.batteryPower;

                    inverterData.inverterTemperature = response.readInt16(35100 + 74) / 10;
                    inverterData.pvTotal = response.readUInt32(35100 + 91) / 10.0;
                    inverterData.pvToday = response.readUInt32(35100 + 93) / 10.0;
                    inverterData.loadToday = response.readUInt16(35100 + 105) / 10.0;
                    inverterData.loadTotal = response.readUInt32(35100 + 103) / 10.0;
                    inverterData.batteryChargedToday = response.readUInt16(35100 + 108) / 10.0;
                    inverterData.batteryDischargedToday = response.readUInt16(35100 + 111) / 10.0;
                   
                    int day = (response.readUInt16(35100 + 1) >> 8) & 0xFF;
                    log_d("Day: %d", day);
                    if (this->day != day)
                    {
                        log_d("Day changed, resetting counters");
                        this->day = day;
                        gridBuyTotal = 0;
                        gridSellTotal = 0;
                    }

                    // Read RTC time
                    uint16_t reg0 = response.readUInt16(35100 + 0);
                    uint16_t reg1 = response.readUInt16(35100 + 1);
                    struct tm timeinfo = {};
                    timeinfo.tm_year = ((reg0 >> 8) & 0xFF) + 100;
                    timeinfo.tm_mon = (reg0 & 0xFF) - 1;
                    timeinfo.tm_mday = (reg1 >> 8) & 0xFF;
                    timeinfo.tm_hour = reg1 & 0xFF;
                    timeinfo.tm_min = 0;
                    timeinfo.tm_sec = 0;
                    timeinfo.tm_isdst = -1;
                    inverterData.inverterTime = mktime(&timeinfo);
                    log_d("GoodWe RTC: %04d-%02d-%02d %02d:%02d:%02d",
                          timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                          timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

                    break;
                }
                delay(i * 300);
            }

            for (int i = 0; i < RETRY_COUNT; i++)
            {
                ModbusResponse response = sendSNDataRequestPacket(ip);
                if (response.isValid)
                {
                    inverterData.sn = response.readString(35003, 8);
                    log_d("Dongle SN: %s", inverterData.sn.c_str());
                    break;
                }
                delay(i * 300);
            }

            for (int i = 0; i < RETRY_COUNT; i++)
            {
                ModbusResponse response = sendSmartMeterRequestPacket(ip);
                if (response.isValid)
                {
                    inverterData.gridSellTotal = response.readIEEE754(36000 + 15) / 1000.0f;
                    inverterData.gridBuyTotal = response.readIEEE754(36000 + 17) / 1000.0f;

                    inverterData.gridPowerL1 = response.readInt32(36000 + 19);
                    inverterData.gridPowerL2 = response.readInt32(36000 + 21);
                    inverterData.gridPowerL3 = response.readInt32(36000 + 23);
                    
                    inverterData.loadPower -= (inverterData.gridPowerL1 + inverterData.gridPowerL2 + inverterData.gridPowerL3);
                    
                    log_d("Grid sell total: %f", inverterData.gridSellTotal);
                    log_d("Grid buy total: %f", inverterData.gridBuyTotal);
                    if (gridBuyTotal == 0)
                    {
                        gridBuyTotal = inverterData.gridBuyTotal;
                    }
                    if (gridSellTotal == 0)
                    {
                        gridSellTotal = inverterData.gridSellTotal;
                    }
                    inverterData.gridSellToday = inverterData.gridSellTotal - gridSellTotal;
                    inverterData.gridBuyToday = inverterData.gridBuyTotal - gridBuyTotal;
                    break;
                }
                delay(i * 300);
            }

            for (int i = 0; i < RETRY_COUNT; i++)
            {
                ModbusResponse response = sendBMSInfoRequestPacket(ip);
                if (response.isValid)
                {
                    inverterData.batteryTemperature = response.readUInt16(37000 + 3) / 10;
                    inverterData.soc = response.readUInt16(37000 + 7);
                    break;
                }
                delay(i * 300);
            }

            // Read work mode (for intelligence support)
            uint16_t workMode = 0;
            int16_t ecoModePower = 0;
            bool ecoModeEnabled = false;
            
            for (int i = 0; i < RETRY_COUNT; i++)
            {
                ModbusResponse response = sendWorkModeRequestPacket(ip);
                if (response.isValid)
                {
                    workMode = response.readUInt16(47000);
                    log_d("GoodWe work_mode: %d", workMode);
                    break;
                }
                delay(i * 300);
            }
            
            // If Eco mode, read eco_mode_1 V2 to determine charge/discharge
            if (workMode == GOODWE_WORK_MODE_ECO)
            {
                for (int i = 0; i < RETRY_COUNT; i++)
                {
                    ModbusResponse response = sendEcoMode1RequestPacket(ip);
                    if (response.isValid && response.length >= REG_ECO_MODE_V2_REGS)
                    {
                        // V2 format (12 bytes):
                        // Bytes 0-3: start_h, start_m, end_h, end_m
                        // Byte 4: on_off (0xFF = enabled for ECO_MODE)
                        // Byte 5: day_bits (0x7F = all days)
                        // Bytes 6-7: power (signed int16, negative=charge, positive=discharge)
                        // Bytes 8-9: SOC (uint16)
                        // Bytes 10-11: month_bits
                        int8_t onOff = (int8_t)response.data[4];  // Byte 4 = on_off
                        ecoModePower = (int16_t)((response.data[6] << 8) | response.data[7]);  // Bytes 6-7 = power
                        uint16_t soc = (response.data[8] << 8) | response.data[9];  // Bytes 8-9 = SOC
                        ecoModeEnabled = (onOff != 0 && onOff != 85);  // 0xFF/-1 = enabled, 0 = disabled, 85 = NOT_SET
                        log_d("GoodWe eco_mode_1 V2: power=%d, on_off=%d, soc=%d, enabled=%d", ecoModePower, onOff, soc, ecoModeEnabled);
                        break;
                    }
                    delay(i * 300);
                }
            }
            
            inverterData.inverterMode = goodweModeToInverterMode(workMode, ecoModePower, ecoModeEnabled);
            log_d("GoodWe InverterMode: %d", inverterData.inverterMode);
        }
        inverterData.hasBattery = inverterData.soc != 0 || inverterData.batteryPower != 0;
        rtuChannel.disconnect();
        logInverterData(inverterData);

        if (inverterData.status != DONGLE_STATUS_OK)
        {
            log_d("Failed to read data from dongle, status: %d", inverterData.status);
            ip = IPAddress(0, 0, 0, 0);
        }

        return inverterData;
    }

    IPAddress discoverDongleIP()
    {
        IPAddress dongleIP;
        WiFiUDP udp;
        String message = "WIFIKIT-214028-READ";
        udp.beginPacket(IPAddress(255, 255, 255, 255), 48899);
        udp.write((const uint8_t *)message.c_str(), (size_t)message.length());
        udp.endPacket();

        unsigned long start = millis();
        while (millis() - start < 3000)
        {
            int packetSize = udp.parsePacket();
            if (packetSize)
            {
                char d[128] = {0};
                udp.read(d, sizeof(d));

                log_d("Received IP address: %s", String(d).c_str());
                int indexOfComma = String(d).indexOf(',');
                String ip = String(d).substring(0, indexOfComma);
                log_d("Parsed IP address: %s", ip.c_str());
                dongleIP.fromString(ip);
                log_d("Dongle IP: %s", dongleIP.toString());
                break;
            }
        }
        udp.stop();
        return dongleIP;
    }
};
