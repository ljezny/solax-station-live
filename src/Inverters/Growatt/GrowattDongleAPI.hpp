#pragma once

#include "../../Protocol/ModbusTCP.hpp"
#include <WiFi.h>
#include "../InverterResult.hpp"

class GrowattDongleAPI
{
public:
    GrowattDongleAPI() : ip(0, 0, 0, 0) {}

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
        data.pv1Power = response.readUInt32(5) / 10;
        data.pv2Power = response.readUInt32(9) / 10;
        data.pv3Power = response.readUInt32(13) / 10;
        data.pv4Power = response.readUInt32(17) / 10;// + response.readUInt32(21) / 10 + response.readUInt32(25) / 10 + response.readUInt32(29) / 10 + response.readUInt32(33) / 10 + response.readUInt32(37) / 10;
        data.inverterTemperature = response.readInt16(93) / 10;
        data.pvToday = response.readUInt32(53) / 10.0;
        data.pvTotal = response.readUInt32(55) / 10.0;
        data.L1Power = response.readUInt32(35) / 10.0;
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
        data.soc = response.readInt16(1014);
        // Battery power: Pcharge (1011) - Pdischarge (1009)
        // Positive = charging (to battery), Negative = discharging (from battery)
        data.batteryPower = response.readInt32(1011) / 10 - response.readInt32(1009) / 10;
        data.batteryTemperature = response.readInt16(1040) / 10;
        data.batteryChargedToday = response.readUInt32(1056) / 10.0;
        data.batteryDischargedToday = response.readUInt32(1052) / 10.0;
        data.batteryChargedTotal = response.readUInt32(1058) / 10.0;
        data.batteryDischargedTotal = response.readUInt32(1054) / 10.0;
        // Register 1037 contains only inverter power to load, not total house consumption
        // Total load = inverter output - grid power (negative grid = import adds to consumption)
        // We calculate it after gridPowerL1 is set below
        data.loadToday = response.readUInt32(1060) / 10.0;
        data.loadTotal = response.readUInt32(1062) / 10.0;
        data.gridSellToday = response.readUInt32(1048) / 10.0;
        data.gridSellTotal = response.readUInt32(1050) / 10.0;
        data.gridBuyToday = response.readUInt32(1044) / 10.0;
        data.gridBuyTotal = response.readUInt32(1046) / 10.0;
        
        // Grid power calculation: Pactouser (1021) - Pactogrid (1029)
        // Negative = import from grid, Positive = export to grid
        data.gridPowerL1 = response.readInt32(1029) / 10.0 - response.readInt32(1021) / 10.0;

        
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
        data.sn = response.readString(23 , 10);
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
};