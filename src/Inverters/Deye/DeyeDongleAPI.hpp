#pragma once

#include "../../Protocol/V5TCP.hpp"

class DeyeDongleAPI
{
public:
    InverterData_t loadData(String ipAddress, String dongleSN)
    {
        return readData(ipAddress, dongleSN);
    }

private:
    V5TCP channel;
    IPAddress ip;

    InverterData_t readData(String ipAddress, String dongleSN)
    {
        int powerMultiplier = 1;
        uint16_t deviceType = 0;
        InverterData_t inverterData;
        log_d("Connecting to dongle...");
        uint32_t sn = strtoul(dongleSN.c_str(), NULL, 10);
        log_d("SN: %d", sn);
        byte packetBuffer[1024];
        channel.ensureIPAddress(ipAddress);

        if (!channel.tryReadWithRetries(0, 8 - 0 + 1, sn, packetBuffer, [&]()
                                        {
                    inverterData.millis = millis();
                    inverterData.status = DONGLE_STATUS_OK;
                    deviceType = channel.readUInt16(packetBuffer, 0);
                    log_d("Device type: %s", String(deviceType, HEX));
                    uint16_t commProtoVer = channel.readUInt16(packetBuffer, 2);
                    log_d("Comm protocol version: %s", String(commProtoVer, HEX));
                    String inverterSN = channel.readString(packetBuffer, 3, 10);
                    log_d("Inverter SN: %s", inverterSN.c_str());
                    inverterData.sn = inverterSN; }))
            return inverterData;

        if (deviceType == 0x500 || deviceType == 0x600 || deviceType == 0x601)
        {
            load3PhaseInverter(deviceType, sn, inverterData);
        }
        else if (deviceType == 2 || deviceType == 3)
        {
            loadMicroInverter(sn, inverterData);
        }

        inverterData.hasBattery = inverterData.soc != 0 || inverterData.batteryPower != 0;
        logInverterData(inverterData);

        return inverterData;
    }

    void loadMicroInverter(uint32_t sn, InverterData_t &inverterData)
    {
        log_d("Loading micro inverter data...");
        byte packetBuffer[1024];
        if (!channel.tryReadWithRetries(150, 197 - 150 + 1, sn, packetBuffer, [&]()
                                        {
                                            inverterData.pv1Power = channel.readUInt16(packetBuffer, 186 - 150);
                                            inverterData.pv2Power = channel.readUInt16(packetBuffer, 187 - 150);
                                            inverterData.pv3Power = channel.readUInt16(packetBuffer, 188 - 150);
                                            inverterData.pv4Power = channel.readUInt16(packetBuffer, 189 - 150);
                                            inverterData.inverterPower = channel.readInt16(packetBuffer, 175 - 150);
                                            inverterData.loadPower = channel.readInt16(packetBuffer, 178 - 150);
                                            inverterData.batteryTemperature = (channel.readInt16(packetBuffer, 182 - 150) - 1000) / 10;
                                            inverterData.soc = channel.readUInt16(packetBuffer, 184 - 150);
                                            inverterData.batteryPower = -1 * channel.readInt16(packetBuffer, 190 - 150);
                                            inverterData.feedInPower = -1 * channel.readInt16(packetBuffer, 169 - 150);
                                        }))
            return;

                if (!channel.tryReadWithRetries(60, 99 - 60 + 1, sn, packetBuffer, [&]()
                                        {
                inverterData.inverterTemperature = (channel.readInt16(packetBuffer, 90 - 60) - 1000) / 10;
                inverterData.pvToday = channel.readUInt16(packetBuffer, 60 - 60) / 10.0f;
                inverterData.pvTotal = channel.readUInt16(packetBuffer, 63 - 60) / 10.0f;
                inverterData.loadToday = channel.readUInt16(packetBuffer, 84 - 60) / 10.0f;
                inverterData.batteryChargedToday = channel.readUInt16(packetBuffer, 70 - 60) / 10.0f;
                inverterData.batteryDischargedToday = channel.readUInt16(packetBuffer, 71 - 60) / 10.0f;
                inverterData.gridBuyToday = channel.readUInt16(packetBuffer, 76 - 60) / 10.0f;
                inverterData.gridSellToday = channel.readUInt16(packetBuffer, 77 - 60) / 10.0f;
                                        }))
            return;
    }

    void load3PhaseInverter(int deviceType, uint32_t sn, InverterData_t &inverterData)
    {
        log_d("Loading 3-phase inverter data...");
        int powerMultiplier = (deviceType == 0x600 || deviceType == 0x601) ? 10 : 1;
        byte packetBuffer[1024];
        if (!channel.tryReadWithRetries(672, 675 - 672 + 1, sn, packetBuffer, [&]()
                                        {
                inverterData.pv1Power = powerMultiplier * channel.readUInt16(packetBuffer, 672 - 672);
                inverterData.pv2Power = powerMultiplier * channel.readUInt16(packetBuffer, 673 - 672);
                inverterData.pv3Power = powerMultiplier * channel.readUInt16(packetBuffer, 674 - 672);
                inverterData.pv4Power = powerMultiplier * channel.readUInt16(packetBuffer, 675 - 672); }))
            return;

        if (!channel.tryReadWithRetries(586, 591 - 586 + 1, sn, packetBuffer, [&]()
                                        {
                                            inverterData.batteryTemperature = (channel.readInt16(packetBuffer, 586 - 586) - 1000) / 10;
                                            inverterData.soc = channel.readUInt16(packetBuffer, 588 - 586);
                                            inverterData.batteryPower = -1 * powerMultiplier * channel.readInt16(packetBuffer, 590 - 586); // Battery power flow - negative for charging, positive for discharging
                                        }))
            return;

        if (!channel.tryReadWithRetries(598, 655 - 598 + 1, sn, packetBuffer, [&]()
                                        {
                inverterData.L1Power = channel.readInt16(packetBuffer, 633 - 598);
                inverterData.L2Power = channel.readInt16(packetBuffer, 634 - 598);
                inverterData.L3Power = channel.readInt16(packetBuffer, 635 - 598);
                inverterData.inverterPower = channel.readInt16(packetBuffer, 636 - 598);
                inverterData.loadPower = channel.readInt16(packetBuffer, 653 - 598);
                inverterData.feedInPower = -1 * channel.readInt16(packetBuffer, 625 - 598); }))
            return;

        channel.tryReadWithRetries(541, 1, sn, packetBuffer, [&]()
                                   { inverterData.inverterTemperature = (channel.readInt16(packetBuffer, 0) - 1000) / 10; });

        if (!channel.tryReadWithRetries(514, 535 - 514 + 2, sn, packetBuffer, [&]()
                                        {
                                            inverterData.pvToday = channel.readUInt16(packetBuffer, 529 - 514) / 10.0f;
                inverterData.pvTotal = channel.readUInt16(packetBuffer, 534 - 514) / 10.0f;
                inverterData.loadToday = channel.readUInt16(packetBuffer, 526 - 514) / 10.0f;
                inverterData.loadTotal = channel.readUInt32(packetBuffer, 527 - 514) / 10.0f;
                inverterData.batteryChargedToday = channel.readUInt16(packetBuffer, 514 - 514) / 10.0f;
                inverterData.batteryDischargedToday = channel.readUInt16(packetBuffer, 515 - 514) / 10.0f;
                inverterData.gridBuyToday = channel.readUInt16(packetBuffer, 520 - 514) / 10.0f;
                inverterData.gridBuyTotal = channel.readUInt32(packetBuffer, 522 - 514) / 10.0f;
                inverterData.gridSellToday = channel.readUInt16(packetBuffer, 521 - 514) / 10.0f;
                inverterData.gridSellTotal = channel.readUInt32(packetBuffer, 524 - 514) / 10.0f; }))
            return;
    }
};