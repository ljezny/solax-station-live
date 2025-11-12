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
        if (!readHoldingData1(inverterData) || !readHoldingData2(inverterData) || !readInputData1(inverterData) || !readInputData2(inverterData))
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
        const int baseAddress = 0;
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_INPUT, baseAddress, 125);
        if (!response.isValid)
        {
            log_d("Failed to read main inverter data");
            return false;
        }
        data.status = DONGLE_STATUS_OK;
        // data.pv1Power = response.readUInt32(3005 - baseAddress) / 10;
        // data.pv2Power = response.readUInt32(3009 - baseAddress) / 10;
        // data.pv3Power = response.readUInt32(3013 - baseAddress) / 10;
        // data.pv4Power = response.readUInt32(3017 - baseAddress) / 10;
        // data.inverterTemperature = response.readInt16(3093 - baseAddress) / 10;
        // data.pvToday = response.readUInt32(3055 - baseAddress) / 10.0 + response.readUInt32(3059 - baseAddress) / 10.0 + response.readUInt32(3063 - baseAddress) / 10.0 + response.readUInt32(3067 - baseAddress) / 10.0;
        // data.pvTotal = response.readUInt32(3053 - baseAddress) / 10.0;
        // data.L1Power = response.readInt32(3028 - baseAddress) / 10;
        // data.L2Power = response.readInt16(3032 - baseAddress) / 10;
        // data.L3Power = response.readInt16(3036 - baseAddress) / 10;
        // data.inverterPower = data.L1Power + data.L2Power + data.L3Power;
        // data.loadPower = response.readInt32(3045 - baseAddress) / 10;
        // data.loadToday = response.readUInt32(3075 - baseAddress) / 10.0;
        // data.loadTotal = response.readUInt32(3077 - baseAddress) / 10.0;
        // data.gridSellToday = response.readUInt32(3071 - baseAddress) / 10.0;
        // data.gridSellTotal = response.readUInt32(3073 - baseAddress) / 10.0;
        // data.gridBuyToday = response.readUInt32(3067 - baseAddress) / 10.0;
        // data.gridBuyTotal = response.readUInt32(3069 - baseAddress) / 10.0;
        // data.gridPower = response.readInt32(3043 - baseAddress) / 10.0 - response.readInt32(3041 - baseAddress) / 10.0;

        return true;
    }

    bool readInputData2(InverterData_t &data)
    {
        const int baseAddress = 1000;
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_INPUT, baseAddress, 125);
        if (!response.isValid)
        {
            log_d("Failed to read main inverter data");
            return false;
        }
        data.status = DONGLE_STATUS_OK;
        data.soc = response.readInt16(1014 - baseAddress);
        data.batteryPower = response.readInt32(1011 - baseAddress) / 10 - response.readInt32(1009 - baseAddress) / 10;
        data.batteryTemperature = response.readInt16(1040 - baseAddress) / 10;
        // data.batteryChargedToday = response.readUInt32(3129 - baseAddress) / 10.0;
        // data.batteryDischargedToday = response.readUInt32(3125 - baseAddress) / 10.0;
        // data.batteryChargedTotal = response.readUInt32(3131 - baseAddress) / 10.0;
        // data.batteryDischargedTotal = response.readUInt32(3127 - baseAddress) / 10.0;

        return true;
    }

    bool readHoldingData1(InverterData_t &data)
    {
        const int baseAddress = 0;
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_HOLDING, baseAddress, 125);
        if (!response.isValid)
        {
            log_d("Failed to read main inverter data");
            return false;
        }
        data.status = DONGLE_STATUS_OK;
        data.sn = response.readString(23 - baseAddress, 10);
        return true;
    }

    bool readHoldingData2(InverterData_t &data)
    {
        const int baseAddress = 1000;
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_HOLDING, baseAddress, 125);
        if (!response.isValid)
        {
            log_d("Failed to read main inverter data");
            return false;
        }
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
            ip = discoverDongleIP();
        }

        log_d("Using IP: %s", ip.toString().c_str());
        return ip;
    }

    IPAddress discoverDongleIP()
    {
        /*IPAddress dongleIP;
        WiFiUDP udp;
        String message = "HF-A11ASSISTHREAD";
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
        return dongleIP;*/
        return IPAddress(192, 168, 10, 100); // this is default for Growatt dongle when in AP mode
    }
};