#pragma once

#include "../../Protocol/ModbusTCP.hpp"
#include <WiFi.h>
#include "../InverterResult.hpp"

class GrowattDongleAPI
{
public:
    GrowattDongleAPI() : ip(0, 0, 0, 0) {}

    /**
     * Returns true if this inverter supports intelligence mode control
     */
    bool supportsIntelligence() { return false; }

    InverterData_t loadData(const String &ipAddress)
    {
        InverterData_t inverterData = {};
        inverterData.millis = millis();

        if (!connectToDongle(ipAddress))
        {
            inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
            return inverterData;
        }

        // Storage(SPH Type)：03 register range：0~124,1000~1124；04 register range：0~124,1000~1124
        if (!readHoldingData1(inverterData) || !readInputData1(inverterData) || !readInputData2(inverterData))
        {
            inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
            channel.disconnect();
            return inverterData;
        }
        
        // Try to read battery power limits (optional, won't fail if not available)
        readBatteryLimits(inverterData);
        
        // Try to read RTC time (optional, won't fail if not available)
        readInverterRTC(inverterData);

        logInverterData(inverterData);
        channel.disconnect();
        return inverterData;
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

    bool readInputData1(InverterData_t &data)
    {
        const int baseAddress = 5;
        log_d("Reading Growatt input PV data from address %d", baseAddress);
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_INPUT, baseAddress, 93 - baseAddress + 1);
        if (!response.isValid)
        {
            log_d("Failed to input PV data");
            return false;
        }
        log_d("Growatt input PV data read successfully");
        int32_t pv1PowerVal = response.readInt32(5);
        log_d("Register 5: %d (int32_t)", pv1PowerVal);
        data.pv1Power = (pv1PowerVal > 0 ? pv1PowerVal : 0) / 10;
        int32_t pv2PowerVal = response.readInt32(9);
        log_d("Register 9: %d (int32_t)", pv2PowerVal);
        data.pv2Power = (pv2PowerVal > 0 ? pv2PowerVal : 0) / 10;
        
        int16_t inverterTempVal = response.readInt16(93);
        log_d("Register 93: %d (int16_t)", inverterTempVal);
        data.inverterTemperature = inverterTempVal / 10;
        uint32_t pvTodayVal = response.readUInt32(53);
        log_d("Register 53: %u (uint32_t)", pvTodayVal);
        data.pvToday = pvTodayVal / 10.0;
        uint32_t pvTotalVal = response.readUInt32(55);
        log_d("Register 55: %u (uint32_t)", pvTotalVal);
        data.pvTotal = pvTotalVal / 10.0;
        uint32_t L1PowerVal = response.readUInt32(35);
        log_d("Register 35: %u (uint32_t)", L1PowerVal);
        data.L1Power = L1PowerVal / 10.0;
        // data.L1Power = response.readInt32(40) / 10;
        // data.L2Power = response.readInt32(44) / 10;
        // data.L3Power = response.readInt32(48) / 10;
        return true;
    }

    bool readInputData2(InverterData_t &data)
    {
        //1000 - 1124, but we read we need
        const int baseAddress = 1009;
        log_d("Reading Growatt input storage data from address %d", baseAddress);
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_INPUT, baseAddress, 1070 - baseAddress + 1);
        if (!response.isValid)
        {
            log_d("Failed to read Growatt input storage data");
            return false;
        }
        log_d("Growatt input storage data read successfully");
        data.status = DONGLE_STATUS_OK;
        int16_t socVal = response.readInt16(1014);
        log_d("Register 1014: %d (int16_t)", socVal);
        data.soc = socVal;
        // Battery power: Pcharge (1011) - Pdischarge (1009)
        // Positive = charging (to battery), Negative = discharging (from battery)
        int32_t pChargeVal = response.readInt32(1011);
        log_d("Register 1011: %d (int32_t)", pChargeVal);
        int32_t pDischargeVal = response.readInt32(1009);
        log_d("Register 1009: %d (int32_t)", pDischargeVal);
        data.batteryPower = pChargeVal / 10 - pDischargeVal / 10;
        int16_t batteryTempVal = response.readInt16(1040);
        log_d("Register 1040: %d (int16_t)", batteryTempVal);
        data.batteryTemperature = batteryTempVal / 10;
        uint32_t batteryChargedTodayVal = response.readUInt32(1056);
        log_d("Register 1056: %u (uint32_t)", batteryChargedTodayVal);
        data.batteryChargedToday = batteryChargedTodayVal / 10.0;
        uint32_t batteryDischargedTodayVal = response.readUInt32(1052);
        log_d("Register 1052: %u (uint32_t)", batteryDischargedTodayVal);
        data.batteryDischargedToday = batteryDischargedTodayVal / 10.0;
        uint32_t batteryChargedTotalVal = response.readUInt32(1058);
        log_d("Register 1058: %u (uint32_t)", batteryChargedTotalVal);
        data.batteryChargedTotal = batteryChargedTotalVal / 10.0;
        uint32_t batteryDischargedTotalVal = response.readUInt32(1054);
        log_d("Register 1054: %u (uint32_t)", batteryDischargedTotalVal);
        data.batteryDischargedTotal = batteryDischargedTotalVal / 10.0;
        // Register 1037 contains only inverter power to load, not total house consumption
        // Total load = inverter output - grid power (negative grid = import adds to consumption)
        // We calculate it after gridPowerL1 is set below
        uint32_t loadTodayVal = response.readUInt32(1060);
        log_d("Register 1060: %u (uint32_t)", loadTodayVal);
        data.loadToday = loadTodayVal / 10.0;
        uint32_t loadTotalVal = response.readUInt32(1062);
        log_d("Register 1062: %u (uint32_t)", loadTotalVal);
        data.loadTotal = loadTotalVal / 10.0;
        uint32_t gridSellTodayVal = response.readUInt32(1048);
        log_d("Register 1048: %u (uint32_t)", gridSellTodayVal);
        data.gridSellToday = gridSellTodayVal / 10.0;
        uint32_t gridSellTotalVal = response.readUInt32(1050);
        log_d("Register 1050: %u (uint32_t)", gridSellTotalVal);
        data.gridSellTotal = gridSellTotalVal / 10.0;
        uint32_t gridBuyTodayVal = response.readUInt32(1044);
        log_d("Register 1044: %u (uint32_t)", gridBuyTodayVal);
        data.gridBuyToday = gridBuyTodayVal / 10.0;
        uint32_t gridBuyTotalVal = response.readUInt32(1046);
        log_d("Register 1046: %u (uint32_t)", gridBuyTotalVal);
        data.gridBuyTotal = gridBuyTotalVal / 10.0;
        
        // Grid power calculation: Pactouser (1021) - Pactogrid (1029)
        // Negative = import from grid, Positive = export to grid
        int32_t pacToUserVal = response.readInt32(1021);
        log_d("Register 1021: %d (int32_t)", pacToUserVal);
        int32_t pacToGridVal = response.readInt32(1029);
        log_d("Register 1029: %d (int32_t)", pacToGridVal);
        data.gridPowerL1 = pacToGridVal / 10.0 - pacToUserVal / 10.0;

        
        // Calculate total house consumption: inverter output minus grid export (or plus grid import)
        data.loadPower = data.L1Power - data.gridPowerL1;
        
        // data.gridPowerL1 = response.readInt32(1023) / 10.0 - response.readInt32(1015) / 10.0;
        // data.gridPowerL2 = response.readInt32(1025) / 10.0 - response.readInt32(1017) / 10.0;
        // data.gridPowerL3 = response.readInt32(1027) / 10.0 - response.readInt32(1019) / 10.0;
        //data.batteryCapacityWh = response.readUInt16(1107);
        return true;
    }

    bool readBatteryLimits(InverterData_t &data)
    {
        // Try to read BMS charge/discharge power from registers 3331 and 3334
        // These are INPUT registers with scale 0.1W
        const int baseAddress = 3331;
        log_d("Reading Growatt BMS power limits from address %d", baseAddress);
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_INPUT, baseAddress, 4);
        if (!response.isValid)
        {
            log_d("Failed to read Growatt BMS power limits (optional)");
            return false;
        }
        log_d("Growatt BMS power limits read successfully");
        
        // Register 3331-3332: BMS 1 Charge Power (U32, scale 0.1W)
        uint32_t chargePowerVal = response.readUInt32(3331);
        data.maxChargePowerW = (uint16_t)(chargePowerVal / 10);
        
        // Register 3334-3335: BMS 1 Discharge Power (U32, scale 0.1W)
        uint32_t dischargePowerVal = response.readUInt32(3334);
        data.maxDischargePowerW = (uint16_t)(dischargePowerVal / 10);
        
        log_d("Growatt max charge power: %d W, max discharge power: %d W", data.maxChargePowerW, data.maxDischargePowerW);
        return true;
    }

    bool readInverterRTC(InverterData_t &data)
    {
        // Growatt RTC registers: 45-50 (holding registers)
        // 45: Year (2000-based), 46: Month, 47: Day, 48: Hour, 49: Minute, 50: Second
        const int baseAddress = 45;
        log_d("Reading Growatt RTC from address %d", baseAddress);
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_HOLDING, baseAddress, 6);
        if (!response.isValid)
        {
            log_d("Failed to read Growatt RTC (optional)");
            return false;
        }

        struct tm timeinfo = {};
        timeinfo.tm_year = response.readUInt16(45) + 100;  // Year-2000 + 100 = years since 1900
        timeinfo.tm_mon = response.readUInt16(46) - 1;     // Month 1-12 to 0-11
        timeinfo.tm_mday = response.readUInt16(47);
        timeinfo.tm_hour = response.readUInt16(48);
        timeinfo.tm_min = response.readUInt16(49);
        timeinfo.tm_sec = response.readUInt16(50);
        timeinfo.tm_isdst = -1;

        data.inverterTime = mktime(&timeinfo);
        log_d("Growatt RTC: %04d-%02d-%02d %02d:%02d:%02d",
              timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
              timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        return true;
    }

    bool readHoldingData1(InverterData_t &data)
    {
        if(sn != "")
        {
            data.sn = sn;
            return true;
        }

        //0 - 125, but we read we need
        const int baseAddress = 23;
        log_d("Reading Growatt holding inverter data from address %d", baseAddress);
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_HOLDING, baseAddress, 10);
        if (!response.isValid)
        {
            log_d("Failed to read Growatt holding inverter data");
            return false;
        }
        log_d("Growatt holding inverter data read successfully");
        data.status = DONGLE_STATUS_OK;
        String snVal = response.readString(23 , 10);
        log_d("Register 23-32: %s (string)", snVal.c_str());
        data.sn = snVal;
        sn = data.sn;
        return true;
    }

    bool readHoldingData2(InverterData_t &data)
    {
        const int baseAddress = 1000;
        log_d("Reading Growatt holding inverter data from address %d", baseAddress);
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_HOLDING, baseAddress, 125);
        if (!response.isValid)
        {
            log_d("Failed to read Growatt holding inverter data");
            return false;
        }
        log_d("Growatt holding inverter data read successfully");
        data.status = DONGLE_STATUS_OK;

        return true;
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
            ip = IPAddress(192, 168, 10, 100); // this is default for Growatt dongle when in AP mode
        }

        log_d("Using IP: %s", ip.toString().c_str());
        return ip;
    }

    /**
     * Sets the work mode of the Growatt inverter for intelligent battery control
     * 
     * Growatt SPH Work Modes (register 1044/1110):
     * 0 = Load First (Self-Use)
     * 1 = Battery First
     * 2 = Grid First
     * 
     * For force charge/discharge, Growatt uses:
     * - Register 1100: Priority Mode (0=LoadFirst, 1=BatFirst, 2=GridFirst)
     * - Register 1044: System Mode (0=SelfUse, 1=ForceCharge, 2=ForceDischarge)
     * - Register 1070-1091: Time of Use settings
     * 
     * @param ipAddress IP address of the dongle
     * @param mode Desired inverter mode
     * @return true if mode was set successfully
     */
    bool setWorkMode(const String& ipAddress, InverterMode_t mode)
    {
        if (!connectToDongle(ipAddress))
        {
            log_d("Failed to connect for setWorkMode");
            return false;
        }

        log_d("Setting Growatt work mode to %d", mode);
        bool success = false;

        switch (mode)
        {
        case INVERTER_MODE_SELF_USE:
            // Load First mode - register 1100 = 0
            success = channel.writeSingleRegister(UNIT_ID, GROWATT_REG_PRIORITY_MODE, GROWATT_PRIORITY_LOAD_FIRST);
            if (success)
            {
                // Also set system mode to Self-Use
                channel.writeSingleRegister(UNIT_ID, GROWATT_REG_SYSTEM_MODE, GROWATT_SYSTEM_SELF_USE);
            }
            break;

        case INVERTER_MODE_HOLD_BATTERY:
            // Battery First mode - keeps battery from discharging
            success = channel.writeSingleRegister(UNIT_ID, GROWATT_REG_PRIORITY_MODE, GROWATT_PRIORITY_BAT_FIRST);
            break;

        case INVERTER_MODE_CHARGE_FROM_GRID:
            // Force Charge mode
            success = channel.writeSingleRegister(UNIT_ID, GROWATT_REG_SYSTEM_MODE, GROWATT_SYSTEM_FORCE_CHARGE);
            break;

        case INVERTER_MODE_DISCHARGE_TO_GRID:
            // Grid First + allow export
            success = channel.writeSingleRegister(UNIT_ID, GROWATT_REG_PRIORITY_MODE, GROWATT_PRIORITY_GRID_FIRST);
            if (success)
            {
                // Enable battery discharge (set low SOC limit)
                channel.writeSingleRegister(UNIT_ID, GROWATT_REG_DISCHARGE_SOC_LIMIT, 10);  // 10%
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
    // Growatt SPH Modbus register addresses
    static constexpr uint16_t GROWATT_REG_PRIORITY_MODE = 1100;      // Priority Mode (holding)
    static constexpr uint16_t GROWATT_REG_SYSTEM_MODE = 1044;        // System Mode
    static constexpr uint16_t GROWATT_REG_DISCHARGE_SOC_LIMIT = 1071; // Min SOC for discharge
    static constexpr uint16_t GROWATT_REG_CHARGE_SOC_LIMIT = 1072;   // Max SOC for charge
    
    // Growatt Priority Mode values
    static constexpr uint16_t GROWATT_PRIORITY_LOAD_FIRST = 0;       // Load First (Self-Use)
    static constexpr uint16_t GROWATT_PRIORITY_BAT_FIRST = 1;        // Battery First
    static constexpr uint16_t GROWATT_PRIORITY_GRID_FIRST = 2;       // Grid First
    
    // Growatt System Mode values
    static constexpr uint16_t GROWATT_SYSTEM_SELF_USE = 0;           // Self-Use
    static constexpr uint16_t GROWATT_SYSTEM_FORCE_CHARGE = 1;       // Force Charge
    static constexpr uint16_t GROWATT_SYSTEM_FORCE_DISCHARGE = 2;    // Force Discharge
};