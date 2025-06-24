#pragma once

#include "../LSW3DongleBase.hpp"

class DeyeDongleAPI: LSW3DongleBase
{
public:
    InverterData_t loadData(String sn)
    {
        return readData(sn);
    }

private:
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
        if (connect(client))
        {
            log_d("Connected.");
            byte packetBuffer[1024];

            inverterData.sn = sn;
            bool isV104 = false;
            if (sendReadDataRequest(client, sequenceNumber, 0, 8 - 0 + 1, sn))
            {
                if (readModbusRTUResponse(client, packetBuffer, sizeof(packetBuffer)) > 0)
                {
                    inverterData.status = DONGLE_STATUS_OK;
                    inverterData.millis = millis();
                    uint16_t deviceType = readUInt16(packetBuffer, 0);
                    log_d("Device type: %s", String(deviceType, HEX));
                    uint16_t commProtoVer = readUInt16(packetBuffer, 2);
                    log_d("Comm protocol version: %s", String(commProtoVer, HEX));
                    isV104 = (commProtoVer >= 0x0104);
                    String inverterSN = readString(packetBuffer, 3, 10);
                    log_d("Inverter SN: %s", inverterSN.c_str());
                    inverterData.sn = inverterSN;
                }
                else
                {
                    disconnect(client);
                    inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                    return inverterData;
                }
            }
            else
            {
                log_d("Failed to send request");
                disconnect(client);
                inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                return inverterData;
            }

            // pv input
            // 672-673
            // but we need only few
            if (sendReadDataRequest(client, sequenceNumber, 672, 675 - 672 + 1, sn))
            {
                if (readModbusRTUResponse(client, packetBuffer, sizeof(packetBuffer)) > 0)
                {
                    inverterData.pv1Power = isV104 ? readUInt16_H10(packetBuffer, 672 - 672) : readUInt16(packetBuffer, 672 - 672);
                    inverterData.pv2Power = isV104 ? readUInt16_H10(packetBuffer, 673 - 672) : readUInt16(packetBuffer, 673 - 672);
                    inverterData.pv3Power = isV104 ? readUInt16_H10(packetBuffer, 674 - 672) : readUInt16(packetBuffer, 674 - 672);
                    inverterData.pv4Power = isV104 ? readUInt16_H10(packetBuffer, 675 - 672) : readUInt16(packetBuffer, 675 - 672);
                }
                else
                {
                    disconnect(client);
                    inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                    return inverterData;
                }
            }
            else
            {
                log_d("Failed to send request");
                disconnect(client);
                inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                return inverterData;
            }

            // battery input
            // 586 - 591
            if (sendReadDataRequest(client, sequenceNumber, 586, 591 - 586 + 1, sn))
            {
                if (readModbusRTUResponse(client, packetBuffer, sizeof(packetBuffer)) > 0)
                {
                    inverterData.batteryTemperature = (readInt16(packetBuffer, 586 - 586)  - 1000)  / 10;
                    inverterData.soc = readUInt16(packetBuffer, 588 - 586);
                    inverterData.batteryPower = isV104 ? -1 * readInt16_H10(packetBuffer, 590 - 586) : -1 * readInt16(packetBuffer, 590 - 586); //Battery power flow - negative for charging, positive for discharging
                }
                else
                {
                    disconnect(client);
                    inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                    return inverterData;
                }
            }
            else
            {
                log_d("Failed to send request");
                disconnect(client);
                inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                return inverterData;
            }


            // phase and power
            // 598 - 655
            if (sendReadDataRequest(client, sequenceNumber, 598, 655 - 598 + 1, sn))
            {
                if (readModbusRTUResponse(client, packetBuffer, sizeof(packetBuffer)) > 0)
                {
                    inverterData.L1Power = readInt16(packetBuffer, 633 - 598);
                    inverterData.L2Power = readInt16(packetBuffer, 634 - 598);
                    inverterData.L3Power = readInt16(packetBuffer, 635 - 598);
                    inverterData.inverterPower = readInt16(packetBuffer, 636 - 598);
                    inverterData.loadPower = readInt16(packetBuffer, 653 - 598);
                    inverterData.feedInPower = -1 * readInt16(packetBuffer, 625 - 598);
                }
                else
                {
                    disconnect(client);
                    inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                    return inverterData;
                }
            }
            else
            {
                log_d("Failed to send request");
                disconnect(client);
                inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                return inverterData;
            }

            // module info
            // 541
            if (sendReadDataRequest(client, sequenceNumber, 541, 1, sn))
            {
                if (readModbusRTUResponse(client, packetBuffer, sizeof(packetBuffer)) > 0)
                {
                    inverterData.inverterTemperature = (readInt16(packetBuffer, 0) - 1000) / 10;
                }
                else
                {
                    disconnect(client);
                    inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                    return inverterData;
                }
            }
            else
            {
                log_d("Failed to send request");
                disconnect(client);
                inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                return inverterData;
            }

            // //stats
            // 514 - 535
            if (sendReadDataRequest(client, sequenceNumber, 514, 535 - 514 + 2, sn))
            {
                if (readModbusRTUResponse(client, packetBuffer, sizeof(packetBuffer)) > 0)
                {
                    inverterData.pvToday = readUInt16(packetBuffer, 529 - 514) / 10.0f;
                    inverterData.pvTotal = readUInt16(packetBuffer, 534 - 514) / 10.0f;
                    inverterData.loadToday = readUInt16(packetBuffer, 526 - 514) / 10.0f;
                    inverterData.loadTotal = readUInt32(packetBuffer, 527 - 514) / 10.0f;
                    inverterData.batteryChargedToday = readUInt16(packetBuffer, 514 - 514) / 10.0f;
                    inverterData.batteryDischargedToday = readUInt16(packetBuffer, 515 - 514) / 10.0f;
                    inverterData.gridBuyToday = readUInt16(packetBuffer, 520 - 514) / 10.0f;
                    inverterData.gridBuyTotal = readUInt32(packetBuffer, 522 - 514) / 10.0f;
                    inverterData.gridSellToday = readUInt16(packetBuffer, 521 - 514) / 10.0f;
                    inverterData.gridSellTotal = readUInt32(packetBuffer, 524 - 514) / 10.0f;
                }
                else
                {
                    disconnect(client);
                    inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
                    return inverterData;
                }
            } else {
                log_d("Failed to send request");
                disconnect(client);
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