#pragma once

#include "../../Protocol/V5TCP.hpp"

class DeyeDongleAPI
{
public:
    InverterData_t loadData(String sn)
    {
        return readData(sn);
    }

private:
    V5TCP channel;

    uint16_t readUInt16_H10(byte *buf, byte reg)
    {
        return ((buf[3 + reg * 2] << 8) * 10) | buf[3 + reg * 2 + 1];
    }

    int16_t readInt16_H10(byte *buf, byte reg)
    {
        return ((buf[3 + reg * 2] << 8) * 10) | buf[3 + reg * 2 + 1];
    }

    InverterData_t readData(String dongleSN)
    {
        InverterData_t inverterData;
        log_d("Connecting to dongle...");
        uint32_t sn = strtoul(dongleSN.c_str(), NULL, 10);
        log_d("SN: %d", sn);
        if (channel.connect())
        {
            log_d("Connected.");
            byte packetBuffer[1024];

            inverterData.sn = sn;

            // pv input
            // 672-673
            // but we need only few
            bool isV104 = false;
            if (channel.sendReadDataRequest(0, 8 - 0 + 1, sn))
            {
                if (channel.readModbusRTUResponse(packetBuffer, sizeof(packetBuffer)) > 0)
                {
                    inverterData.status = DONGLE_STATUS_OK;
                    inverterData.millis = millis();
                    uint16_t deviceType = channel.readUInt16(packetBuffer, 0);
                    log_d("Device type: %s", String(deviceType, HEX));
                    uint16_t commProtoVer = channel.readUInt16(packetBuffer, 2);
                    log_d("Comm protocol version: %s", String(commProtoVer, HEX));
                    isV104 = (commProtoVer >= 0x0104);
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
                    inverterData.pv1Power = isV104 ? readUInt16_H10(packetBuffer, 672 - 672) : channel.readUInt16(packetBuffer, 672 - 672);
                    inverterData.pv2Power = isV104 ? readUInt16_H10(packetBuffer, 673 - 672) : channel.readUInt16(packetBuffer, 673 - 672);
                    inverterData.pv3Power = isV104 ? readUInt16_H10(packetBuffer, 674 - 672) : channel.readUInt16(packetBuffer, 674 - 672);
                    inverterData.pv4Power = isV104 ? readUInt16_H10(packetBuffer, 675 - 672) : channel.readUInt16(packetBuffer, 675 - 672);
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
                    inverterData.batteryPower = isV104 ? -1 * readInt16_H10(packetBuffer, 590 - 586) : -1 * channel.readInt16(packetBuffer, 590 - 586); // Battery power flow - negative for charging, positive for discharging
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
};