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
        if (ip == IPAddress(0, 0, 0, 0))
        {
            if (!ipAddress.isEmpty())
            {
                ip = IPAddress(ipAddress.c_str());
            }

            if (ip == IPAddress(0, 0, 0, 0))
            {
                ip = discoverDongleIP();
                if (ip == IPAddress(0, 0, 0, 0))
                {
                    ip = IPAddress(10, 10, 100, 254); // default IP
                }
            }
        }
        int powerMultiplier = 1;
        InverterData_t inverterData;
        log_d("Connecting to dongle...");
        uint32_t sn = strtoul(dongleSN.c_str(), NULL, 10);
        log_d("SN: %d", sn);
        if (channel.connect(ip))
        {
            log_d("Connected.");
            byte packetBuffer[1024];

            inverterData.sn = String(sn);

            if (channel.sendReadDataRequest(0, 8 - 0 + 1, sn))
            {
                if (channel.readModbusRTUResponse(packetBuffer, sizeof(packetBuffer)) > 0)
                {
                    inverterData.status = DONGLE_STATUS_OK;
                    inverterData.millis = millis();
                    uint16_t deviceType = channel.readUInt16(packetBuffer, 0);
                    log_d("Device type: %s", String(deviceType, HEX));
                    powerMultiplier = (deviceType == 0x600 || deviceType == 0x601) ? 10 : 1;
                    uint16_t commProtoVer = channel.readUInt16(packetBuffer, 2);
                    log_d("Comm protocol version: %s", String(commProtoVer, HEX));
                    String inverterSN = channel.readString(packetBuffer, 3, 10);
                    log_d("Inverter SN: %s", inverterSN.c_str());
                    inverterData.sn = inverterSN;
                }
                else
                {
                    channel.disconnect();
                    inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                    return inverterData;
                }
            }
            else
            {
                log_d("Failed to send request");
                channel.disconnect();
                inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                return inverterData;
            }

            // pv input
            // 672-673
            // but we need only few
            if (channel.sendReadDataRequest(672, 673 - 672 + 1, sn))
            {
                if (channel.readModbusRTUResponse(packetBuffer, sizeof(packetBuffer)) > 0)
                {
                    inverterData.pv1Power = powerMultiplier * channel.readUInt16(packetBuffer, 672 - 672);
                    inverterData.pv2Power = powerMultiplier * channel.readUInt16(packetBuffer, 673 - 672);
                    inverterData.pv3Power = powerMultiplier * channel.readUInt16(packetBuffer, 674 - 672);
                    inverterData.pv4Power = powerMultiplier * channel.readUInt16(packetBuffer, 675 - 672);
                }
                else
                {
                    channel.disconnect();
                    inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                    return inverterData;
                }
            }
            else
            {
                log_d("Failed to send request");
                channel.disconnect();
                inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                return inverterData;
            }

            // battery input
            // 586 - 591
            if (channel.sendReadDataRequest(586, 591 - 586 + 1, sn))
            {
                if (channel.readModbusRTUResponse(packetBuffer, sizeof(packetBuffer)) > 0)
                {
                    inverterData.batteryTemperature = (channel.readInt16(packetBuffer, 586 - 586) - 1000) / 10;
                    inverterData.soc = channel.readUInt16(packetBuffer, 588 - 586);
                    inverterData.batteryTemperature = (channel.readInt16(packetBuffer, 586 - 586) - 1000) / 10;
                    inverterData.batteryPower =  -1 * powerMultiplier * channel.readInt16(packetBuffer, 590 - 586); // Battery power flow - negative for charging, positive for discharging
                }
                else
                {
                    channel.disconnect();
                    inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                    return inverterData;
                }
            }
            else
            {
                log_d("Failed to send request");
                channel.disconnect();
                inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                return inverterData;
            }

            // phase and power
            // 598 - 655
            if (channel.sendReadDataRequest(598, 655 - 598 + 1, sn))
            {
                if (channel.readModbusRTUResponse(packetBuffer, sizeof(packetBuffer)) > 0)
                {
                    inverterData.L1Power = channel.readInt16(packetBuffer, 633 - 598);
                    inverterData.L2Power = channel.readInt16(packetBuffer, 634 - 598);
                    inverterData.L3Power = channel.readInt16(packetBuffer, 635 - 598);
                    inverterData.inverterPower = channel.readInt16(packetBuffer, 636 - 598);
                    inverterData.loadPower = channel.readInt16(packetBuffer, 653 - 598);
                    inverterData.feedInPower = -1 * channel.readInt16(packetBuffer, 625 - 598);
                }
                else
                {
                    channel.disconnect();
                    inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                    return inverterData;
                }
            }
            else
            {
                log_d("Failed to send request");
                channel.disconnect();
                inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                return inverterData;
            }

            // module info
            // 541
            if (channel.sendReadDataRequest(541, 1, sn))
            {
                if (channel.readModbusRTUResponse(packetBuffer, sizeof(packetBuffer)) > 0)
                {
                    inverterData.inverterTemperature = (channel.readInt16(packetBuffer, 0) - 1000) / 10;
                }
                else
                {
                    channel.disconnect();
                    inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                    return inverterData;
                }
            }
            else
            {
                log_d("Failed to send request");
                channel.disconnect();
                inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                return inverterData;
            }

            // //stats
            // 514 - 535
            if (channel.sendReadDataRequest(514, 535 - 514 + 2, sn))
            {
                if (channel.readModbusRTUResponse(packetBuffer, sizeof(packetBuffer)) > 0)
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
                    inverterData.gridSellTotal = channel.readUInt32(packetBuffer, 524 - 514) / 10.0f;
                }
                else
                {
                    channel.disconnect();
                    inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                    return inverterData;
                }
            }
            else
            {
                log_d("Failed to send request");
                channel.disconnect();
                inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                return inverterData;
            }
        }

        inverterData.hasBattery = inverterData.soc != 0 || inverterData.batteryPower != 0;
        logInverterData(inverterData);

        // disconnect(client); //do not disconnect, we will use the same connection for next request
        return inverterData;
    }

    IPAddress discoverDongleIP()
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