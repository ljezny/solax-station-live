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

        // https://github.com/wills106/homeassistant-solax-modbus/blob/main/custom_components/solax_modbus/plugin_sofar.py
        InverterData_t inverterData;
        log_d("Connecting to dongle...");
        uint32_t sn = strtoul(dongleSN.c_str(), NULL, 10);
        log_d("SN: %d", sn);
        byte packetBuffer[1024];
        for (int r = 0; r < 3; r++)
        {
            if (channel.connect(ip))
            {
                inverterData.sn = sn;

                // pv input
                // 0x0580 - 0x05FF
                // but we need only few
                if (channel.sendReadDataRequest(0x586, 0x58F - 0x586 + 1, sn))
                {
                    if (channel.readModbusRTUResponse(packetBuffer, sizeof(packetBuffer)) > 0)
                    {
                        inverterData.status = DONGLE_STATUS_OK;
                        inverterData.millis = millis();
                        inverterData.pv1Power = channel.readUInt16(packetBuffer, 0x586 - 0x586) * 10;
                        inverterData.pv2Power = channel.readUInt16(packetBuffer, 0x589 - 0x586) * 10;
                        inverterData.pv3Power = channel.readUInt16(packetBuffer, 0x58B - 0x586) * 10;
                        inverterData.pv4Power = channel.readUInt16(packetBuffer, 0x58F - 0x586) * 10;
                    }
                    else
                    {
                        log_d("Failed to read PV data");
                        inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                    }
                }
                else
                {
                    log_d("Failed to send request");
                    inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                    return inverterData;
                }
                channel.disconnect();
            }

            if (inverterData.status == DONGLE_STATUS_OK)
            {
                break; // exit loop if data was read successfully
            }
        }

        if (inverterData.status != DONGLE_STATUS_OK)
        {
            log_d("Failed to connect to dongle at %s", ip.toString().c_str());
            inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
            return inverterData;
        }

        for (int r = 0; r < 3; r++)
        {
            if (channel.connect(ip))
            {
                // battery input
                // 0x0600 - 0x067F
                if (channel.sendReadDataRequest(0x667, 2, sn))
                {
                    if (channel.readModbusRTUResponse(packetBuffer, sizeof(packetBuffer)) > 0)
                    {
                        inverterData.status = DONGLE_STATUS_OK;
                        inverterData.batteryPower = channel.readInt16(packetBuffer, 0) * 100;
                        inverterData.soc = channel.readUInt16(packetBuffer, 1);
                    }
                    else
                    {
                        inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                    }
                }
                else
                {
                    log_d("Failed to send request");
                    inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                }
                channel.disconnect();
            }

            if (inverterData.status == DONGLE_STATUS_OK)
            {
                break; // exit loop if data was read successfully
            }
        }

        if (inverterData.status != DONGLE_STATUS_OK)
        {
            log_d("Failed to read battery data");
            inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
            return inverterData;
        }

        for (int r = 0; r < 3; r++)
        {
            if (channel.connect(ip))
            {
                if (channel.sendReadDataRequest(0x607, 0x607 - 0x607 + 1, sn))
                {
                    if (channel.readModbusRTUResponse(packetBuffer, sizeof(packetBuffer)) > 0)
                    {
                        inverterData.status = DONGLE_STATUS_OK;
                        inverterData.batteryTemperature = channel.readInt16(packetBuffer, 0x607 - 0x607);
                    }
                }
                channel.disconnect();
            }
            if (inverterData.status == DONGLE_STATUS_OK)
            {
                break; // exit loop if data was read successfully
            }
        }

        if (inverterData.status != DONGLE_STATUS_OK)
        {
            log_d("Failed to read battery temperature");
            inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
            return inverterData;
        }

        for (int r = 0; r < 3; r++)
        {
            if (channel.connect(ip))
            {
                // module info
                // 0x404 - 0x44F
                if (channel.sendReadDataRequest(0x418, 0x418 - 0x418 + 1, sn))
                {
                    if (channel.readModbusRTUResponse(packetBuffer, sizeof(packetBuffer)) > 0)
                    {
                        inverterData.status = DONGLE_STATUS_OK;
                        inverterData.inverterTemperature = channel.readInt16(packetBuffer, 0x418 - 0x418);
                    }
                }
                channel.disconnect();
            }

            if (inverterData.status == DONGLE_STATUS_OK)
            {
                break; // exit loop if data was read successfully
            }
        }

        if (inverterData.status != DONGLE_STATUS_OK)
        {
            log_d("Failed to read inverter temperature");
            inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
            return inverterData;
        }

        for (int r = 0; r < 3; r++)
        {
            if (channel.connect(ip))
            {
                // on grid input
                // 0x484 - 0x4BC
                if (channel.sendReadDataRequest(0x484, 0x4BC - 0x484 + 1, sn))
                {
                    if (channel.readModbusRTUResponse(packetBuffer, sizeof(packetBuffer)) > 0)
                    {
                        inverterData.status = DONGLE_STATUS_OK;
                        inverterData.inverterPower = channel.readInt16(packetBuffer, 0x485 - 0x484) * 10;
                        inverterData.loadPower = channel.readInt16(packetBuffer, 0x04AF - 0x484) * 10;
                        inverterData.feedInPower = channel.readInt16(packetBuffer, 0x0488 - 0x484) * 10;
                        inverterData.L1Power = channel.readInt16(packetBuffer, 0x48F - 0x484) * 10;
                        inverterData.L2Power = channel.readInt16(packetBuffer, 0x49A - 0x484) * 10;
                        inverterData.L3Power = channel.readInt16(packetBuffer, 0x4A5 - 0x484) * 10;
                    }
                    else
                    {
                        log_d("Failed to read grid data");
                        inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                    }
                }
                else
                {
                    log_d("Failed to send request");
                    inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                }
                channel.disconnect();
            }
            if (inverterData.status == DONGLE_STATUS_OK)
            {
                break; // exit loop if data was read successfully
            }
        }

        if (inverterData.status != DONGLE_STATUS_OK)
        {
            log_d("Failed to read grid data");
            inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
            return inverterData;
        }

        for (int r = 0; r < 3; r++)
        {
            if (channel.connect(ip))
            {
                // //stats
                // 0x0680 - 0x06BF
                if (channel.sendReadDataRequest(0x684, 0x698 - 0x684 + 2, sn))
                {
                    if (channel.readModbusRTUResponse(packetBuffer, sizeof(packetBuffer)) > 0)
                    {
                        inverterData.status = DONGLE_STATUS_OK;
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
                    }
                    else
                    {
                        log_d("Failed to read stats data");
                        inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                    }
                }
                else
                {
                    log_d("Failed to send request");
                    inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                }
                channel.disconnect();
            }
        }

        inverterData.hasBattery = inverterData.soc != 0 || inverterData.batteryPower != 0;
        logInverterData(inverterData);
        return inverterData;
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