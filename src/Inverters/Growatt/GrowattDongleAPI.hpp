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

    bool setWorkMode(const String& ipAddress, InverterMode_t mode)
    {
        // TODO: Not implemented yet
        return false;
    }
};