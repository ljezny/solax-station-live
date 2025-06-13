#pragma once

#include "../LSW3DongleBase.hpp"

#define DELAY_BETWEEN_REQUESTS_MS 300

class SofarSolarDongleAPI : LSW3DongleBase
{
public:
    InverterData_t loadData(String sn)
    {
        return readData(sn);
    }

private:
    InverterData_t readData(String dongleSN)
    {
        // https://github.com/wills106/homeassistant-solax-modbus/blob/main/custom_components/solax_modbus/plugin_sofar.py
        InverterData_t inverterData;
        NetworkClient client;
        log_d("Connecting to dongle...");
        uint32_t sn = strtoul(dongleSN.c_str(), NULL, 10);
        log_d("SN: %d", sn);
        if (connect(client))
        {
            log_d("Connected.");
            byte packetBuffer[1024];

            inverterData.sn = sn;

            // pv input
            // 0x0580 - 0x05FF
            // but we need only few
            if (sendReadDataRequest(client, sequenceNumber, 0x586, 0x58F - 0x586 + 1, sn))
            {
                if (readModbusRTUResponse(client, packetBuffer, sizeof(packetBuffer)) > 0)
                {
                    inverterData.status = DONGLE_STATUS_OK;
                    inverterData.millis = millis();
                    inverterData.pv1Power = readUInt16(packetBuffer, 0x586 - 0x586) * 10;
                    inverterData.pv2Power = readUInt16(packetBuffer, 0x589 - 0x586) * 10;
                    inverterData.pv3Power = readUInt16(packetBuffer, 0x58B - 0x586) * 10;
                    inverterData.pv4Power = readUInt16(packetBuffer, 0x58F - 0x586) * 10;
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
            delay(DELAY_BETWEEN_REQUESTS_MS);
            // battery input
            // 0x0600 - 0x067F
            if (sendReadDataRequest(client, sequenceNumber, 0x667, 2, sn))
            {
                if (readModbusRTUResponse(client, packetBuffer, sizeof(packetBuffer)) > 0)
                {
                    inverterData.batteryPower = readInt16(packetBuffer, 0) * 100;
                    inverterData.soc = readUInt16(packetBuffer, 1);
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
            delay(DELAY_BETWEEN_REQUESTS_MS);
            if (sendReadDataRequest(client, sequenceNumber, 0x607, 0x607 - 0x607 + 1, sn))
            {
                if (readModbusRTUResponse(client, packetBuffer, sizeof(packetBuffer)) > 0)
                {
                    inverterData.batteryTemperature = readInt16(packetBuffer, 0x607 - 0x607);
                }
            }
            delay(DELAY_BETWEEN_REQUESTS_MS);
            // module info
            // 0x404 - 0x44F
            if (sendReadDataRequest(client, sequenceNumber, 0x418, 0x418 - 0x418 + 1, sn))
            {
                if (readModbusRTUResponse(client, packetBuffer, sizeof(packetBuffer)) > 0)
                {
                    inverterData.inverterTemperature = readInt16(packetBuffer, 0x418 - 0x418);
                }
            }

            delay(DELAY_BETWEEN_REQUESTS_MS);
            // on grid input
            // 0x484 - 0x4BC
            if (sendReadDataRequest(client, sequenceNumber, 0x484, 0x4BC - 0x484 + 1, sn))
            {
                if (readModbusRTUResponse(client, packetBuffer, sizeof(packetBuffer)) > 0)
                {
                    inverterData.inverterPower = readInt16(packetBuffer, 0x485 - 0x484) * 10;
                    inverterData.loadPower = readInt16(packetBuffer, 0x04AF - 0x484) * 10;
                    inverterData.feedInPower = readInt16(packetBuffer, 0x0488 - 0x484) * 10;
                    inverterData.L1Power = readInt16(packetBuffer, 0x48F - 0x484) * 10;
                    inverterData.L2Power = readInt16(packetBuffer, 0x49A - 0x484) * 10;
                    inverterData.L3Power = readInt16(packetBuffer, 0x4A5 - 0x484) * 10;
                }
                else
                {
                    disconnect(client);
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
            delay(DELAY_BETWEEN_REQUESTS_MS);
            // //stats
            // 0x0680 - 0x06BF
            if (sendReadDataRequest(client, sequenceNumber, 0x684, 0x698 - 0x684 + 2, sn))
            {
                if (readModbusRTUResponse(client, packetBuffer, sizeof(packetBuffer)) > 0)
                {
                    inverterData.pvToday = readUInt32(packetBuffer, 0x684 - 0x684) / 100.0f;
                    inverterData.pvTotal = readUInt32(packetBuffer, 0x686 - 0x684) / 10.0f;
                    inverterData.loadToday = readUInt32(packetBuffer, 0x688 - 0x684) / 100.0f;
                    inverterData.loadTotal = readUInt32(packetBuffer, 0x68A - 0x684) / 10.0f;
                    inverterData.batteryChargedToday = readUInt32(packetBuffer, 0x694 - 0x684) / 100.0f;
                    inverterData.batteryDischargedToday = readUInt32(packetBuffer, 0x698 - 0x684) / 100.0f;
                    inverterData.gridBuyToday = readUInt32(packetBuffer, 0x68C - 0x684) / 100.0f;
                    inverterData.gridBuyTotal = readUInt32(packetBuffer, 0x68E - 0x684) / 10.0f;
                    inverterData.gridSellToday = readUInt32(packetBuffer, 0x690 - 0x684) / 100.0f;
                    inverterData.gridSellTotal = readUInt32(packetBuffer, 0x692 - 0x684) / 10.0f;
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
        }

        inverterData.hasBattery = inverterData.soc != 0 || inverterData.batteryPower != 0;
        logInverterData(inverterData);

        disconnect(client);
        return inverterData;
    }
};