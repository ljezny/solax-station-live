#pragma once

#include "../../Protocol/ModbusTCP.hpp"
#include <WiFi.h>
#include "../InverterResult.hpp"

class SolaxModbusDongleAPI {
public:
    SolaxModbusDongleAPI() : isSupportedDongle(true), ip(0, 0, 0, 0) {}

    InverterData_t loadData(const String& ipAddress) {
        InverterData_t inverterData = {};
        inverterData.millis = millis();

        if (!isSupportedDongle) {
            inverterData.status = DONGLE_STATUS_UNSUPPORTED_DONGLE;
            log_d("Unsupported dongle");
            return inverterData;
        }

        if (!connectToDongle(ipAddress)) {
            inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
            return inverterData;
        }
        if (!readInverterInfo(inverterData) ||
            !readMainInverterData(inverterData) ||
            !readPowerData(inverterData) ||
            !readPhaseData(inverterData) ||
            !readPV3Power(inverterData)) {
            inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
            channel.disconnect();
            return inverterData;
        }

        finalizePowerCalculations(inverterData);
        logInverterData(inverterData);
        channel.disconnect();
        return inverterData;
    }

protected:
    ModbusTCP channel;
    bool isSupportedDongle;
    IPAddress ip;
    String sn = "";
private:
    static constexpr uint16_t MODBUS_PORT = 502;
    static constexpr uint8_t UNIT_ID = 1;
    static constexpr uint8_t FUNCTION_CODE_READ_HOLDING = 0x03;
    static constexpr uint8_t FUNCTION_CODE_READ_INPUT = 0x04;

    bool connectToDongle(const String& ipAddress) {
        IPAddress targetIp = getIp(ipAddress);
        if (!channel.connect(targetIp, MODBUS_PORT)) {
            log_d("Failed to connect to Solax Modbus dongle at %s", targetIp.toString().c_str());
            ip = IPAddress(0, 0, 0, 0);
            channel.disconnect();
            return false;
        }
        return true;
    }

    bool readInverterInfo(InverterData_t& data) {
        if(!sn.isEmpty()) {
            data.sn = sn;
            return true;
        }
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_HOLDING, 0x00, 0x14);
        if (!response.isValid) {
            log_d("Failed to read inverter info");
            return false;
        }

        data.status = DONGLE_STATUS_OK;
        sn = response.readString(0x00, 14);
        String factoryName = response.readString(0x07, 14);
        String moduleName = response.readString(0x0E, 14);
        log_d("SN: %s, Factory: %s, Module: %s", 
              sn.c_str(), factoryName.c_str(), moduleName.c_str());
        return true;
    }

    bool readMainInverterData(InverterData_t& data) {
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_INPUT, 0x00, 0x54 + 1);
        if (!response.isValid) {
            log_d("Failed to read main inverter data");
            return false;
        }
        data.status = DONGLE_STATUS_OK;
        data.inverterPower = response.readInt16(0x02);
        data.pv1Power = response.readUInt16(0x0A);
        data.pv2Power = response.readUInt16(0x0B);
        data.inverterTemperature = response.readInt16(0x08);
        if (isGen5(data.sn)) {
            data.inverterTemperature /= 10;
        }
        data.soc = response.readUInt16(0x1C);
        data.batteryPower = response.readInt16(0x16);
        data.batteryVoltage = response.readInt16(0x14) / 10.0f;
        data.feedInPower = response.readInt32LSB(0x46);
        data.batteryChargedToday = response.readUInt16(0x23) / 10.0f;
        data.batteryDischargedToday = response.readUInt16(0x20) / 10.0f;
        data.batteryTemperature = response.readInt16(0x18);
        data.gridBuyTotal = response.readUInt32LSB(0x4A) / 100.0f;
        data.gridSellTotal = response.readUInt32LSB(0x48) / 100.0f;
        data.pvTotal = response.readUInt32LSB(0x52) / 10.0f;
        data.loadToday = response.readUInt16(0x50) / 10.0f;
        return true;
    }

    bool readPowerData(InverterData_t& data) {
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_INPUT, 0x91, 0x9A - 0x91 + 2);
        if (!response.isValid) {
            log_d("Failed to read power data");
            return false;
        }

        data.gridBuyToday = response.readUInt32LSB(0x98) / 100.0f;
        data.gridSellToday = response.readUInt32LSB(0x9A) / 100.0f;
        data.pvToday = response.readUInt16(0x96) / 10.0f;
        return true;
    }

    bool readPhaseData(InverterData_t& data) {
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_INPUT, 0x6A, 0x84 - 0x6A + 2);
        if (!response.isValid) {
            log_d("Failed to read phase data");
            return false;
        }

        data.L1Power = response.readInt16(0x6C);
        data.L2Power = response.readInt16(0x70);
        data.L3Power = response.readInt16(0x74);
        
        uint16_t offgridL1Power = response.readInt16(0x78);
        uint16_t offgridL2Power = response.readInt16(0x7C);
        uint16_t offgridL3Power = response.readInt16(0x80);
        
        data.L1Power += offgridL1Power;
        data.L2Power += offgridL2Power;
        data.L3Power += offgridL3Power;
        return true;
    }

    bool readPV3Power(InverterData_t& data) {
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_INPUT, 0x0124, 1);
        if (!response.isValid) {
            log_d("Failed to read PV3 power");
            return false;
        }

        data.pv3Power = response.readInt16(0x0124);
        return true;
    }

    void finalizePowerCalculations(InverterData_t& data) {
        data.inverterPower = data.L1Power + data.L2Power + data.L3Power;
        data.loadPower = data.inverterPower - data.feedInPower;
        data.loadToday += data.gridBuyToday - data.gridSellToday;
    }

    IPAddress getIp(const String& ipAddress) {
        if (ip == IPAddress(0, 0, 0, 0)) {
            if (!ipAddress.isEmpty()) {
                ip = IPAddress();
                ip.fromString(ipAddress);
            }
        }

        if (ip == IPAddress(0, 0, 0, 0)) {
            ip = discoverIpViaMDNS();
        }

        if (ip == IPAddress(0, 0, 0, 0)) {
            ip = (WiFi.localIP()[0] == 192) ? 
                 IPAddress(192, 168, 10, 10) : 
                 IPAddress(5, 8, 8, 8);
        }

        log_d("Using IP: %s", ip.toString().c_str());
        return ip;
    }

    IPAddress discoverIpViaMDNS() {
        mdns_result_t* results = nullptr;
        mdns_init();
        esp_err_t err = mdns_query_ptr("_pocketseries", "_tcp", 5000, 20, &results);
        if (err != ESP_OK || !results) {
            log_d("MDNS query failed or no results");
            mdns_query_results_free(results);
            return IPAddress(0, 0, 0, 0);
        }

        IPAddress foundIp(0, 0, 0, 0);
        for (mdns_result_t* r = results; r; r = r->next) {
            log_d("Found MDNS: %s, type: %s, proto: %s, hostname: %s, port: %d",
                  r->instance_name, r->service_type, r->proto, r->hostname, r->port);
            foundIp = r->addr->addr.u_addr.ip4.addr;
            break;
        }
        mdns_query_results_free(results);
        mdns_free();
        return foundIp;
    }

    bool isGen5(const String& sn) {
        return sn.startsWith("H35") || sn.startsWith("H3B");
    }
};