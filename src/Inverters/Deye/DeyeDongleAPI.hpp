#pragma once

#include "../../Protocol/V5TCP.hpp"

class DeyeDongleAPI
{
public:
    /**
     * Returns true if this inverter supports intelligence mode control
     */
    bool supportsIntelligence() { return false; }

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

        if (deviceType == 2 || deviceType == 3)
        {
            loadMicroInverter(sn, inverterData);
        } else {
            load3PhaseInverter(deviceType, sn, inverterData);
        }

        // Read RTC time from Deye inverter (registers 22-27)
        readInverterRTC(sn, inverterData);

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
                                            inverterData.L1Power = channel.readInt16(packetBuffer, 175 - 150);
                                            inverterData.loadPower = channel.readInt16(packetBuffer, 178 - 150);
                                            inverterData.batteryTemperature = (channel.readInt16(packetBuffer, 182 - 150) - 1000) / 10;
                                            inverterData.soc = channel.readUInt16(packetBuffer, 184 - 150);
                                            inverterData.batteryPower = -1 * channel.readInt16(packetBuffer, 190 - 150);
                                            inverterData.gridPowerL1 = -1 * channel.readInt16(packetBuffer, 169 - 150);
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

    void readInverterRTC(uint32_t sn, InverterData_t &inverterData)
    {
        // Deye RTC registers: 22-27 (holding registers)
        // 22: Year, 23: Month, 24: Day, 25: Hour, 26: Minute, 27: Second
        byte packetBuffer[16];
        channel.tryReadWithRetries(22, 6, sn, packetBuffer, [&]()
                                   {
                                       struct tm timeinfo = {};
                                       timeinfo.tm_year = channel.readUInt16(packetBuffer, 0) - 1900;  // Year - 1900
                                       timeinfo.tm_mon = channel.readUInt16(packetBuffer, 1) - 1;     // Month 1-12 to 0-11
                                       timeinfo.tm_mday = channel.readUInt16(packetBuffer, 2);
                                       timeinfo.tm_hour = channel.readUInt16(packetBuffer, 3);
                                       timeinfo.tm_min = channel.readUInt16(packetBuffer, 4);
                                       timeinfo.tm_sec = channel.readUInt16(packetBuffer, 5);
                                       timeinfo.tm_isdst = -1;

                                       inverterData.inverterTime = mktime(&timeinfo);
                                       log_d("Deye RTC: %04d-%02d-%02d %02d:%02d:%02d",
                                             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                                             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
                                   });
    }

    void load3PhaseInverter(int deviceType, uint32_t sn, InverterData_t &inverterData)
    {
        log_d("Loading 3-phase inverter data...");
        int powerMultiplier = (deviceType == 6) ? 10 : 1;
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

        // Read max charge/discharge current from registers 108/109
        // Scale is 0.1A according to Deye documentation
        channel.tryReadWithRetries(108, 2, sn, packetBuffer, [&]()
                                   {
                                       float maxChargeCurrent = channel.readUInt16(packetBuffer, 108 - 108) * 0.1f;
                                       float maxDischargeCurrent = channel.readUInt16(packetBuffer, 109 - 108) * 0.1f;
                                       // Estimate battery voltage from battery power and assume ~50V for HV battery
                                       float batteryVoltage = 50.0f; // Default for HV batteries
                                       inverterData.maxChargePowerW = (uint16_t)(maxChargeCurrent * batteryVoltage);
                                       inverterData.maxDischargePowerW = (uint16_t)(maxDischargeCurrent * batteryVoltage);
                                       log_d("Deye max charge current: %.1f A, max discharge current: %.1f A", maxChargeCurrent, maxDischargeCurrent);
                                       log_d("Max charge power: %d W, max discharge power: %d W", inverterData.maxChargePowerW, inverterData.maxDischargePowerW);
                                   });

        if (!channel.tryReadWithRetries(598, 655 - 598 + 1, sn, packetBuffer, [&]()
                                        {
                inverterData.L1Power = channel.readInt16(packetBuffer, 633 - 598);
                inverterData.L2Power = channel.readInt16(packetBuffer, 634 - 598);
                inverterData.L3Power = channel.readInt16(packetBuffer, 635 - 598);
                //inverterData.inverterPower = channel.readInt16(packetBuffer, 636 - 598);
                inverterData.loadPower = channel.readInt16(packetBuffer, 653 - 598);
                inverterData.gridPowerL1 = -1 * channel.readInt16(packetBuffer, 622 - 598);
                inverterData.gridPowerL2 = -1 * channel.readInt16(packetBuffer, 623 - 598);
                inverterData.gridPowerL3 = -1 * channel.readInt16(packetBuffer, 624 - 598);
                //inverterData.gridPower = -1 * channel.readInt16(packetBuffer, 625 - 598); 
            }))
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

    bool setWorkMode(const String& ipAddress, InverterMode_t mode)
    {
        // TODO: Not implemented yet
        return false;
    }
};