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
    bool supportsIntelligence() { return false; }

    InverterData_t loadData(String ipAddress)
    {
        return readData(ipAddress);
    }

private:
    ModbusRTU rtuChannel;
    //ModbusTCP tcpChannel;
    IPAddress ip = IPAddress(0, 0, 0, 0); // default IP address
    double gridBuyTotal = 0;
    double gridSellTotal = 0;
    int day = -1;
    const int RETRY_COUNT = 5;
    ModbusResponse sendSNDataRequestPacket(IPAddress ip)
    {
        // if (tcpChannel.isConnected())
        // {
        //     return tcpChannel.sendModbusRequest(0xF7, 0x03, 35003, 8);
        // }
        return rtuChannel.sendDataRequest(ip, 8899, 35003, 8);
    }

    ModbusResponse sendRunningDataRequestPacket(IPAddress ip)
    {
        // if (tcpChannel.isConnected())
        // {
        //     return tcpChannel.sendModbusRequest(0xF7, 0x03, 35100, 125);
        // }
        return rtuChannel.sendDataRequest(ip, 8899, 35100, 125);
    }

    ModbusResponse sendBMSInfoRequestPacket(IPAddress ip)
    {
        // if (tcpChannel.isConnected())
        // {
        //     return tcpChannel.sendModbusRequest(0xF7, 0x03, 37000, 8);
        // }
        return rtuChannel.sendDataRequest(ip, 8899, 37000, 8);
    }

    ModbusResponse sendSmartMeterRequestPacket(IPAddress ip)
    {
        // if (tcpChannel.isConnected())
        // {
        //     return tcpChannel.sendModbusRequest(0xF7, 0x03, 36000, 44);
        // }
        return rtuChannel.sendDataRequest(ip, 8899, 36000, 44);
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
        if (/*tcpChannel.connect(ip, 502) || */rtuChannel.connect())
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
                    // _ac = readsw(40);
                    inverterData.L1Power = response.readInt16(35100 + 25) + response.readInt16(35100 + 50);
                    inverterData.L2Power = response.readInt16(35100 + 30) + response.readInt16(35100 + 56);
                    inverterData.L3Power = response.readInt16(35100 + 35) + response.readInt16(35100 + 62);
                    inverterData.loadPower = inverterData.L1Power + inverterData.L2Power + inverterData.L3Power;

                    inverterData.inverterTemperature = response.readInt16(35100 + 74) / 10;
                    inverterData.pvTotal = response.readUInt32(35100 + 91) / 10.0;
                    inverterData.pvToday = response.readUInt32(35100 + 93) / 10.0;
                    inverterData.loadToday = response.readUInt16(35100 + 105) / 10.0;
                    inverterData.loadTotal = response.readUInt32(35100 + 103) / 10.0;
                    inverterData.batteryChargedToday = response.readUInt16(35100 + 108) / 10.0;
                    inverterData.batteryDischargedToday = response.readUInt16(35100 + 111) / 10.0;
                   
                    // this is a hack - Goodwe returns incorrect day values for grid sell/buy
                    // so count it manually from total values
                    int day = (response.readUInt16(35100 + 1) >> 8) & 0xFF;
                    log_d("Day: %d", day);
                    if (this->day != day)
                    {
                        log_d("Day changed, resetting counters");
                        this->day = day;
                        gridBuyTotal = 0;
                        gridSellTotal = 0;
                    }

                    // Read RTC time from registers 35100+0 and 35100+1
                    // Format: Register 0: (Year-2000) << 8 | Month, Register 1: Day << 8 | Hour
                    // Register 2: Minute << 8 | Second (but we don't have it in this response)
                    uint16_t reg0 = response.readUInt16(35100 + 0);
                    uint16_t reg1 = response.readUInt16(35100 + 1);
                    struct tm timeinfo = {};
                    timeinfo.tm_year = ((reg0 >> 8) & 0xFF) + 100; // Year-2000 + 100 = years since 1900
                    timeinfo.tm_mon = (reg0 & 0xFF) - 1;           // Month 1-12 to 0-11
                    timeinfo.tm_mday = (reg1 >> 8) & 0xFF;
                    timeinfo.tm_hour = reg1 & 0xFF;
                    // Note: GoodWe provides minute/second in register 35100+2, but we approximate
                    timeinfo.tm_min = 0;
                    timeinfo.tm_sec = 0;
                    timeinfo.tm_isdst = -1;
                    inverterData.inverterTime = mktime(&timeinfo);
                    log_d("GoodWe RTC: %04d-%02d-%02d %02d:%02d:%02d",
                          timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                          timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

                    break;
                }
                delay(i * 300); // wait before retrying
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
                delay(i * 300); // wait before retrying
            }

            for (int i = 0; i < RETRY_COUNT; i++)
            { // it is UDP so retries are needed
                ModbusResponse response = sendSmartMeterRequestPacket(ip);
                if (response.isValid)
                {
                    inverterData.gridSellTotal = response.readIEEE754(36000 + 15) / 1000.0f;
                    inverterData.gridBuyTotal = response.readIEEE754(36000 + 17) / 1000.0f;

                    // debug logging
                    int register25 = response.readInt32(36000 + 25);
                    log_d("Register 25: %d", register25);
                    int register8 = response.readInt16(36000 + 8);
                    log_d("Register 8: %d", register8);
                    // end debug logging

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
                delay(i * 300); // wait before retrying
            }

            for (int i = 0; i < RETRY_COUNT; i++)
            { // it is UDP so retries are needed
                ModbusResponse response = sendBMSInfoRequestPacket(ip);
                if (response.isValid)
                {

                    inverterData.batteryTemperature = response.readUInt16(37000 + 3) / 10;
                    inverterData.soc = response.readUInt16(37000 + 7);
                    break;
                }
                delay(i * 300); // wait before retrying
            }
        }
        inverterData.hasBattery = inverterData.soc != 0 || inverterData.batteryPower != 0;
        //tcpChannel.disconnect();
        rtuChannel.disconnect();
        logInverterData(inverterData);

        if (inverterData.status != DONGLE_STATUS_OK)
        {
            log_d("Failed to read data from dongle, status: %d", inverterData.status);
            ip = IPAddress(0, 0, 0, 0); // reset IP address to force discovery next time
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
                // On success, the inverter responses with it's IP address (as a text string) followed by it's WiFi AP name.
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
     * For charge/discharge we use Eco Mode with eco_mode_1 register (47515):
     * - eco_mode_1 is 8 bytes: start_h, start_m, end_h, end_m, power_hi, power_lo, on_off_days, soc
     * - For 24/7 operation: 00:00 - 23:59, all days (0x7F)
     * - Power: negative for charge, positive for discharge
     * 
     * @param ipAddress IP address of the dongle
     * @param mode Desired inverter mode
     * @return true if mode was set successfully
     */
    bool setWorkMode(const String& ipAddress, InverterMode_t mode)
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
            // Backup Mode - work_mode = 2
            success = writeWorkModeRegister(targetIp, GOODWE_WORK_MODE_BACKUP);
            break;

        case INVERTER_MODE_CHARGE_FROM_GRID:
            // Eco Mode with 24/7 charging
            success = setEcoModeCharge(targetIp, 100);  // 100% power
            break;

        case INVERTER_MODE_DISCHARGE_TO_GRID:
            // Eco Mode with 24/7 discharging
            success = setEcoModeDischarge(targetIp, 100);  // 100% power
            break;

        default:
            log_d("Unknown mode: %d", mode);
            break;
        }

        rtuChannel.disconnect();
        return success;
    }

private:
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
    
    // Eco Mode registers
    static constexpr uint16_t REG_ECO_MODE_1 = 47515;         // Eco Mode Group 1 (8 bytes = 4 registers)
    static constexpr uint16_t REG_ECO_MODE_1_SWITCH = 47518;  // Eco Mode Group 1 Switch (high byte)
    static constexpr uint16_t REG_ECO_MODE_2_SWITCH = 47522;  // Eco Mode Group 2 Switch
    static constexpr uint16_t REG_ECO_MODE_3_SWITCH = 47526;  // Eco Mode Group 3 Switch
    static constexpr uint16_t REG_ECO_MODE_4_SWITCH = 47530;  // Eco Mode Group 4 Switch

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
     * Set Eco Mode for charging from grid
     * Encodes eco_mode_1 as: 00:00-23:59, all days, negative power (charge)
     * Format: "0000173b{power_neg:04x}ff7f" where power_neg is 2's complement negative
     */
    bool setEcoModeCharge(IPAddress targetIp, int powerPercent)
    {
        log_d("Setting Eco Mode CHARGE at %d%%", powerPercent);
        
        // Encode charge: power is negative in 2's complement
        // Format from Python: bytes.fromhex("0000173b{:04x}ff7f".format((-1 * abs(eco_mode_power)) & (2 ** 16 - 1)))
        int16_t powerValue = -abs(powerPercent);  // Negative for charge
        uint16_t powerEncoded = (uint16_t)powerValue;  // 2's complement
        
        // eco_mode_1: 8 bytes = 4 registers
        // Byte format: start_h(0), start_m(0), end_h(23), end_m(59), power_hi, power_lo, on_off_days(0x7F=all days, on=0xFF), soc(0x7F)
        // From Python encode_charge: "0000173b{power_neg:04x}ff7f"
        // 00 00 = start 00:00
        // 17 3b = end 23:59 (0x17=23, 0x3b=59)
        // power_neg = negative power in 2's complement
        // ff = on_off (0xFF = on, all days = 0x7F in next nibble... actually combined)
        // 7f = day_bits (all 7 days)
        uint8_t ecoModeData[8] = {
            0x00, 0x00,  // Start time: 00:00
            0x17, 0x3b,  // End time: 23:59
            (uint8_t)(powerEncoded >> 8),   // Power high byte
            (uint8_t)(powerEncoded & 0xFF), // Power low byte
            0xFF,        // On/Off + SoC high nibble (enabled)
            0x7F         // Day bits: all 7 days (Mon-Sun)
        };
        
        return writeEcoModeAndActivate(targetIp, ecoModeData);
    }

    /**
     * Set Eco Mode for discharging to grid
     * Encodes eco_mode_1 as: 00:00-23:59, all days, positive power (discharge)
     */
    bool setEcoModeDischarge(IPAddress targetIp, int powerPercent)
    {
        log_d("Setting Eco Mode DISCHARGE at %d%%", powerPercent);
        
        // Encode discharge: power is positive
        uint16_t powerEncoded = abs(powerPercent);
        
        uint8_t ecoModeData[8] = {
            0x00, 0x00,  // Start time: 00:00
            0x17, 0x3b,  // End time: 23:59
            (uint8_t)(powerEncoded >> 8),   // Power high byte
            (uint8_t)(powerEncoded & 0xFF), // Power low byte
            0xFF,        // On/Off + SoC high nibble (enabled)
            0x7F         // Day bits: all 7 days
        };
        
        return writeEcoModeAndActivate(targetIp, ecoModeData);
    }

    /**
     * Write eco_mode_1 data and switch to Eco Mode
     */
    bool writeEcoModeAndActivate(IPAddress targetIp, const uint8_t* ecoModeData)
    {
        // Log the eco mode data we're writing
        String dataHex = "";
        for (int i = 0; i < 8; i++) {
            char buf[4];
            snprintf(buf, sizeof(buf), "%02X ", ecoModeData[i]);
            dataHex += buf;
        }
        log_d("Writing eco_mode_1 data: %s", dataHex.c_str());
        
        // Step 1: Write eco_mode_1 (4 registers = 8 bytes) at register 47515
        bool success = false;
        for (int retry = 0; retry < RETRY_COUNT; retry++)
        {
            if (rtuChannel.writeMultipleRegisters(targetIp, GOODWE_UDP_PORT, GOODWE_UNIT_ID, 
                                                   REG_ECO_MODE_1, ecoModeData, 8))
            {
                success = true;
                break;
            }
            delay(retry * 200);
        }
        
        if (!success)
        {
            log_d("Failed to write eco_mode_1");
            return false;
        }
        
        // Step 2: Disable other eco mode groups (2, 3, 4)
        // eco_mode_X_switch is the high byte of the register, so we write 0 to disable
        for (int retry = 0; retry < RETRY_COUNT; retry++)
        {
            if (rtuChannel.writeSingleRegister(targetIp, GOODWE_UDP_PORT, GOODWE_UNIT_ID, REG_ECO_MODE_2_SWITCH, 0))
                break;
            delay(retry * 100);
        }
        for (int retry = 0; retry < RETRY_COUNT; retry++)
        {
            if (rtuChannel.writeSingleRegister(targetIp, GOODWE_UDP_PORT, GOODWE_UNIT_ID, REG_ECO_MODE_3_SWITCH, 0))
                break;
            delay(retry * 100);
        }
        for (int retry = 0; retry < RETRY_COUNT; retry++)
        {
            if (rtuChannel.writeSingleRegister(targetIp, GOODWE_UDP_PORT, GOODWE_UNIT_ID, REG_ECO_MODE_4_SWITCH, 0))
                break;
            delay(retry * 100);
        }
        
        // Step 3: Set work_mode to Eco (3)
        success = writeWorkModeRegister(targetIp, GOODWE_WORK_MODE_ECO);
        if (!success)
        {
            log_d("Failed to set work_mode to Eco");
            return false;
        }
        
        log_d("Eco mode activated successfully");
        return true;
    }
};