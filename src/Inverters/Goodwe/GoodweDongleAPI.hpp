#pragma once

#include <Arduino.h>
#include <RemoteLogger.hpp>
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
        // Set IP address
        if (!ipAddress.isEmpty())
        {
            IPAddress newIp;
            if (newIp.fromString(ipAddress))
            {
                ip = newIp;
            }
        }
        
        if (ip == IPAddress(0, 0, 0, 0))
        {
            LOGD("No IP address available for setWorkMode");
            return false;
        }

        return executeWorkModeChange(mode, minSoc, maxSoc);
    }

private:
    /**
     * Execute the actual work mode change using tryWrite methods
     */
    bool executeWorkModeChange(InverterMode_t mode, int minSoc, int maxSoc)
    {
        switch (mode)
        {
        case INVERTER_MODE_SELF_USE:
            return tryWriteSingleRegister(REG_WORK_MODE, GOODWE_WORK_MODE_GENERAL);

        case INVERTER_MODE_HOLD_BATTERY:
            return setEcoModeDischarge(ip, 1, minSoc);

        case INVERTER_MODE_CHARGE_FROM_GRID:
            return setEcoModeCharge(ip, 99, maxSoc);

        case INVERTER_MODE_DISCHARGE_TO_GRID:
            return setEcoModeDischarge(ip, 99, minSoc);

        default:
            return false;
        }
    }

    ModbusRTU rtuChannel;
    ModbusTCP tcpChannel;
    IPAddress ip = IPAddress(0, 0, 0, 0);
    bool preferTcp = false; // prefer TCP after successful TCP connection
    double gridBuyTotal = 0;
    double gridSellTotal = 0;
    int day = -1;
    static constexpr int RETRY_COUNT = 3;
    static constexpr int GOODWE_TCP_PORT = 502;
    static constexpr uint8_t GOODWE_UNIT_ID = 0xF7;
    static constexpr int GOODWE_UDP_PORT = 8899;
    
    // GoodWe Work Mode register and values
    static constexpr uint16_t REG_WORK_MODE = 47000;
    static constexpr uint16_t GOODWE_WORK_MODE_GENERAL = 0;
    static constexpr uint16_t GOODWE_WORK_MODE_OFFGRID = 1;
    static constexpr uint16_t GOODWE_WORK_MODE_BACKUP = 2;
    static constexpr uint16_t GOODWE_WORK_MODE_ECO = 3;
    static constexpr uint16_t GOODWE_WORK_MODE_PEAKSHAVING = 4;
    static constexpr uint16_t GOODWE_WORK_MODE_SELFUSE = 5;
    
    // Eco Mode V2 registers (12 bytes = 6 registers per group)
    static constexpr uint16_t REG_ECO_MODE_V2_1 = 47547;
    static constexpr uint16_t REG_ECO_MODE_V2_2 = 47553;
    static constexpr uint16_t REG_ECO_MODE_V2_3 = 47559;
    static constexpr uint16_t REG_ECO_MODE_V2_4 = 47565;
    static constexpr uint16_t REG_ECO_MODE_V2_REGS = 6;

    // ==================== Core Communication Methods ====================

    /**
     * Try to read registers with retries. First tries UDP, if all retries fail, tries TCP fallback.
     * Similar to SofarSolar's tryReadWithRetries pattern.
     * 
     * @param startAddr Starting register address
     * @param count Number of registers to read
     * @param callback Lambda to process the response data
     * @return true if read was successful
     */
    template <typename Callback>
    bool tryReadWithRetries(uint16_t startAddr, uint8_t count, Callback callback)
    {
        // Try preferred channel first (UDP by default, TCP if preferTcp)
        if (preferTcp)
        {
            // Try TCP first
            if (tryReadTcp(startAddr, count, callback))
            {
                return true;
            }
            // TCP failed, try UDP fallback
            LOGD("TCP read failed, trying UDP fallback");
            if (tryReadUdp(startAddr, count, callback))
            {
                preferTcp = false; // UDP works, reset preference
                return true;
            }
        }
        else
        {
            // Try UDP first (default)
            if (tryReadUdp(startAddr, count, callback))
            {
                return true;
            }
            // UDP failed, try TCP fallback
            LOGD("UDP read failed at 0x%04X, trying TCP fallback", startAddr);
            if (tryReadTcp(startAddr, count, callback))
            {
                LOGD("TCP fallback successful, will prefer TCP next time");
                preferTcp = true;
                return true;
            }
        }
        
        LOGD("All read attempts failed for register 0x%04X", startAddr);
        return false;
    }

    /**
     * Try to read via UDP with retries
     */
    template <typename Callback>
    bool tryReadUdp(uint16_t startAddr, uint8_t count, Callback callback)
    {
        rtuChannel.connect();
        for (int i = 0; i < RETRY_COUNT; i++)
        {
            ModbusResponse response = rtuChannel.sendDataRequest(ip, GOODWE_UDP_PORT, startAddr, count);
            if (response.isValid)
            {
                callback(response);
                rtuChannel.disconnect();
                return true;
            }
            if (i < RETRY_COUNT - 1) delay(100 + i * 100);
        }
        rtuChannel.disconnect();
        return false;
    }

    /**
     * Try to read via TCP with retries
     */
    template <typename Callback>
    bool tryReadTcp(uint16_t startAddr, uint8_t count, Callback callback)
    {
        if (!tcpChannel.connect(ip, GOODWE_TCP_PORT))
        {
            LOGD("TCP connection failed to %s:%d", ip.toString().c_str(), GOODWE_TCP_PORT);
            return false;
        }
        
        for (int i = 0; i < RETRY_COUNT; i++)
        {
            ModbusResponse response = tcpChannel.sendModbusRequest(GOODWE_UNIT_ID, 0x03, startAddr, count);
            if (response.isValid)
            {
                callback(response);
                tcpChannel.disconnect();
                return true;
            }
            if (i < RETRY_COUNT - 1) delay(100 + i * 100);
        }
        tcpChannel.disconnect();
        return false;
    }

    /**
     * Try to write single register with retries and fallback
     */
    bool tryWriteSingleRegister(uint16_t addr, uint16_t value)
    {
        if (preferTcp)
        {
            if (tryWriteSingleTcp(addr, value)) return true;
            LOGD("TCP write failed, trying UDP fallback");
            if (tryWriteSingleUdp(addr, value))
            {
                preferTcp = false;
                return true;
            }
        }
        else
        {
            if (tryWriteSingleUdp(addr, value)) return true;
            LOGD("UDP write failed, trying TCP fallback");
            if (tryWriteSingleTcp(addr, value))
            {
                preferTcp = true;
                return true;
            }
        }
        return false;
    }

    bool tryWriteSingleUdp(uint16_t addr, uint16_t value)
    {
        rtuChannel.connect();
        for (int i = 0; i < RETRY_COUNT; i++)
        {
            if (rtuChannel.writeSingleRegister(ip, GOODWE_UDP_PORT, GOODWE_UNIT_ID, addr, value))
            {
                rtuChannel.disconnect();
                return true;
            }
            if (i < RETRY_COUNT - 1) delay(100 + i * 100);
        }
        rtuChannel.disconnect();
        return false;
    }

    bool tryWriteSingleTcp(uint16_t addr, uint16_t value)
    {
        if (!tcpChannel.connect(ip, GOODWE_TCP_PORT)) return false;
        for (int i = 0; i < RETRY_COUNT; i++)
        {
            if (tcpChannel.writeSingleRegister(GOODWE_UNIT_ID, addr, value))
            {
                tcpChannel.disconnect();
                return true;
            }
            if (i < RETRY_COUNT - 1) delay(100 + i * 100);
        }
        tcpChannel.disconnect();
        return false;
    }

    /**
     * Try to write multiple registers with retries and fallback
     */
    bool tryWriteMultipleRegisters(uint16_t startAddr, const uint8_t* data, uint8_t byteCount)
    {
        if (preferTcp)
        {
            if (tryWriteMultipleTcp(startAddr, data, byteCount)) return true;
            LOGD("TCP write failed, trying UDP fallback");
            if (tryWriteMultipleUdp(startAddr, data, byteCount))
            {
                preferTcp = false;
                return true;
            }
        }
        else
        {
            if (tryWriteMultipleUdp(startAddr, data, byteCount)) return true;
            LOGD("UDP write failed, trying TCP fallback");
            if (tryWriteMultipleTcp(startAddr, data, byteCount))
            {
                preferTcp = true;
                return true;
            }
        }
        return false;
    }

    bool tryWriteMultipleUdp(uint16_t startAddr, const uint8_t* data, uint8_t byteCount)
    {
        rtuChannel.connect();
        for (int i = 0; i < RETRY_COUNT; i++)
        {
            if (rtuChannel.writeMultipleRegisters(ip, GOODWE_UDP_PORT, GOODWE_UNIT_ID, startAddr, data, byteCount))
            {
                rtuChannel.disconnect();
                return true;
            }
            if (i < RETRY_COUNT - 1) delay(100 + i * 100);
        }
        rtuChannel.disconnect();
        return false;
    }

    bool tryWriteMultipleTcp(uint16_t startAddr, const uint8_t* data, uint8_t byteCount)
    {
        if (!tcpChannel.connect(ip, GOODWE_TCP_PORT)) return false;
        
        // Convert uint8_t array to uint16_t array for TCP channel
        uint8_t regCount = byteCount / 2;
        uint16_t values[32];
        for (int i = 0; i < regCount; i++)
        {
            values[i] = (data[i * 2] << 8) | data[i * 2 + 1];
        }
        
        for (int i = 0; i < RETRY_COUNT; i++)
        {
            if (tcpChannel.writeMultipleRegisters(GOODWE_UNIT_ID, startAddr, values, regCount))
            {
                tcpChannel.disconnect();
                return true;
            }
            if (i < RETRY_COUNT - 1) delay(100 + i * 100);
        }
        tcpChannel.disconnect();
        return false;
    }

    // ==================== Mode Conversion ====================

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
            if (!ecoModeEnabled || ecoModePower == 0)
                return INVERTER_MODE_SELF_USE;
            else if (ecoModePower < 0)
                return INVERTER_MODE_CHARGE_FROM_GRID;
            else if (ecoModePower <= 1)
                return INVERTER_MODE_HOLD_BATTERY;
            else
                return INVERTER_MODE_DISCHARGE_TO_GRID;
        
        default:
            return INVERTER_MODE_UNKNOWN;
        }
    }

    // ==================== Work Mode Write Methods ====================

    /**
     * Set Eco Mode V2 for charging from grid
     */
    bool setEcoModeCharge(IPAddress targetIp, int powerPercent, int targetSoc)
    {
        int16_t powerValue = -abs(powerPercent);
        uint16_t powerEncoded = (uint16_t)powerValue;
        
        uint8_t ecoModeData[12] = {
            0x00, 0x00,  // Start time: 00:00
            0x17, 0x3B,  // End time: 23:59
            0xFF, 0x7F,  // on_off=enabled, day_bits=all days
            (uint8_t)(powerEncoded >> 8), (uint8_t)(powerEncoded & 0xFF),
            (uint8_t)(targetSoc >> 8), (uint8_t)(targetSoc & 0xFF),
            0x00, 0x00   // month_bits: all months
        };
        
        return writeEcoModeV2AndActivate(ecoModeData);
    }

    /**
     * Set Eco Mode V2 for discharging to grid
     */
    bool setEcoModeDischarge(IPAddress targetIp, int powerPercent, int minSoc)
    {
        uint16_t powerEncoded = abs(powerPercent);
        uint16_t socValue = 100;
        
        uint8_t ecoModeData[12] = {
            0x00, 0x00,  // Start time: 00:00
            0x17, 0x3B,  // End time: 23:59
            0xFF, 0x7F,  // on_off=enabled, day_bits=all days
            (uint8_t)(powerEncoded >> 8), (uint8_t)(powerEncoded & 0xFF),
            (uint8_t)(socValue >> 8), (uint8_t)(socValue & 0xFF),
            0x00, 0x00   // month_bits: all months
        };
        
        return writeEcoModeV2AndActivate(ecoModeData);
    }

    /**
     * Write eco_mode_1 V2 data and switch to Eco Mode
     */
    bool writeEcoModeV2AndActivate(const uint8_t* ecoModeData)
    {
        // Step 1: Write eco_mode_1 V2
        if (!tryWriteMultipleRegisters(REG_ECO_MODE_V2_1, ecoModeData, 12))
        {
            LOGD("Failed to write eco_mode_1");
            return false;
        }
        
        // Step 2: Disable other eco mode groups
        uint8_t disabledGroup[12] = {
            0x30, 0x00, 0x30, 0x00, 0x00, 0x00,
            0x00, 0x64, 0x00, 0x64, 0x00, 0x00
        };
        tryWriteMultipleRegisters(REG_ECO_MODE_V2_2, disabledGroup, 12);
        tryWriteMultipleRegisters(REG_ECO_MODE_V2_3, disabledGroup, 12);
        tryWriteMultipleRegisters(REG_ECO_MODE_V2_4, disabledGroup, 12);
        
        // Step 3: Set work_mode to Eco (3)
        return tryWriteSingleRegister(REG_WORK_MODE, GOODWE_WORK_MODE_ECO);
    }

    // ==================== Read Data Methods ====================

    InverterData_t readData(String ipAddress)
    {
        // Set IP address
        if (!ipAddress.isEmpty())
        {
            IPAddress newIp;
            if (newIp.fromString(ipAddress))
            {
                if (ip != newIp)
                {
                    LOGD("Using IP from settings: %s", ipAddress.c_str());
                }
                ip = newIp;
            }
        }

        if (ip == IPAddress(0, 0, 0, 0))
        {
            ip = discoverDongleIP();
            if (ip == IPAddress(0, 0, 0, 0))
            {
                ip = IPAddress(10, 10, 100, 253);
            }
        }

        InverterData_t inverterData{};
        
        // Read running data (critical - must succeed)
        if (!tryReadWithRetries(35100, 125, [&](ModbusResponse& response) {
            inverterData.status = DONGLE_STATUS_OK;
            inverterData.millis = millis();
            inverterData.pv1Power = response.readUInt32(35100 + 5);
            inverterData.pv2Power = response.readUInt32(35100 + 9);
            inverterData.pv3Power = response.readUInt32(35100 + 13);
            inverterData.pv4Power = response.readUInt32(35100 + 17);
            inverterData.batteryPower -= response.readInt16(35100 + 83);
            inverterData.inverterOutpuPowerL1 = response.readInt16(35100 + 25);
            inverterData.inverterOutpuPowerL2 = response.readInt16(35100 + 30);
            inverterData.inverterOutpuPowerL3 = response.readInt16(35100 + 35);
            inverterData.loadPower = inverterData.pv1Power + inverterData.pv2Power + 
                                     inverterData.pv3Power + inverterData.pv4Power - inverterData.batteryPower;
            inverterData.inverterTemperature = response.readInt16(35100 + 74) / 10;
            inverterData.pvTotal = response.readUInt32(35100 + 91) / 10.0;
            inverterData.pvToday = response.readUInt32(35100 + 93) / 10.0;
            inverterData.loadToday = response.readUInt16(35100 + 105) / 10.0;
            inverterData.loadTotal = response.readUInt32(35100 + 103) / 10.0;
            inverterData.batteryChargedToday = response.readUInt16(35100 + 108) / 10.0;
            inverterData.batteryDischargedToday = response.readUInt16(35100 + 111) / 10.0;
            
            int newDay = (response.readUInt16(35100 + 1) >> 8) & 0xFF;
            if (this->day != newDay)
            {
                this->day = newDay;
                gridBuyTotal = 0;
                gridSellTotal = 0;
            }

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
        }))
        {
            LOGD("Failed to read running data");
            logInverterData(inverterData, millis());
            return inverterData;
        }

        // Read SN (optional)
        tryReadWithRetries(35003, 8, [&](ModbusResponse& response) {
            inverterData.sn = response.readString(35003, 8);
        });

        // Read SmartMeter data (optional)
        tryReadWithRetries(36000, 44, [&](ModbusResponse& response) {
            inverterData.gridSellTotal = response.readIEEE754(36000 + 15) / 1000.0f;
            inverterData.gridBuyTotal = response.readIEEE754(36000 + 17) / 1000.0f;
            inverterData.gridPowerL1 = response.readInt32(36000 + 19);
            inverterData.gridPowerL2 = response.readInt32(36000 + 21);
            inverterData.gridPowerL3 = response.readInt32(36000 + 23);
            inverterData.loadPower -= (inverterData.gridPowerL1 + inverterData.gridPowerL2 + inverterData.gridPowerL3);
            
            if (gridBuyTotal == 0) gridBuyTotal = inverterData.gridBuyTotal;
            if (gridSellTotal == 0) gridSellTotal = inverterData.gridSellTotal;
            inverterData.gridSellToday = inverterData.gridSellTotal - gridSellTotal;
            inverterData.gridBuyToday = inverterData.gridBuyTotal - gridBuyTotal;
        });

        // Read BMS info (optional)
        tryReadWithRetries(37000, 8, [&](ModbusResponse& response) {
            inverterData.batteryTemperature = response.readUInt16(37000 + 3) / 10;
            inverterData.soc = response.readUInt16(37000 + 7);
        });

        // Read work mode for intelligence support (optional)
        uint16_t workMode = 0;
        int16_t ecoModePower = 0;
        bool ecoModeEnabled = false;
        
        tryReadWithRetries(REG_WORK_MODE, 1, [&](ModbusResponse& response) {
            workMode = response.readUInt16(REG_WORK_MODE);
        });
        
        if (workMode == GOODWE_WORK_MODE_ECO)
        {
            tryReadWithRetries(REG_ECO_MODE_V2_1, REG_ECO_MODE_V2_REGS, [&](ModbusResponse& response) {
                int8_t onOff = (int8_t)response.data[4];
                ecoModePower = (int16_t)((response.data[6] << 8) | response.data[7]);
                ecoModeEnabled = (onOff != 0 && onOff != 85);
            });
        }
        
        inverterData.inverterMode = goodweModeToInverterMode(workMode, ecoModePower, ecoModeEnabled);
        inverterData.hasBattery = inverterData.soc != 0 || inverterData.batteryPower != 0;
        
        logInverterData(inverterData, millis() - inverterData.millis);

        if (inverterData.status != DONGLE_STATUS_OK)
        {
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

                int indexOfComma = String(d).indexOf(',');
                String ipStr = String(d).substring(0, indexOfComma);
                dongleIP.fromString(ipStr);
                break;
            }
        }
        udp.stop();
        return dongleIP;
    }
};
