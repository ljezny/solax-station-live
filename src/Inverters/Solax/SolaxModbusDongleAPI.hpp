#pragma once

#include "../../Protocol/ModbusTCP.hpp"
#include <WiFi.h>
#include "../InverterResult.hpp"

class SolaxModbusDongleAPI
{
public:
    SolaxModbusDongleAPI() : isSupportedDongle(true), ip(0, 0, 0, 0) {}

    /**
     * Returns true if this inverter supports intelligence mode control
     */
    bool supportsIntelligence() { return true; }

    InverterData_t loadData(const String &ipAddress)
    {
        InverterData_t inverterData = {};
        inverterData.millis = millis();

        if (!isSupportedDongle)
        {
            inverterData.status = DONGLE_STATUS_UNSUPPORTED_DONGLE;
            log_d("Unsupported dongle");
            return inverterData;
        }

        if (!connectToDongle(ipAddress))
        {
            inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
            return inverterData;
        }
        if (!readInverterInfo(inverterData) ||
            !readMainInverterData(inverterData) ||
            !readPowerData(inverterData) ||
            !readPhaseData(inverterData) ||
            !readPV3Power(inverterData) ||
            !readWorkMode(inverterData))
        {
            inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
            channel.disconnect();
            return inverterData;
        }

        finalizePowerCalculations(inverterData);
        logInverterData(inverterData);
        channel.disconnect();
        return inverterData;
    }

    /**
     * Nastaví work mode střídače Solax
     * @param ipAddress IP adresa donglu
     * @param mode Požadovaný režim (InverterMode_t)
     * @return true pokud se nastavení podařilo
     */
    bool setWorkMode(const String &ipAddress, InverterMode_t mode)
    {
        if (!connectToDongle(ipAddress))
        {
            log_d("Failed to connect for setWorkMode");
            return false;
        }

        uint16_t solaxWorkMode, solaxManualMode;
        inverterModeToSolaxMode(mode, solaxWorkMode, solaxManualMode);

        // First unlock the inverter with Advanced unlock code (6868)
        bool success = channel.writeSingleRegister(UNIT_ID, REG_LOCK_STATE, LOCK_STATE_UNLOCKED_ADVANCED);
        if (!success)
        {
            log_d("Failed to unlock inverter");
            channel.disconnect();
            return false;
        }
        log_d("Inverter unlocked (Advanced mode)");

        // Set the work mode using WRITE register 0x1F (GEN4/GEN5/GEN6)
        success = channel.writeSingleRegister(UNIT_ID, REG_CHARGER_USE_MODE_WRITE, solaxWorkMode);
        
        // If switching to Manual mode, also set the manual mode register 0x20
        if (success && solaxWorkMode == SOLAX_WORK_MODE_MANUAL)
        {
            success = channel.writeSingleRegister(UNIT_ID, REG_MANUAL_MODE_WRITE, solaxManualMode);
        }
        
        // Note: We don't lock the inverter back - it auto-locks after timeout
        // Trying to lock immediately causes Modbus exception 4 (Slave Device Failure)
        
        channel.disconnect();
        
        if (success)
        {
            log_d("Successfully set work mode to %d (Solax WorkMode: %d, ManualMode: %d)", 
                  mode, solaxWorkMode, solaxManualMode);
        }
        else
        {
            log_d("Failed to set work mode to %d", mode);
        }
        
        return success;
    }

protected:
    bool isSupportedDongle;
    IPAddress ip;
    String sn = "";
    ModbusTCP channel;

private:
    static constexpr uint16_t MODBUS_PORT = 502;
    static constexpr uint8_t UNIT_ID = 1;
    static constexpr uint8_t FUNCTION_CODE_READ_HOLDING = 0x03;
    static constexpr uint8_t FUNCTION_CODE_READ_INPUT = 0x04;
    
    // Lock State register - must unlock before writing mode registers
    static constexpr uint16_t REG_LOCK_STATE = 0x0000;              // Lock State register
    static constexpr uint16_t LOCK_STATE_LOCKED = 0;                // Locked
    static constexpr uint16_t LOCK_STATE_UNLOCKED = 2014;           // Unlocked
    static constexpr uint16_t LOCK_STATE_UNLOCKED_ADVANCED = 6868;  // Unlocked - Advanced (required for mode changes)
    
    // Solax Work Mode register addresses
    // READ registers (INPUT registers 0x8B, 0x8C) - for reading current state
    static constexpr uint16_t REG_SOLAR_CHARGER_USE_MODE = 0x008B;  // SolarChargerUseMode - read current work mode
    static constexpr uint16_t REG_MANUAL_MODE_READ = 0x008C;        // Manual Mode register - for reading
    
    // WRITE registers (HOLDING registers 0x1F, 0x20) - for GEN4/GEN5/GEN6 inverters
    static constexpr uint16_t REG_CHARGER_USE_MODE_WRITE = 0x001F;  // Charger Use Mode - for writing (GEN4+)
    static constexpr uint16_t REG_MANUAL_MODE_WRITE = 0x0020;       // Manual Mode Select - for writing (GEN4+)
    
    // Solax Work Mode values (SolarChargerUseMode register 0x008B)
    static constexpr uint16_t SOLAX_WORK_MODE_SELF_USE = 0;         // Self use mode
    static constexpr uint16_t SOLAX_WORK_MODE_FEEDIN_PRIORITY = 1;  // Feedin Priority
    static constexpr uint16_t SOLAX_WORK_MODE_BACK_UP = 2;          // Back up mode
    static constexpr uint16_t SOLAX_WORK_MODE_MANUAL = 3;           // Manual mode
    
    // Manual Mode values (register 0x008C) - when in Manual mode (mode 3)
    static constexpr uint16_t SOLAX_MANUAL_STOP = 0;                // Stop charge & discharge
    static constexpr uint16_t SOLAX_MANUAL_FORCE_CHARGE = 1;        // Force charge
    static constexpr uint16_t SOLAX_MANUAL_FORCE_DISCHARGE = 2;     // Force discharge

    /**
     * Převede InverterMode_t na Solax work mode a manual mode hodnoty
     * @param mode Požadovaný režim
     * @param outWorkMode Výstup: hodnota pro registr 0x008B (SolarChargerUseMode)
     * @param outManualMode Výstup: hodnota pro registr 0x008C (ManualMode)
     */
    void inverterModeToSolaxMode(InverterMode_t mode, uint16_t &outWorkMode, uint16_t &outManualMode)
    {
        switch (mode)
        {
        case INVERTER_MODE_SELF_USE:
            outWorkMode = SOLAX_WORK_MODE_SELF_USE;
            outManualMode = SOLAX_MANUAL_STOP;
            break;
        case INVERTER_MODE_CHARGE_FROM_GRID:
            // Pro nabíjení ze sítě použijeme Manual mode s Force Charge
            outWorkMode = SOLAX_WORK_MODE_MANUAL;
            outManualMode = SOLAX_MANUAL_FORCE_CHARGE;
            break;
        case INVERTER_MODE_DISCHARGE_TO_GRID:
            // Pro prodej do sítě použijeme Manual mode s Force Discharge
            outWorkMode = SOLAX_WORK_MODE_MANUAL;
            outManualMode = SOLAX_MANUAL_FORCE_DISCHARGE;
            break;
        case INVERTER_MODE_HOLD_BATTERY:
            // Pro držení baterie použijeme Manual mode se Stop
            outWorkMode = SOLAX_WORK_MODE_MANUAL;
            outManualMode = SOLAX_MANUAL_STOP;
            break;
        default:
            outWorkMode = SOLAX_WORK_MODE_SELF_USE;
            outManualMode = SOLAX_MANUAL_STOP;
            break;
        }
    }

    /**
     * Převede Solax work mode a manual mode hodnoty na InverterMode_t
     * @param solaxMode Hodnota z registru 0x008B (SolarChargerUseMode)
     * @param manualMode Hodnota z registru 0x008C (ManualMode)
     */
    InverterMode_t solaxModeToInverterMode(uint16_t solaxMode, uint16_t manualMode = 0)
    {
        switch (solaxMode)
        {
        case SOLAX_WORK_MODE_SELF_USE:
            return INVERTER_MODE_SELF_USE;
        case SOLAX_WORK_MODE_FEEDIN_PRIORITY:
            return INVERTER_MODE_DISCHARGE_TO_GRID;
        case SOLAX_WORK_MODE_BACK_UP:
            return INVERTER_MODE_HOLD_BATTERY;
        case SOLAX_WORK_MODE_MANUAL:
            // V manual mode záleží na hodnotě manualMode registru
            switch (manualMode)
            {
            case SOLAX_MANUAL_FORCE_CHARGE:
                return INVERTER_MODE_CHARGE_FROM_GRID;
            case SOLAX_MANUAL_FORCE_DISCHARGE:
                return INVERTER_MODE_DISCHARGE_TO_GRID;
            case SOLAX_MANUAL_STOP:
            default:
                return INVERTER_MODE_HOLD_BATTERY;
            }
        default:
            return INVERTER_MODE_UNKNOWN;
        }
    }

    bool connectToDongle(const String &ipAddress)
    {
        IPAddress targetIp = getIp(ipAddress);
        if (!channel.connect(targetIp, MODBUS_PORT))
        {
            log_d("Failed to connect to Solax Modbus dongle at %s", targetIp.toString().c_str());
            ip = IPAddress(0, 0, 0, 0);
            channel.disconnect();
            return false;
        }
        return true;
    }

    bool readInverterInfo(InverterData_t &data)
    {
        if (!sn.isEmpty())
        {
            data.sn = sn;
            return true;
        }
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_HOLDING, 0x00, 0x14);
        if (!response.isValid)
        {
            log_d("Failed to read inverter info");
            return false;
        }

        data.status = DONGLE_STATUS_OK;
        sn = response.readString(0x00, 14);
        data.sn = sn;
        String factoryName = response.readString(0x07, 14);
        String moduleName = response.readString(0x0E, 14);
        log_d("SN: %s, Factory: %s, Module: %s",
              sn.c_str(), factoryName.c_str(), moduleName.c_str());
        return true;
    }

    bool readMainInverterData(InverterData_t &data)
    {
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_INPUT, 0x00, 0x54 + 1);
        if (!response.isValid)
        {
            log_d("Failed to read main inverter data");
            return false;
        }
        data.status = DONGLE_STATUS_OK;
        data.L1Power = response.readInt16(0x02);
        data.pv1Power = response.readUInt16(0x0A);
        data.pv2Power = response.readUInt16(0x0B);
        data.inverterTemperature = response.readInt16(0x08);
        if (isGen5(data.sn))
        {
            data.inverterTemperature /= 10;
        }
        data.soc = response.readUInt16(0x1C);
        data.batteryPower = response.readInt16(0x16);
        data.batteryVoltage = response.readInt16(0x14) / 10.0f;
        //data.gridPower = response.readInt32LSB(0x46); //read later from phases
        data.batteryChargedToday = response.readUInt16(0x23) / 10.0f;
        data.batteryDischargedToday = response.readUInt16(0x20) / 10.0f;
        data.batteryTemperature = response.readInt16(0x18);
        data.gridBuyTotal = response.readUInt32LSB(0x4A) / 100.0f;
        data.gridSellTotal = response.readUInt32LSB(0x48) / 100.0f;
        data.pvTotal = response.readUInt32LSB(0x52) / 10.0f;
        data.loadToday = response.readUInt16(0x50) / 10.0f;
        data.batteryCapacityWh = response.readUInt16(0x26);
        //log 0x26 register value for debugging
        log_d("Battery capacity (0x26): %d Wh", data.batteryCapacityWh);
        
        // Read BMS max charge/discharge current from registers 0x24 and 0x25
        // Scale is 0.1A, we convert to power using battery voltage
        float bmsChargeMaxCurrent = response.readUInt16(0x24) * 0.1f;  // Amperes
        float bmsDischargeMaxCurrent = response.readUInt16(0x25) * 0.1f;  // Amperes
        // Calculate power: P = U * I, use battery voltage if available
        float batteryVoltageForCalc = data.batteryVoltage > 0 ? data.batteryVoltage : 50.0f; // Default 50V for HV battery
        data.maxChargePowerW = (uint16_t)(bmsChargeMaxCurrent * batteryVoltageForCalc);
        data.maxDischargePowerW = (uint16_t)(bmsDischargeMaxCurrent * batteryVoltageForCalc);
        log_d("BMS max charge current: %.1f A, max discharge current: %.1f A", bmsChargeMaxCurrent, bmsDischargeMaxCurrent);
        log_d("Max charge power: %d W, max discharge power: %d W", data.maxChargePowerW, data.maxDischargePowerW);
        
        data.minSoc = 10;
        data.maxSoc = 100;
        return true;
    }

    bool readWorkMode(InverterData_t &data)
    {
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_HOLDING, REG_SOLAR_CHARGER_USE_MODE, 2);
        if (!response.isValid)
        {
            log_d("Failed to read work mode register");
            return false;
        }

        uint16_t solaxMode = response.readUInt16(REG_SOLAR_CHARGER_USE_MODE);
        uint16_t manualMode = response.readUInt16(REG_MANUAL_MODE_READ);
        data.inverterMode = solaxModeToInverterMode(solaxMode, manualMode);
        log_d("Read work mode: SolarChargerUseMode=%d, ManualMode=%d, InverterMode=%d", solaxMode, manualMode, data.inverterMode);
        return true;
    }

    bool readPowerData(InverterData_t &data)
    {
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_INPUT, 0x91, 0x9A - 0x91 + 2);
        if (!response.isValid)
        {
            log_d("Failed to read power data");
            return false;
        }

        data.gridBuyToday = response.readUInt32LSB(0x9A) / 100.0f;
        data.gridSellToday = response.readUInt32LSB(0x98) / 100.0f;
        data.pvToday = response.readUInt16(0x96) / 10.0f;
        return true;
    }

    bool readPhaseData(InverterData_t &data)
    {
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_INPUT, 0x6A, 0x88 - 0x6A + 2);
        if (!response.isValid)
        {
            log_d("Failed to read phase data");
            return false;
        }
        data.L1Power = response.readInt16(0x6C);
        data.L2Power = response.readInt16(0x70);
        data.L3Power = response.readInt16(0x74);

        //backup
        uint16_t backupL1Power = response.readInt16(0x78);
        uint16_t backupL2Power = response.readInt16(0x7C);
        uint16_t backupL3Power = response.readInt16(0x80);
        data.L1Power += backupL1Power;
        data.L2Power += backupL2Power;
        data.L3Power += backupL3Power;

        data.gridPowerL1 = response.readInt16(0x82);
        data.gridPowerL2 = response.readInt16(0x84);
        data.gridPowerL3 = response.readInt16(0x86);
        //data.gridPower = data.gridPowerL1 + data.gridPowerL2 + data.gridPowerL3;
        return true;
    }

    bool readPV3Power(InverterData_t &data)
    {
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_INPUT, 0x0124, 1);
        if (!response.isValid)
        {
            log_d("Failed to read PV3 power");
            return false;
        }

        data.pv3Power = response.readInt16(0x0124);
        return true;
    }

    void finalizePowerCalculations(InverterData_t &data)
    {
        //data.inverterPower = data.L1Power + data.L2Power + data.L3Power;
        data.loadPower = data.L1Power + data.L2Power + data.L3Power - (data.gridPowerL1 + data.gridPowerL2 + data.gridPowerL3);
        data.loadToday += data.gridBuyToday - data.gridSellToday;
    }

    IPAddress getIp(const String &ipAddress)
    {
        if (ip == IPAddress(0, 0, 0, 0))
        {
            if (!ipAddress.isEmpty())
            {
                ip = IPAddress();
                ip.fromString(ipAddress);
            }
        }

        if (ip == IPAddress(0, 0, 0, 0))
        {
            ip = discoverIpViaMDNS();
        }

        if (ip == IPAddress(0, 0, 0, 0))
        {
            ip = (WiFi.localIP()[0] == 192) ? IPAddress(192, 168, 10, 10) : IPAddress(5, 8, 8, 8);
        }

        log_d("Using IP: %s", ip.toString().c_str());
        return ip;
    }

    IPAddress discoverIpViaMDNS()
    {
        mdns_result_t *results = nullptr;
        mdns_init();
        esp_err_t err = mdns_query_ptr("_pocketseries", "_tcp", 5000, 20, &results);
        if (err != ESP_OK || !results)
        {
            log_d("MDNS query failed or no results");
            mdns_query_results_free(results);
            return IPAddress(0, 0, 0, 0);
        }

        IPAddress foundIp(0, 0, 0, 0);
        for (mdns_result_t *r = results; r; r = r->next)
        {
            log_d("Found MDNS: %s, type: %s, proto: %s, hostname: %s, port: %d",
                  r->instance_name, r->service_type, r->proto, r->hostname, r->port);
            foundIp = r->addr->addr.u_addr.ip4.addr;
            break;
        }
        mdns_query_results_free(results);
        mdns_free();
        return foundIp;
    }

    bool isGen5(const String &sn)
    {
        return sn.startsWith("H35") || sn.startsWith("H3B");
    }
};