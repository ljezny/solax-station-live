#pragma once

#include "../../Protocol/V5TCP.hpp"
#include <RemoteLogger.hpp>
#include <SolarIntelligence.h>

class SofarSolarDongleAPI
{
public:
    /**
     * Returns true if this inverter supports intelligence mode control
     */
    bool supportsIntelligence() { return true; }

    InverterData_t loadData(String ipAddress, String sn)
    {
        return readData(ipAddress, sn);
    }

    /**
     * Sets the work mode of the SofarSolar inverter for intelligent battery control
     * 
     * Based on official Sofar documentation (sofar solar.pdf):
     * - Register 0x1110: Energy Storage Mode (0=Self-Use, 3=Passive Mode)
     * - Registers 0x1187-0x118C: Passive Mode parameters
     *   - 0x1187-0x1188 (I32): Desired Grid Power - Positive=buy from grid, Negative=sell to grid
     *   - 0x1189-0x118A (I32): Battery Min Power - Positive=charge, Negative=discharge
     *   - 0x118B-0x118C (I32): Battery Max Power - Positive=charge, Negative=discharge
     * 
     * @param ipAddress IP address of the dongle
     * @param dongleSN Serial number of the dongle
     * @param mode Desired inverter mode
     * @return true if mode was set successfully
     */
    bool setWorkMode(const String& ipAddress, const String& dongleSN, SolarInverterMode_t mode)
    {
        LOGD("setWorkMode called: ip=%s, sn=%s, mode=%d", ipAddress.c_str(), dongleSN.c_str(), mode);
        
        uint32_t sn = strtoul(dongleSN.c_str(), NULL, 10);
        if (sn == 0)
        {
            LOGE("Invalid SN for setWorkMode: '%s'", dongleSN.c_str());
            return false;
        }

        channel.ensureIPAddress(ipAddress);

        bool success = false;

        switch (mode)
        {
        case SI_MODE_SELF_USE:
            // Self-Use mode (Energy Storage Mode = 0)
            LOGD("Setting Self-Use mode (reg 0x%04X = %d)", SOFAR_REG_ENERGY_STORAGE_MODE, SOFAR_MODE_SELF_USE);
            success = writeRegister(sn, SOFAR_REG_ENERGY_STORAGE_MODE, SOFAR_MODE_SELF_USE);
            break;

        case SI_MODE_HOLD_BATTERY:
            // Passive Mode with 0 power = hold battery at current state
            LOGD("Setting Hold Battery mode (Passive + GridPower=0, Bat=0)");
            success = setPassiveMode(sn, 0, 0, 0);  // Grid=0, MinBat=0, MaxBat=0
            break;

        case SI_MODE_CHARGE_FROM_GRID:
        {
            // Passive Mode: positive grid power = buy from grid (charge battery)
            // Based on Sofar docs: GridPower > 0 = buy from grid, Bat > 0 = charge
            // Load power from Intelligence settings
            SolarIntelligenceSettings_t settings = IntelligenceSettingsStorage::load();
            int32_t chargePowerW = (int32_t)(settings.maxChargePowerKw * 1000);
            LOGD("Setting Charge from Grid mode (Passive + GridPower=+%dW, Bat +1000 to +%dW)", chargePowerW, chargePowerW);
            success = setPassiveMode(sn, chargePowerW, 1000, chargePowerW);
            break;
        }

        case SI_MODE_DISCHARGE_TO_GRID:
        {
            // Passive Mode: negative grid power = sell to grid (discharge battery)
            // Based on Sofar docs: GridPower < 0 = sell to grid, Bat < 0 = discharge
            // Load power from Intelligence settings
            SolarIntelligenceSettings_t settings = IntelligenceSettingsStorage::load();
            int32_t dischargePowerW = (int32_t)(settings.maxDischargePowerKw * 1000);
            LOGD("Setting Discharge to Grid mode (Passive + GridPower=-%dW, Bat -%d to -1000W)", dischargePowerW, dischargePowerW);
            success = setPassiveMode(sn, -dischargePowerW, -dischargePowerW, -1000);
            break;
        }

        default:
            LOGW("Unknown mode requested: %d", mode);
            break;
        }

        LOGD("setWorkMode result: %s", success ? "SUCCESS" : "FAILED");
        return success;
    }

    // Overload for compatibility - without dongleSN parameter
    bool setWorkMode(const String& ipAddress, SolarInverterMode_t mode)
    {
        LOGW("setWorkMode called without dongleSN - not supported for Sofar");
        return false;
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

        // Grid Input - registry z dokumentace Sofar (0x0480-0x04FF)
        // 0x485 = ActivePower_Output_Total, 0x488 = ActivePower_PCC_Total (SmartMeter)
        // 0x48F/0x49A/0x4A5 = ActivePower_Output per phase (L1/L2/L3)
        // 0x493/0x49E/0x4A9 = ActivePower_PCC per phase (SmartMeter L1/L2/L3)
        if (!channel.tryReadWithRetries(0x484, 0x4BC - 0x484 + 1, sn, packetBuffer, [&]()
                                {
            // Celkový výkon střídače (0x485) - pro jednofázové střídače je to hlavní hodnota
            int16_t inverterOutputTotal = channel.readInt16(packetBuffer, 0x485 - 0x484) * 10;
            
            // Výkon po fázích
            inverterData.inverterOutpuPowerL1 = channel.readInt16(packetBuffer, 0x48F - 0x484) * 10;  // Output L1
            inverterData.inverterOutpuPowerL2 = channel.readInt16(packetBuffer, 0x49A - 0x484) * 10;  // Output L2
            inverterData.inverterOutpuPowerL3 = channel.readInt16(packetBuffer, 0x4A5 - 0x484) * 10;  // Output L3 (opraveno z 0x4A9)
            
            // SmartMeter (PCC) výkony - celkový a po fázích
            int16_t gridPowerTotal = channel.readInt16(packetBuffer, 0x488 - 0x484) * 10;  // PCC Total
            inverterData.gridPowerL1 = channel.readInt16(packetBuffer, 0x493 - 0x484) * 10;  // PCC L1
            inverterData.gridPowerL2 = channel.readInt16(packetBuffer, 0x49E - 0x484) * 10;  // PCC L2
            inverterData.gridPowerL3 = channel.readInt16(packetBuffer, 0x4A9 - 0x484) * 10;  // PCC L3
            
            // Detekce jednofázového střídače: L2 a L3 jsou 0
            bool isSinglePhase = (inverterData.inverterOutpuPowerL2 == 0 && inverterData.inverterOutpuPowerL3 == 0);
            
            if (isSinglePhase) {
                // Jednofázový střídač: celkový výkon do L1
                if (inverterOutputTotal != 0) {
                    inverterData.inverterOutpuPowerL1 = inverterOutputTotal;
                }
                if (gridPowerTotal != 0) {
                    inverterData.gridPowerL1 = gridPowerTotal;
                }
                LOGD("Single-phase inverter: Output=%dW, Grid=%dW", inverterOutputTotal, gridPowerTotal);
            } else {
                LOGD("Three-phase inverter: Output L1=%d L2=%d L3=%d, Grid L1=%d L2=%d L3=%d",
                     inverterData.inverterOutpuPowerL1, inverterData.inverterOutpuPowerL2, inverterData.inverterOutpuPowerL3,
                     inverterData.gridPowerL1, inverterData.gridPowerL2, inverterData.gridPowerL3);
            }
            
            // Load power
            inverterData.loadPower = channel.readInt16(packetBuffer, 0x04AF - 0x484) * 10; }))
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
        
        // Read current work mode (Passive Mode parameters)
        readWorkMode(sn, inverterData);

        inverterData.hasBattery = inverterData.soc != 0 || inverterData.batteryPower != 0;
        logInverterData(inverterData, millis() - inverterData.millis);
        return inverterData;
    }

    void readInverterRTC(uint32_t sn, InverterData_t &inverterData)
    {
        // SofarSolar RTC registers: 0x42C-0x431 (holding registers)
        // Format: Year (% 100), Month, Day, Hour, Minute, Second
        // Buffer: 3 header + 12 data (6 regs * 2 bytes) + 2 CRC = 17 bytes min
        byte packetBuffer[32];
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
     * Read current work mode from the inverter
     * Reads Energy Storage Mode (0x1110) and Passive Mode parameters (0x1187-0x118C)
     */
    void readWorkMode(uint32_t sn, InverterData_t &inverterData)
    {
        byte packetBuffer[32];
        
        // Read Energy Storage Mode (0x1110)
        if (!channel.tryReadWithRetries(SOFAR_REG_ENERGY_STORAGE_MODE, 1, sn, packetBuffer, [&]()
            {
                uint16_t storageMode = channel.readUInt16(packetBuffer, 0);
                LOGD("Energy Storage Mode: %d (0=Self-Use, 3=Passive)", storageMode);
                
                if (storageMode == SOFAR_MODE_SELF_USE)
                {
                    inverterData.inverterMode = SI_MODE_SELF_USE;
                }
                else if (storageMode == SOFAR_MODE_PASSIVE)
                {
                    // In passive mode, we need to read the power parameters to determine the actual mode
                    inverterData.inverterMode = SI_MODE_HOLD_BATTERY; // Will be updated below
                }
            }))
        {
            LOGW("Failed to read Energy Storage Mode");
            return;
        }
        
        // If in Passive Mode, read the power parameters to determine actual mode
        if (inverterData.inverterMode == SI_MODE_HOLD_BATTERY)
        {
            // Read Passive Mode parameters (0x1187-0x118C = 6 registers)
            if (channel.tryReadWithRetries(SOFAR_REG_PASSIVE_GRID_POWER, 6, sn, packetBuffer, [&]()
                {
                    // Grid Power (I32) at 0x1187-0x1188
                    int16_t gridPowerHigh = channel.readInt16(packetBuffer, 0);
                    uint16_t gridPowerLow = channel.readUInt16(packetBuffer, 1);
                    int32_t gridPower = ((int32_t)gridPowerHigh << 16) | gridPowerLow;
                    
                    // Min Battery Power (I32) at 0x1189-0x118A
                    int16_t minBatHigh = channel.readInt16(packetBuffer, 2);
                    uint16_t minBatLow = channel.readUInt16(packetBuffer, 3);
                    int32_t minBatPower = ((int32_t)minBatHigh << 16) | minBatLow;
                    
                    // Max Battery Power (I32) at 0x118B-0x118C
                    int16_t maxBatHigh = channel.readInt16(packetBuffer, 4);
                    uint16_t maxBatLow = channel.readUInt16(packetBuffer, 5);
                    int32_t maxBatPower = ((int32_t)maxBatHigh << 16) | maxBatLow;
                    
                    LOGD("Passive Mode params: GridPower=%d, MinBat=%d, MaxBat=%d", gridPower, minBatPower, maxBatPower);
                    
                    // Determine mode based on parameters
                    // Positive GridPower = buy from grid (charging)
                    // Negative GridPower = sell to grid (discharging)
                    // Positive Bat = charge, Negative Bat = discharge
                    
                    if (gridPower > 500 && maxBatPower > 500)
                    {
                        // Buying from grid and battery charging
                        inverterData.inverterMode = SI_MODE_CHARGE_FROM_GRID;
                    }
                    else if (gridPower < -500 && maxBatPower < -500)
                    {
                        // Selling to grid and battery discharging
                        inverterData.inverterMode = SI_MODE_DISCHARGE_TO_GRID;
                    }
                    else if (gridPower == 0 && minBatPower == 0 && maxBatPower == 0)
                    {
                        // All zeros = hold battery
                        inverterData.inverterMode = SI_MODE_HOLD_BATTERY;
                    }
                    else
                    {
                        // Some other passive mode configuration
                        inverterData.inverterMode = SI_MODE_HOLD_BATTERY;
                    }
                }))
            {
                LOGD("Read Passive Mode parameters successfully");
            }
            else
            {
                LOGW("Failed to read Passive Mode parameters");
            }
        }
    }

private:
    // SofarSolar Modbus register addresses (based on homeassistant-solax-modbus)
    static constexpr uint16_t SOFAR_REG_ENERGY_STORAGE_MODE = 0x1110;  // Energy Storage Mode
    static constexpr uint16_t SOFAR_REG_PASSIVE_GRID_POWER = 0x1187;  // Desired Grid Power (S32)
    static constexpr uint16_t SOFAR_REG_PASSIVE_BAT_MIN = 0x1189;     // Min Battery Power (S32)
    static constexpr uint16_t SOFAR_REG_PASSIVE_BAT_MAX = 0x118B;     // Max Battery Power (S32)
    
    // SofarSolar Energy Storage Mode values
    static constexpr uint16_t SOFAR_MODE_SELF_USE = 0;           // Self-Use
    static constexpr uint16_t SOFAR_MODE_TIME_OF_USE = 1;        // Time of Use
    static constexpr uint16_t SOFAR_MODE_TIMING = 2;             // Timing Mode
    static constexpr uint16_t SOFAR_MODE_PASSIVE = 3;            // Passive Mode
    static constexpr uint16_t SOFAR_MODE_PEAK_CUT = 4;           // Peak Cut Mode

    /**
     * Set Passive Mode with specific power parameters
     * Based on HA integration - writes all 6 passive mode registers in a single transaction
     * Sofar inverters require I32 values to be written as complete 2-register pairs
     * 
     * @param sn Dongle serial number
     * @param gridPower Desired grid power in W (positive=buy, negative=sell)
     * @param minBatPower Min battery power in W (positive=charge, negative=discharge)
     * @param maxBatPower Max battery power in W (positive=charge, negative=discharge)
     * @return true if successful
     */
    bool setPassiveMode(uint32_t sn, int32_t gridPower, int32_t minBatPower, int32_t maxBatPower)
    {
        LOGD("setPassiveMode: gridPower=%d, minBat=%d, maxBat=%d", gridPower, minBatPower, maxBatPower);
        
        // Step 1: Set Energy Storage Mode to Passive (0x1110 = 3)
        if (!writeRegister(sn, SOFAR_REG_ENERGY_STORAGE_MODE, SOFAR_MODE_PASSIVE))
        {
            LOGE("Failed to set Energy Storage Mode to Passive");
            return false;
        }
        
        // Small delay to let inverter process mode change
        delay(500);
        
        // Step 2: Write all 6 passive mode registers in a single transaction
        // Sofar requires I32 values to be written as complete high+low word pairs
        // Registers 0x1187-0x118C: GridPower(I32), MinBat(I32), MaxBat(I32)
        uint16_t values[6];
        
        // GridPower (I32) at 0x1187-0x1188 - big endian (high word first)
        values[0] = (uint16_t)(gridPower >> 16);       // 0x1187: high word
        values[1] = (uint16_t)(gridPower & 0xFFFF);    // 0x1188: low word
        
        // MinBatPower (I32) at 0x1189-0x118A
        values[2] = (uint16_t)(minBatPower >> 16);     // 0x1189: high word
        values[3] = (uint16_t)(minBatPower & 0xFFFF);  // 0x118A: low word
        
        // MaxBatPower (I32) at 0x118B-0x118C
        values[4] = (uint16_t)(maxBatPower >> 16);     // 0x118B: high word
        values[5] = (uint16_t)(maxBatPower & 0xFFFF);  // 0x118C: low word
        
        LOGD("Writing Passive params: Grid=%d [0x%04X,0x%04X], MinBat=%d [0x%04X,0x%04X], MaxBat=%d [0x%04X,0x%04X]",
             gridPower, values[0], values[1],
             minBatPower, values[2], values[3],
             maxBatPower, values[4], values[5]);
        
        if (!channel.connect(channel.ip))
        {
            LOGE("setPassiveMode: Failed to connect to %s", channel.ip.toString().c_str());
            return false;
        }
        
        bool success = channel.writeMultipleRegisters(SOFAR_REG_PASSIVE_GRID_POWER, values, 6, sn);
        channel.disconnect();
        
        if (success)
        {
            LOGD("setPassiveMode: SUCCESS - all 6 registers written in single transaction");
        }
        else
        {
            LOGE("setPassiveMode: FAILED to write passive mode registers");
        }
        
        return success;
    }

    /**
     * Write a single register using Solarman V5 protocol
     * Uses Modbus function 0x10 (Write Multiple Registers) instead of 0x06 (Write Single Register)
     * because Sofar inverters require function 0x10 for configuration registers
     */
    bool writeRegister(uint32_t sn, uint16_t addr, uint16_t value)
    {
        LOGD("writeRegister: addr=0x%04X, value=%d (0x%04X), sn=%lu [using FC16]", addr, value, value, sn);
        
        if (!channel.connect(channel.ip))
        {
            LOGE("writeRegister: Failed to connect to %s", channel.ip.toString().c_str());
            return false;
        }
        
        // Use writeMultipleRegisters (function code 0x10) instead of writeSingleRegister (0x06)
        // Sofar inverters require FC16 for configuration registers
        uint16_t values[1] = { value };
        bool success = channel.writeMultipleRegisters(addr, values, 1, sn);
        channel.disconnect();
        
        if (success)
        {
            LOGD("writeRegister: SUCCESS");
        }
        else
        {
            LOGE("writeRegister: FAILED");
        }
        return success;
    }
};