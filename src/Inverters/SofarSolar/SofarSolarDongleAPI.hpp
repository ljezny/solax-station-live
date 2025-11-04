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

    InverterData_t readData(String ipAddress, String dongleSN)
    {
        InverterData_t inverterData{};
        uint32_t sn = strtoul(dongleSN.c_str(), NULL, 10);
        inverterData.sn = sn;
        byte packetBuffer[1024];

        channel.ensureIPAddress(ipAddress);

        // PV Input
        if (!channel.tryReadWithRetries(0x586, 0x58F - 0x586 + 1, sn, packetBuffer, [&]()
                                {
            inverterData.millis = millis();
            inverterData.pv1Power = channel.readUInt16(packetBuffer, 0) * 10;
            inverterData.pv2Power = channel.readUInt16(packetBuffer, 3) * 10;
            inverterData.pv3Power = channel.readUInt16(packetBuffer, 5) * 10;
            inverterData.pv4Power = channel.readUInt16(packetBuffer, 9) * 10; }))
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

        // Grid Input
        if (!channel.tryReadWithRetries(0x484, 0x4BC - 0x484 + 1, sn, packetBuffer, [&]()
                                {
            inverterData.inverterPower = channel.readInt16(packetBuffer, 0x485 - 0x484) * 10;
            inverterData.loadPower = channel.readInt16(packetBuffer, 0x04AF - 0x484) * 10;
            inverterData.gridPower = channel.readInt16(packetBuffer, 0x0488 - 0x484) * 10;
            inverterData.L1Power = channel.readInt16(packetBuffer, 0x48F - 0x484) * 10;
            inverterData.L2Power = channel.readInt16(packetBuffer, 0x49A - 0x484) * 10;
            inverterData.L3Power = channel.readInt16(packetBuffer, 0x4A5 - 0x484) * 10; }))
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

        inverterData.hasBattery = inverterData.soc != 0 || inverterData.batteryPower != 0;
        logInverterData(inverterData);
        return inverterData;
    }
};