#pragma once

#include "../../Protocol/V5TCP.hpp"
#include <RemoteLogger.hpp>
#include <SolarIntelligence.h>

class SofarSolarDongleAPI
{
public:
    /**
     * Returns true if this inverter supports intelligence mode control
     * Note: Old protocol (SM1E/SE1E/ZM1E/ZE1E) does NOT support Intelligence
     */
    bool supportsIntelligence() { return !useOldProtocol; }

    InverterData_t loadData(String ipAddress, String sn)
    {
        LOGI("=== Sofar loadData START === IP=%s, DongleSN=%s", ipAddress.c_str(), sn.c_str());
        
        // If we already detected protocol, use it directly
        if (protocolDetected) {
            if (useOldProtocol) {
                LOGI("Protocol already detected: OLD (SM1E/SE1E/ZM1E/ZE1E)");
                return readDataOldProtocol(ipAddress, sn);
            } else {
                LOGI("Protocol already detected: NEW (SM2E/ZM2E/etc)");
                return readData(ipAddress, sn);
            }
        }
        
        // First run - detect protocol by reading Serial Number from different addresses
        // NEW protocol: SN at 0x445 (HOLDING register) - returns SM2E*, ZM2E* etc.
        // OLD protocol: SN at 0x2002 (INPUT register) - returns SM1E*, SE1E*, ZM1E*, ZE1E*
        
        LOGI("=== PROTOCOL DETECTION START ===");
        LOGI("Attempting to detect Sofar protocol type by reading Inverter Serial Number...");
        uint32_t dongleSn = strtoul(sn.c_str(), NULL, 10);
        LOGI("Dongle SN (numeric): %lu", dongleSn);
        channel.ensureIPAddress(ipAddress);
        
        // Try to read SN from NEW protocol address (0x445)
        LOGI("Step 1: Trying NEW protocol - reading SN from register 0x445 (HOLDING)...");
        String inverterSN = readInverterSerialNumber(dongleSn, 0x445, 7, false);  // HOLDING register
        LOGI("NEW protocol SN read result: '%s' (length=%d)", inverterSN.c_str(), inverterSN.length());
        
        if (isValidSerialNumber(inverterSN)) {
            LOGI("=== NEW PROTOCOL DETECTED ===");
            LOGI("Valid NEW protocol SN found: '%s'", inverterSN.c_str());
            protocolDetected = true;
            useOldProtocol = false;
            return readData(ipAddress, sn);
        }
        LOGI("NEW protocol SN '%s' is NOT valid (unknown prefix or too short)", inverterSN.c_str());
        
        // Try OLD protocol address (0x2002 INPUT register)
        LOGI("Step 2: Trying OLD protocol - reading SN from register 0x2002 (INPUT)...");
        inverterSN = readInverterSerialNumber(dongleSn, 0x2002, 6, true);  // INPUT register
        LOGI("OLD protocol SN read result: '%s' (length=%d)", inverterSN.c_str(), inverterSN.length());
        
        if (isValidSerialNumber(inverterSN)) {
            LOGI("=== OLD PROTOCOL DETECTED ===");
            LOGI("Valid OLD protocol SN found: '%s' - this is SM1E/SE1E/ZM1E/ZE1E model", inverterSN.c_str());
            protocolDetected = true;
            useOldProtocol = true;
            return readDataOldProtocol(ipAddress, sn);
        }
        LOGI("OLD protocol SN '%s' is NOT valid (unknown prefix or too short)", inverterSN.c_str());
        
        // Fallback: neither worked, try NEW protocol and validate data
        LOGW("=== SN DETECTION FAILED - FALLBACK TO DATA VALIDATION ===");
        LOGW("Could not detect protocol from Serial Number, trying NEW protocol with data validation...");
        InverterData_t data = readData(ipAddress, sn);
        LOGI("NEW protocol read result: status=%d, SOC=%d, batTemp=%d, invTemp=%d", 
             data.status, data.soc, data.batteryTemperature, data.inverterTemperature);
        
        if (data.status == DONGLE_STATUS_OK && isDataValid(data)) {
            LOGI("=== NEW PROTOCOL CONFIRMED BY DATA VALIDATION ===");
            protocolDetected = true;
            useOldProtocol = false;
            return data;
        }
        LOGW("NEW protocol data INVALID or read failed");
        
        // Last resort: try OLD protocol
        LOGW("Step 4: Last resort - trying OLD protocol with data validation...");
        data = readDataOldProtocol(ipAddress, sn);
        LOGI("OLD protocol read result: status=%d, SOC=%d, batTemp=%d, invTemp=%d", 
             data.status, data.soc, data.batteryTemperature, data.inverterTemperature);
        
        if (data.status == DONGLE_STATUS_OK && isDataValid(data)) {
            LOGI("=== OLD PROTOCOL CONFIRMED BY DATA VALIDATION ===");
            protocolDetected = true;
            useOldProtocol = true;
        } else {
            LOGE("=== PROTOCOL DETECTION FAILED ===");
            LOGE("Neither NEW nor OLD protocol returned valid data!");
        }
        
        return data;
    }
    
    /**
     * Read inverter serial number from specified register address
     * @param dongleSn Dongle serial number (numeric)
     * @param address Register address (0x445 for NEW, 0x2002 for OLD)
     * @param wordCount Number of 16-bit words to read (7 for NEW, 6 for OLD)
     * @param inputRegister True for INPUT register (OLD), false for HOLDING (NEW)
     * @return Serial number string or empty string on failure
     */
    String readInverterSerialNumber(uint32_t dongleSn, uint16_t address, uint8_t wordCount, bool inputRegister) {
        byte packetBuffer[32];
        String result = "";
        
        LOGD("readInverterSerialNumber: addr=0x%04X, words=%d, inputReg=%s", 
             address, wordCount, inputRegister ? "true" : "false");
        
        // For INPUT registers (OLD protocol), we need to use function code 0x04
        // For HOLDING registers (NEW protocol), we use function code 0x03
        // V5TCP::tryReadWithRetries uses HOLDING (0x03) by default
        
        bool success = false;
        if (inputRegister) {
            // OLD protocol uses INPUT registers - need different function code
            // For now, try with standard read - some inverters respond to both
            success = channel.tryReadWithRetries(address, wordCount, dongleSn, packetBuffer, [&]() {
                // Convert registers to ASCII string
                LOGD("Raw bytes from 0x%04X:", address);
                for (int i = 0; i < wordCount; i++) {
                    uint16_t reg = channel.readUInt16(packetBuffer, i);
                    LOGD("  Word[%d] = 0x%04X ('%c' '%c')", i, reg, 
                         (reg >> 8) >= 0x20 && (reg >> 8) <= 0x7E ? (reg >> 8) : '.',
                         (reg & 0xFF) >= 0x20 && (reg & 0xFF) <= 0x7E ? (reg & 0xFF) : '.');
                    char c1 = (reg >> 8) & 0xFF;
                    char c2 = reg & 0xFF;
                    if (c1 >= 0x20 && c1 <= 0x7E) result += c1;
                    if (c2 >= 0x20 && c2 <= 0x7E) result += c2;
                }
            });
        } else {
            success = channel.tryReadWithRetries(address, wordCount, dongleSn, packetBuffer, [&]() {
                // Convert registers to ASCII string
                LOGD("Raw bytes from 0x%04X:", address);
                for (int i = 0; i < wordCount; i++) {
                    uint16_t reg = channel.readUInt16(packetBuffer, i);
                    LOGD("  Word[%d] = 0x%04X ('%c' '%c')", i, reg, 
                         (reg >> 8) >= 0x20 && (reg >> 8) <= 0x7E ? (reg >> 8) : '.',
                         (reg & 0xFF) >= 0x20 && (reg & 0xFF) <= 0x7E ? (reg & 0xFF) : '.');
                    char c1 = (reg >> 8) & 0xFF;
                    char c2 = reg & 0xFF;
                    if (c1 >= 0x20 && c1 <= 0x7E) result += c1;
                    if (c2 >= 0x20 && c2 <= 0x7E) result += c2;
                }
            });
        }
        
        if (success) {
            LOGI("SN read from 0x%04X SUCCESS: '%s'", address, result.c_str());
        } else {
            LOGW("SN read from 0x%04X FAILED", address);
        }
        
        return result;
    }
    
    /**
     * Check if serial number is valid (contains expected Sofar prefixes)
     */
    bool isValidSerialNumber(const String& sn) {
        if (sn.length() < 4) {
            LOGD("SN too short: '%s' (len=%d, need >=4)", sn.c_str(), sn.length());
            return false;
        }
        
        String prefix = sn.substring(0, 4);
        LOGD("Checking SN prefix: '%s' (full SN: '%s')", prefix.c_str(), sn.c_str());
        
        // Known Sofar SN prefixes from HA plugin documentation
        // NEW protocol (plugin_sofar.py)
        if (sn.startsWith("SM2E")) { LOGD("Matched NEW protocol prefix: SM2E"); return true; }
        if (sn.startsWith("ZM2E")) { LOGD("Matched NEW protocol prefix: ZM2E"); return true; }
        if (sn.startsWith("SP1")) { LOGD("Matched NEW protocol prefix: SP1"); return true; }
        if (sn.startsWith("SP2")) { LOGD("Matched NEW protocol prefix: SP2"); return true; }
        if (sn.startsWith("ZP1")) { LOGD("Matched NEW protocol prefix: ZP1"); return true; }
        if (sn.startsWith("ZP2")) { LOGD("Matched NEW protocol prefix: ZP2"); return true; }
        if (sn.startsWith("SH3E")) { LOGD("Matched NEW protocol prefix: SH3E"); return true; }
        if (sn.startsWith("SS2E")) { LOGD("Matched NEW protocol prefix: SS2E"); return true; }
        if (sn.startsWith("ZS2E")) { LOGD("Matched NEW protocol prefix: ZS2E"); return true; }
        if (sn.startsWith("SH1")) { LOGD("Matched NEW protocol prefix: SH1"); return true; }
        
        // OLD protocol (plugin_sofar_old.py)
        if (sn.startsWith("SM1E")) { LOGI("Matched OLD protocol prefix: SM1E (HYDxxxxES old)"); return true; }
        if (sn.startsWith("SE1E")) { LOGI("Matched OLD protocol prefix: SE1E (HYDxxxxES)"); return true; }
        if (sn.startsWith("ZM1E")) { LOGI("Matched OLD protocol prefix: ZM1E (HYDxxxxES)"); return true; }
        if (sn.startsWith("ZE1E")) { LOGI("Matched OLD protocol prefix: ZE1E (HYDxxxxES)"); return true; }
        if (sn.startsWith("SA1")) { LOGD("Matched OLD protocol prefix: SA1"); return true; }
        if (sn.startsWith("SA3")) { LOGD("Matched OLD protocol prefix: SA3"); return true; }
        if (sn.startsWith("SB1")) { LOGD("Matched OLD protocol prefix: SB1"); return true; }
        if (sn.startsWith("ZA3")) { LOGD("Matched OLD protocol prefix: ZA3"); return true; }
        if (sn.startsWith("SC1")) { LOGD("Matched OLD protocol prefix: SC1"); return true; }
        if (sn.startsWith("SD1")) { LOGD("Matched OLD protocol prefix: SD1"); return true; }
        if (sn.startsWith("SF4")) { LOGD("Matched OLD protocol prefix: SF4"); return true; }
        if (sn.startsWith("SL1")) { LOGD("Matched OLD protocol prefix: SL1"); return true; }
        if (sn.startsWith("SJ2")) { LOGD("Matched OLD protocol prefix: SJ2"); return true; }
        if (sn.startsWith("SM1")) { LOGD("Matched OLD protocol prefix: SM1"); return true; }
        
        LOGW("UNKNOWN SN prefix: '%s' - not in known Sofar prefixes list!", prefix.c_str());
        return false;
    }
    
    /**
     * Validate inverter data - check if values are within reasonable ranges
     * Used as fallback when SN detection fails
     */
    bool isDataValid(const InverterData_t& data) {
        LOGD("Validating data: SOC=%d, batTemp=%d, invTemp=%d", 
             data.soc, data.batteryTemperature, data.inverterTemperature);
        
        // SOC must be 0-100%
        if (data.soc < 0 || data.soc > 100) {
            LOGW("Data validation FAILED: Invalid SOC=%d (expected 0-100)", data.soc);
            return false;
        }
        
        // Battery temperature must be reasonable (-40 to +80°C)
        if (data.batteryTemperature < -40 || data.batteryTemperature > 80) {
            LOGW("Data validation FAILED: Invalid batteryTemp=%d (expected -40 to +80)", data.batteryTemperature);
            return false;
        }
        
        // Inverter temperature must be reasonable (-40 to +100°C)
        if (data.inverterTemperature < -40 || data.inverterTemperature > 100) {
            LOGW("Data validation FAILED: Invalid inverterTemp=%d (expected -40 to +100)", data.inverterTemperature);
            return false;
        }
        
        LOGD("Data validation PASSED");
        return true;
    }
    
private:
    bool protocolDetected = false;  // True after first successful protocol detection
    bool useOldProtocol = false;    // True if old protocol (SM1E/SE1E/ZM1E/ZE1E) detected
    
public:

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

    /**
     * Read data using OLD protocol for SM1E/SE1E/ZM1E/ZE1E models
     * These are older single-phase hybrid inverters (HYD 3-6kW ES series)
     * They use completely different Modbus register addresses
     * 
     * Based on homeassistant-solax-modbus plugin_sofar_old.py
     */
    InverterData_t readDataOldProtocol(String ipAddress, String dongleSN)
    {
        InverterData_t inverterData{};
        
        LOGI("=======================================================");
        LOGI("=== Reading Sofar OLD protocol (SM1E/SE1E/ZM1E/ZE1E) ===");
        LOGI("=======================================================");
        LOGI("IP: %s, DongleSN: %s", ipAddress.c_str(), dongleSN.c_str());
        
        // Validate SN length
        if (dongleSN.length() > 10) {
            LOGE("Dongle SN '%s' is too long (%d chars). Truncating.", dongleSN.c_str(), dongleSN.length());
            dongleSN = dongleSN.substring(0, 10);
        }
        
        uint32_t sn = strtoul(dongleSN.c_str(), NULL, 10);
        if (sn == ULONG_MAX || sn == 0) {
            LOGE("Invalid SN conversion: '%s' -> %lu", dongleSN.c_str(), sn);
            return inverterData;
        }
        
        LOGI("OLD protocol - Dongle SN (numeric): %lu", sn);
        inverterData.sn = dongleSN;
        inverterData.millis = millis();
        byte packetBuffer[256];

        channel.ensureIPAddress(ipAddress);

        // OLD Protocol registers for HYBRID (AC | HYBRID type):
        // 0x200 = Run Mode
        // 0x206-0x20C = Voltage/Current R,S,T + Grid Frequency
        // 0x20D = Battery Power Charge (S16, scale 0.01 kW)
        // 0x20E = Battery Voltage
        // 0x20F = Battery Current
        // 0x210 = Battery SOC (capacity)
        // 0x211 = Battery Temperature
        // 0x212 = Measured Power (grid, S16, scale 0.01 kW)
        // 0x213 = House Load (scale 0.01 kW)
        // 0x215 = Generation Power (scale 0.01 kW)
        // 0x218 = Generation Today (scale 0.01 kWh)
        // 0x219 = Export Energy Today (scale 0.01 kWh)
        // 0x21A = Import Energy Today (scale 0.01 kWh)
        // 0x21B = Consumption Today (scale 0.01 kWh)
        // 0x238 = Inverter Inner Temperature (S16)
        // 0x239 = Inverter Heatsink Temperature (S16)
        // 0x250-0x255 = PV1 and PV2 data

        // Read main block: 0x200 to 0x21B (28 registers)
        if (!channel.tryReadWithRetries(0x200, 0x21C - 0x200, sn, packetBuffer, [&]()
                                {
            // Run Mode at 0x200
            uint16_t runMode = channel.readUInt16(packetBuffer, 0x200 - 0x200);
            LOGI("OLD: Run Mode = %d (0=Wait,1=Check,2=Normal,3=CheckDisch,4=Disch,5=EPS,6=Fault)", runMode);
            
            // Battery Power at 0x20D (S16, scale 0.01 kW = 10W)
            // Positive = charging, Negative = discharging (need to invert for our convention)
            int16_t battPowerRaw = channel.readInt16(packetBuffer, 0x20D - 0x200);
            inverterData.batteryPower = battPowerRaw * 10;  // Convert to W (0.01 kW * 1000 / 100 = *10)
            LOGI("OLD: Battery Power raw=%d (reg 0x20D) -> %dW", battPowerRaw, inverterData.batteryPower);
            
            // Battery SOC at 0x210
            inverterData.soc = channel.readUInt16(packetBuffer, 0x210 - 0x200);
            LOGI("OLD: SOC = %d%% (reg 0x210)", inverterData.soc);
            
            // Battery Temperature at 0x211 (direct value in °C)
            inverterData.batteryTemperature = channel.readInt16(packetBuffer, 0x211 - 0x200);
            LOGI("OLD: Battery Temp = %d°C (reg 0x211)", inverterData.batteryTemperature);
            
            // Grid Power (Measured Power) at 0x212 (S16, scale 0.01 kW)
            // Positive = export, Negative = import (check this)
            int16_t gridPowerRaw = channel.readInt16(packetBuffer, 0x212 - 0x200);
            inverterData.gridPowerL1 = gridPowerRaw * 10;  // Convert to W
            LOGI("OLD: Grid Power raw=%d (reg 0x212) -> %dW", gridPowerRaw, inverterData.gridPowerL1);
            
            // House Load at 0x213 (scale 0.01 kW)
            int16_t loadPowerRaw = channel.readInt16(packetBuffer, 0x213 - 0x200);
            inverterData.loadPower = loadPowerRaw * 10;  // Convert to W
            LOGI("OLD: Load Power raw=%d (reg 0x213) -> %dW", loadPowerRaw, inverterData.loadPower);
            
            // Generation Power at 0x215 (scale 0.01 kW) - this is inverter output
            int16_t genPowerRaw = channel.readInt16(packetBuffer, 0x215 - 0x200);
            inverterData.inverterOutpuPowerL1 = genPowerRaw * 10;
            LOGI("OLD: Generation Power raw=%d (reg 0x215) -> %dW", genPowerRaw, inverterData.inverterOutpuPowerL1);
            
            // Daily statistics (all scale 0.01 kWh)
            inverterData.pvToday = channel.readUInt16(packetBuffer, 0x218 - 0x200) * 0.01f;
            inverterData.gridSellToday = channel.readUInt16(packetBuffer, 0x219 - 0x200) * 0.01f;
            inverterData.gridBuyToday = channel.readUInt16(packetBuffer, 0x21A - 0x200) * 0.01f;
            inverterData.loadToday = channel.readUInt16(packetBuffer, 0x21B - 0x200) * 0.01f;
            LOGI("OLD: Today - PV=%.2f, GridSell=%.2f, GridBuy=%.2f, Load=%.2f kWh",
                 inverterData.pvToday, inverterData.gridSellToday, inverterData.gridBuyToday, inverterData.loadToday);
            }))
        {
            LOGE("OLD protocol: FAILED to read main block 0x200-0x21B");
            return inverterData;
        }
        LOGI("OLD protocol: Main block 0x200-0x21B read SUCCESS");

        // Read inverter temperatures: 0x238-0x239
        LOGI("OLD protocol: Reading inverter temperature from 0x238...");
        channel.tryReadWithRetries(0x238, 2, sn, packetBuffer, [&]()
                           {
            inverterData.inverterTemperature = channel.readInt16(packetBuffer, 0);
            // 0x239 is heatsink temp, we use inner temp
            LOGI("OLD: Inverter Temp = %d°C (reg 0x238)", inverterData.inverterTemperature);
            });

        // Read PV data: 0x250-0x255 (PV1 and PV2)
        // 0x250 = PV1 Voltage (scale 0.1V)
        // 0x251 = PV1 Current (scale 0.01A)
        // 0x252 = PV1 Power (scale 10W)
        // 0x253 = PV2 Voltage
        // 0x254 = PV2 Current
        // 0x255 = PV2 Power
        channel.tryReadWithRetries(0x250, 6, sn, packetBuffer, [&]()
                           {
            inverterData.pv1Power = channel.readUInt16(packetBuffer, 0x252 - 0x250) * 10;  // scale 10W
            inverterData.pv2Power = channel.readUInt16(packetBuffer, 0x255 - 0x250) * 10;
            LOGI("OLD: PV1=%dW, PV2=%dW (regs 0x252, 0x255)", inverterData.pv1Power, inverterData.pv2Power);
            });

        // Read total statistics: 0x21C-0x223 (all U32)
        // 0x21C-0x21D = Generation Total
        // 0x21E-0x21F = Export Total
        // 0x220-0x221 = Import Total
        // 0x222-0x223 = Consumption Total
        LOGI("OLD protocol: Reading total statistics from 0x21C...");
        channel.tryReadWithRetries(0x21C, 8, sn, packetBuffer, [&]()
                           {
            inverterData.pvTotal = channel.readUInt32(packetBuffer, 0x21C - 0x21C) * 1.0f;  // kWh
            inverterData.gridSellTotal = channel.readUInt32(packetBuffer, 0x21E - 0x21C) * 1.0f;
            inverterData.gridBuyTotal = channel.readUInt32(packetBuffer, 0x220 - 0x21C) * 1.0f;
            inverterData.loadTotal = channel.readUInt32(packetBuffer, 0x222 - 0x21C) * 1.0f;
            LOGI("OLD: Total - PV=%.0f, GridSell=%.0f, GridBuy=%.0f, Load=%.0f kWh",
                 inverterData.pvTotal, inverterData.gridSellTotal, inverterData.gridBuyTotal, inverterData.loadTotal);
            });

        // Success!
        inverterData.status = DONGLE_STATUS_OK;
        inverterData.hasBattery = inverterData.soc != 0 || inverterData.batteryPower != 0;
        
        LOGI("=======================================================");
        LOGI("=== OLD PROTOCOL READ COMPLETE ===");
        LOGI("Status: OK, SOC=%d%%, BatTemp=%d°C, InvTemp=%d°C", 
             inverterData.soc, inverterData.batteryTemperature, inverterData.inverterTemperature);
        LOGI("Battery=%dW, Grid=%dW, Load=%dW, PV1=%dW, PV2=%dW",
             inverterData.batteryPower, inverterData.gridPowerL1, inverterData.loadPower,
             inverterData.pv1Power, inverterData.pv2Power);
        LOGI("=======================================================");
        logInverterData(inverterData, millis() - inverterData.millis);
        return inverterData;
    }

    InverterData_t readData(String ipAddress, String dongleSN)
    {
        InverterData_t inverterData{};
        
        LOGI("=======================================================");
        LOGI("=== Reading Sofar NEW protocol (SM2E/ZM2E/etc) ===");
        LOGI("=======================================================");
        LOGI("IP: %s, DongleSN: %s", ipAddress.c_str(), dongleSN.c_str());
        
        // Validate SN length - should be max 10 digits for uint32_t
        if (dongleSN.length() > 10) {
            LOGE("Dongle SN '%s' is too long (%d chars). Max 10 digits allowed. Truncating.", 
                  dongleSN.c_str(), dongleSN.length());
            dongleSN = dongleSN.substring(0, 10);
        }
        
        uint32_t sn = strtoul(dongleSN.c_str(), NULL, 10);
        
        // Check for overflow
        if (sn == ULONG_MAX || sn == 0) {
            LOGE("Invalid SN conversion: '%s' -> %lu. Check SN format.", dongleSN.c_str(), sn);
        }
        
        LOGI("NEW protocol - Dongle SN (numeric): %lu", sn);
        inverterData.sn = dongleSN;
        byte packetBuffer[1024];

        channel.ensureIPAddress(ipAddress);

        // PV Input
        LOGI("NEW protocol: Reading PV input from 0x586...");
        if (!channel.tryReadWithRetries(0x586, 0x58F - 0x586 + 1, sn, packetBuffer, [&]()
                                {
            inverterData.millis = millis();
            inverterData.pv1Power = channel.readUInt16(packetBuffer, 0) * 10;  // 0x586
            inverterData.pv2Power = channel.readUInt16(packetBuffer, 3) * 10;  // 0x589
            inverterData.pv3Power = channel.readUInt16(packetBuffer, 6) * 10;  // 0x58C
            inverterData.pv4Power = channel.readUInt16(packetBuffer, 9) * 10;  // 0x58F
            LOGI("NEW: PV1=%dW, PV2=%dW, PV3=%dW, PV4=%dW", 
                 inverterData.pv1Power, inverterData.pv2Power, inverterData.pv3Power, inverterData.pv4Power);
            })) {
            LOGE("NEW protocol: FAILED to read PV input from 0x586");
            return inverterData;
        }

        // Battery Input
        LOGI("NEW protocol: Reading battery from 0x667...");
        if (!channel.tryReadWithRetries(0x667, 2, sn, packetBuffer, [&]()
                                {
            inverterData.batteryPower = channel.readInt16(packetBuffer, 0) * 100;
            inverterData.soc = channel.readUInt16(packetBuffer, 1);
            LOGI("NEW: Battery Power=%dW, SOC=%d%% (regs 0x667, 0x668)", inverterData.batteryPower, inverterData.soc);
            })) {
            LOGE("NEW protocol: FAILED to read battery from 0x667");
            return inverterData;
        }

        // Battery Temperature
        LOGI("NEW protocol: Reading battery temp from 0x607...");
        channel.tryReadWithRetries(0x607, 1, sn, packetBuffer, [&]()
                           { 
            inverterData.batteryTemperature = channel.readInt16(packetBuffer, 0);
            LOGI("NEW: Battery Temp=%d°C (reg 0x607)", inverterData.batteryTemperature);
            });

        // Inverter Temperature
        LOGI("NEW protocol: Reading inverter temp from 0x418...");
        channel.tryReadWithRetries(0x418, 1, sn, packetBuffer, [&]()
                           { 
            inverterData.inverterTemperature = channel.readInt16(packetBuffer, 0);
            LOGI("NEW: Inverter Temp=%d°C (reg 0x418)", inverterData.inverterTemperature);
            });

        // Grid Input - registry z dokumentace Sofar (0x0480-0x04FF)
        // 0x485 = ActivePower_Output_Total, 0x488 = ActivePower_PCC_Total (SmartMeter)
        // 0x48F/0x49A/0x4A5 = ActivePower_Output per phase (L1/L2/L3)
        // 0x493/0x49E/0x4A9 = ActivePower_PCC per phase (SmartMeter L1/L2/L3)
        LOGI("NEW protocol: Reading grid from 0x484...");
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
                LOGI("NEW: Single-phase: Output=%dW, Grid=%dW, Load=%dW", inverterOutputTotal, gridPowerTotal, inverterData.loadPower);
            } else {
                LOGI("NEW: Three-phase: Output L1=%d L2=%d L3=%d, Grid L1=%d L2=%d L3=%d",
                     inverterData.inverterOutpuPowerL1, inverterData.inverterOutpuPowerL2, inverterData.inverterOutpuPowerL3,
                     inverterData.gridPowerL1, inverterData.gridPowerL2, inverterData.gridPowerL3);
            }
            
            // Load power
            inverterData.loadPower = channel.readInt16(packetBuffer, 0x04AF - 0x484) * 10; 
            })) {
            LOGE("NEW protocol: FAILED to read grid from 0x484");
            return inverterData;
        }

        // Stats
        LOGI("NEW protocol: Reading stats from 0x684...");
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
            LOGI("NEW: Stats - PVtoday=%.2f, GridBuy=%.2f, GridSell=%.2f kWh", 
                 inverterData.pvToday, inverterData.gridBuyToday, inverterData.gridSellToday);
            inverterData.status = DONGLE_STATUS_OK; })) {
            LOGE("NEW protocol: FAILED to read stats from 0x684");
            return inverterData;
        }

        // Read RTC time from SofarSolar inverter
        readInverterRTC(sn, inverterData);
        
        // Read current work mode (Passive Mode parameters)
        readWorkMode(sn, inverterData);

        inverterData.hasBattery = inverterData.soc != 0 || inverterData.batteryPower != 0;
        
        LOGI("=======================================================");
        LOGI("=== NEW PROTOCOL READ COMPLETE ===");
        LOGI("Status: OK, SOC=%d%%, BatTemp=%d°C, InvTemp=%d°C", 
             inverterData.soc, inverterData.batteryTemperature, inverterData.inverterTemperature);
        LOGI("Battery=%dW, Grid=%dW, Load=%dW",
             inverterData.batteryPower, inverterData.gridPowerL1, inverterData.loadPower);
        LOGI("PV: %dW + %dW + %dW + %dW",
             inverterData.pv1Power, inverterData.pv2Power, inverterData.pv3Power, inverterData.pv4Power);
        LOGI("=======================================================");
        
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