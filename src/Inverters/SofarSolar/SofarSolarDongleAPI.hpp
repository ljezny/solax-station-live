#pragma once

#include "../../Protocol/V5TCP.hpp"

class SofarSolarDongleAPI
{
public:
    InverterData_t loadData(String ipAddress, String sn)
    {
        return readData(ipAddress, sn);
    }

private:
    V5TCP channel;
    IPAddress ip;
    static constexpr int MAX_RETRIES = 3;
    InverterData_t readData(String ipAddress, String dongleSN)
    {
        InverterData_t inverterData;
        inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
        uint32_t sn = strtoul(dongleSN.c_str(), NULL, 10);
        inverterData.sn = sn;
        byte packetBuffer[1024];

        ensureIPAddress(ipAddress);

        // PV Input
        if (!tryReadWithRetries(0x586, 0x58F - 0x586 + 1, sn, packetBuffer, [&]()
                                {
            inverterData.millis = millis();
            inverterData.pv1Power = channel.readUInt16(packetBuffer, 0) * 10;
            inverterData.pv2Power = channel.readUInt16(packetBuffer, 3) * 10;
            inverterData.pv3Power = channel.readUInt16(packetBuffer, 5) * 10;
            inverterData.pv4Power = channel.readUInt16(packetBuffer, 9) * 10; }))
            return inverterData;

        // Battery Input
        if (!tryReadWithRetries(0x667, 2, sn, packetBuffer, [&]()
                                {
            inverterData.batteryPower = channel.readInt16(packetBuffer, 0) * 100;
            inverterData.soc = channel.readUInt16(packetBuffer, 1); }))
            return inverterData;

        // Battery Temperature
        tryReadWithRetries(0x607, 1, sn, packetBuffer, [&]()
                           { inverterData.batteryTemperature = channel.readInt16(packetBuffer, 0); });

        // Inverter Temperature
        tryReadWithRetries(0x418, 1, sn, packetBuffer, [&]()
                           { inverterData.inverterTemperature = channel.readInt16(packetBuffer, 0); });

        // Grid Input
        if (!tryReadWithRetries(0x484, 0x4BC - 0x484 + 1, sn, packetBuffer, [&]()
                                {
            inverterData.inverterPower = channel.readInt16(packetBuffer, 0x485 - 0x484) * 10;
            inverterData.loadPower = channel.readInt16(packetBuffer, 0x04AF - 0x484) * 10;
            inverterData.feedInPower = channel.readInt16(packetBuffer, 0x0488 - 0x484) * 10;
            inverterData.L1Power = channel.readInt16(packetBuffer, 0x48F - 0x484) * 10;
            inverterData.L2Power = channel.readInt16(packetBuffer, 0x49A - 0x484) * 10;
            inverterData.L3Power = channel.readInt16(packetBuffer, 0x4A5 - 0x484) * 10; }))
            return inverterData;

        // Stats
        if (!tryReadWithRetries(0x684, 0x698 - 0x684 + 2, sn, packetBuffer, [&]()
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

        inverterData.hasBattery = inverterData.soc != 0 || inverterData.batteryPower != 0;
        logInverterData(inverterData);
        return inverterData;
    }

    bool tryReadWithRetries(uint16_t startReg, uint16_t length, uint32_t sn, byte *buffer, std::function<void()> onSuccess)
    {
        for (int i = 0; i < MAX_RETRIES; ++i)
        {
            if (channel.connect(ip))
            {
                if (channel.sendReadDataRequest(startReg, length, sn))
                {
                    if (channel.readModbusRTUResponse(buffer, 1024) > 0)
                    {
                        onSuccess();
                        channel.disconnect();
                        return true;
                    }
                    else
                    {
                        log_d("Read failed for 0x%04X", startReg);
                    }
                }
                else
                {
                    log_d("Send request failed for 0x%04X", startReg);
                }
                channel.disconnect();
            }
            else
            {
                log_d("Failed to connect to dongle at %s", ip.toString().c_str());
            }
        }
        return false;
    }

    void ensureIPAddress(const String &ipAddress)
    {
        if (ip == IPAddress(0, 0, 0, 0))
        {
            if (!ipAddress.isEmpty())
                ip = IPAddress(ipAddress.c_str());
            if (ip == IPAddress(0, 0, 0, 0))
            {
                ip = discoverDongleIP();
                if (ip == IPAddress(0, 0, 0, 0))
                    ip = IPAddress(10, 10, 100, 254); // fallback
            }
        }
    }

    IPAddress
    discoverDongleIP()
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