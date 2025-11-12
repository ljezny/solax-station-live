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

        if (!readInverterPVData(inverterData) ||
            !readInverterStorageData(inverterData))
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

    bool readInverterPVData(InverterData_t &data)
    {
        const int baseAddress = 0;
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_INPUT, baseAddress, 125);
        if (!response.isValid)
        {
            log_d("Failed to read main inverter data");
            return false;
        }
        data.status = DONGLE_STATUS_OK;
        data.pv1Power = response.readUInt32(5 - baseAddress) / 10;
        data.pv2Power = response.readUInt32(9 - baseAddress) / 10;
        data.pv3Power = response.readUInt32(13 - baseAddress) / 10;
        data.pv4Power = response.readUInt32(17 - baseAddress) / 10;
        data.inverterTemperature = response.readInt16(93 - baseAddress) / 10;
        data.pvToday = response.readUInt32(53 - baseAddress) / 10.0;
        data.pvTotal = response.readUInt32(55 - baseAddress) / 10.0;
        data.L1Power = response.readInt32(40 - baseAddress) / 10;
        data.L2Power = response.readInt16(44 - baseAddress) / 10;
        data.L3Power = response.readInt16(48 - baseAddress) / 10;
        data.inverterPower = data.L1Power + data.L2Power + data.L3Power;
        return true;
    }

    bool readInverterStorageData(InverterData_t &data)
    {
        const int baseAddress = 1000;
        ModbusResponse response = channel.sendModbusRequest(UNIT_ID, FUNCTION_CODE_READ_INPUT, baseAddress, 125);
        if (!response.isValid)
        {
            log_d("Failed to read main inverter data");
            return false;
        }
        data.status = DONGLE_STATUS_OK;
        data.soc = response.readInt16(10014 - baseAddress);
        
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
        IPAddress dongleIP;
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
        return dongleIP;
    }
};