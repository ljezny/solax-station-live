#pragma once

#include "../../Protocol/V5TCP.hpp"

#define DELAY_BETWEEN_REQUESTS_MS 300

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
        IPAddress ip = IPAddress(ipAddress.c_str());
        if(ip == IPAddress(0, 0, 0, 0))
        {
            ip = IPAddress(10, 10, 100, 254); // default IP
        }
        
        // https://github.com/wills106/homeassistant-solax-modbus/blob/main/custom_components/solax_modbus/plugin_sofar.py
        InverterData_t inverterData;
        log_d("Connecting to dongle...");
        uint32_t sn = strtoul(dongleSN.c_str(), NULL, 10);
        log_d("SN: %d", sn);
        if (channel.connect(ip))
        {
            log_d("Connected.");
            byte packetBuffer[1024];

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
            delay(DELAY_BETWEEN_REQUESTS_MS);
            // battery input
            // 0x0600 - 0x067F
            if (channel.sendReadDataRequest(0x667, 2, sn))
            {
                if (channel.readModbusRTUResponse(packetBuffer, sizeof(packetBuffer)) > 0)
                {
                    inverterData.batteryPower = channel.readInt16(packetBuffer, 0) * 100;
                    inverterData.soc = channel.readUInt16(packetBuffer, 1);
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
            delay(DELAY_BETWEEN_REQUESTS_MS);
            if (channel.sendReadDataRequest(0x607, 0x607 - 0x607 + 1, sn))
            {
                if (channel.readModbusRTUResponse(packetBuffer, sizeof(packetBuffer)) > 0)
                {
                    inverterData.batteryTemperature = channel.readInt16(packetBuffer, 0x607 - 0x607);
                }
            }
            delay(DELAY_BETWEEN_REQUESTS_MS);
            // module info
            // 0x404 - 0x44F
            if (channel.sendReadDataRequest( 0x418, 0x418 - 0x418 + 1, sn))
            {
                if (channel.readModbusRTUResponse(packetBuffer, sizeof(packetBuffer)) > 0)
                {
                    inverterData.inverterTemperature = channel.readInt16(packetBuffer, 0x418 - 0x418);
                }
            }

            delay(DELAY_BETWEEN_REQUESTS_MS);
            // on grid input
            // 0x484 - 0x4BC
            if (channel.sendReadDataRequest(0x484, 0x4BC - 0x484 + 1, sn))
            {
                if (channel.readModbusRTUResponse(packetBuffer, sizeof(packetBuffer)) > 0)
                {
                    inverterData.inverterPower = channel.readInt16(packetBuffer, 0x485 - 0x484) * 10;
                    inverterData.loadPower = channel.readInt16(packetBuffer, 0x04AF - 0x484) * 10;
                    inverterData.feedInPower = channel.readInt16(packetBuffer, 0x0488 - 0x484) * 10;
                    inverterData.L1Power = channel.readInt16(packetBuffer, 0x48F - 0x484) * 10;
                    inverterData.L2Power = channel.readInt16(packetBuffer, 0x49A - 0x484) * 10;
                    inverterData.L3Power = channel.readInt16(packetBuffer, 0x4A5 - 0x484) * 10;
                }
                else
                {
                    channel.disconnect();
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
            delay(DELAY_BETWEEN_REQUESTS_MS);
            // //stats
            // 0x0680 - 0x06BF
            if (channel.sendReadDataRequest(0x684, 0x698 - 0x684 + 2, sn))
            {
                if (channel.readModbusRTUResponse(packetBuffer, sizeof(packetBuffer)) > 0)
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

        channel.disconnect(); //don't disconnect, we need to keep the connection open for next requests
        return inverterData;
    }
};