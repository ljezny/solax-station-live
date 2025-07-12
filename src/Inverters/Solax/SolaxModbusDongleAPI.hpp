#pragma once

#include "../../Protocol/ModbusTCP.hpp"
#include <WiFi.h>
#include "../InverterResult.hpp"

class SolaxModbusDongleAPI
{
public:
    SolaxModbusDongleAPI()
    {
    }

    InverterData_t loadData(String ipAddress)
    {
        InverterData_t inverterData;

        if (!isSupportedDongle)
        {
            inverterData.status = DONGLE_STATUS_UNSUPPORTED_DONGLE;
            log_d("Unsupported dongle");
            return inverterData;
        }

        if (!channel.connect(getIp(ipAddress), 502))
        {
            log_d("Failed to connect to Solax Modbus dongle");
            inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
            channel.disconnect();
            ip = IPAddress(0, 0, 0, 0); // Reset IP address
            return inverterData;
        }

        inverterData.millis = millis();
        ModbusResponse response;

        response = channel.sendModbusRequest(1, 0x03, 0x0, 0x014 - 0x00 + 1);
        if (response.isValid)
        {
            inverterData.status = DONGLE_STATUS_OK;
            inverterData.sn = response.readString(0x00, 14);
            log_d("SN: %s", inverterData.sn.c_str());
            String factoryName = response.readString(0x07, 14);
            String moduleName = response.readString(0x0E, 14);
            log_d("Factory Name: %s, Module Name: %s", factoryName.c_str(), moduleName.c_str());
        }
        else
        {
            inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
            log_d("Failed to read SN or module name");
            channel.disconnect();
            return inverterData;
        }

        response = channel.sendModbusRequest(1, 0x04, 0x0, 0x54 - 0x00 + 1);
        if (response.isValid)
        {
            inverterData.inverterPower = response.readInt16(0x02);
            inverterData.pv1Power = response.readUInt16(0x0A);
            inverterData.pv2Power = response.readUInt16(0x0B);
            inverterData.inverterTemperature = response.readInt16(0x08);
            if (isGen5(inverterData.sn))
            {
                inverterData.inverterTemperature /= 10;
            }
            inverterData.soc = response.readUInt16(0x1C);
            inverterData.batteryPower = response.readInt16(0x16);
            inverterData.batteryVoltage = response.readInt16(0x14) / 10.0f;
            inverterData.feedInPower = response.readInt32LSB(0x46);
            inverterData.batteryChargedToday = response.readUInt16(0x23) / 10.0f;
            inverterData.batteryDischargedToday = response.readUInt16(0x20) / 10.0f;
            inverterData.batteryTemperature = response.readInt16(0x18);
            inverterData.gridBuyTotal = response.readUInt32LSB(0x4A) / 100.0f;
            inverterData.gridSellTotal = response.readUInt32LSB(0x48) / 100.0f;
            inverterData.loadPower = inverterData.inverterPower - inverterData.feedInPower;
            inverterData.pvTotal = response.readUInt32LSB(0x52) / 10.0f;
            inverterData.loadToday = response.readUInt16(0x50) / 10.0f;
        }
        else
        {
            inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
            log_d("Failed to read inverter data");
            channel.disconnect();
            return inverterData;
        }

        response = channel.sendModbusRequest(1, 0x04, 0x91, 0x9A - 0x91 + 2);
        if (response.isValid)
        {
            inverterData.gridBuyToday = response.readUInt32LSB(0x98) / 100.0f;
            inverterData.gridSellToday = response.readUInt32LSB(0x9A) / 100.0f;
            inverterData.loadToday -= inverterData.gridSellToday; // Adjust load today by grid sell
            inverterData.loadToday += inverterData.gridBuyToday;  // Adjust load today by grid buy
            inverterData.pvToday = response.readUInt16(0x96) / 10.0f;
        }
        else
        {
            inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
            log_d("Failed to read PV data");
            channel.disconnect();
            return inverterData;
        }

        response = channel.sendModbusRequest(1, 0x04, 0x6A, 0x84 - 0x6A + 2);
        if (response.isValid)
        {
            inverterData.L1Power = response.readInt16(0x6C);
            inverterData.L2Power = response.readInt16(0x70);
            inverterData.L3Power = response.readInt16(0x74);

            uint16_t offgridL1Power = response.readInt16(0x78);
            uint16_t offgridL2Power = response.readInt16(0x7C);
            uint16_t offgridL3Power = response.readInt16(0x80);

            inverterData.inverterPower += offgridL1Power + offgridL2Power + offgridL3Power;
            inverterData.L1Power += offgridL1Power;
            inverterData.L2Power += offgridL2Power;
            inverterData.L3Power += offgridL3Power;

            inverterData.inverterPower = inverterData.L1Power + inverterData.L2Power + inverterData.L3Power;
            inverterData.loadPower = inverterData.inverterPower - inverterData.feedInPower;
        }
        else
        {
            inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
            log_d("Failed to read inverter data");
            channel.disconnect();
            return inverterData;
        }

        response = channel.sendModbusRequest(1, 0x04, 0x0124, 1);
        if (response.isValid)
        {
            inverterData.pv3Power = response.readInt16(0x0124);
        }
        else
        {
            inverterData.status = DONGLE_STATUS_CONNECTION_ERROR;
            log_d("Failed to read inverter data");
            channel.disconnect();
            return inverterData;
        }

        logInverterData(inverterData);
        channel.disconnect();
        return inverterData; // Placeholder for actual implementation
    }

protected:
    ModbusTCP channel;
    bool isSupportedDongle = true;
    IPAddress ip;
    IPAddress getIp(String ipAddress)
    {
        if(ip == IPAddress(0, 0, 0, 0)) {
            ip = IPAddress(ipAddress.c_str());
        }

        if (ip == IPAddress(0, 0, 0, 0))
        {
            mdns_result_t *results = NULL;
            mdns_init();
            esp_err_t err = mdns_query_ptr("_pocketseries", "_tcp", 5000, 20, &results);
            if (err == ESP_OK)
            {
                if (results)
                {
                    mdns_result_t *r = results;
                    while (r)
                    {
                        log_d("Found result: %s, type: %s, proto: %s, hostname: %s, port: %d",
                              r->instance_name, r->service_type, r->proto, r->hostname, r->port);
                        ip = r->addr->addr.u_addr.ip4.addr;
                        break;

                        r = r->next;
                    }
                    mdns_query_results_free(results);
                }
                else
                {
                    log_d("No results found for _pocketseries._tcp");
                }
            }
            if (ip == IPAddress(0, 0, 0, 0))
            {
                if (WiFi.localIP()[0] == 192)
                {
                    ip = IPAddress(192, 168, 10, 10);
                }
                else
                {
                    ip = IPAddress(5, 8, 8, 8);
                }
            }
        }
        log_d("Using IP: %s", ip.toString().c_str());
        return ip;
    }

    bool isGen5(String sn)
    {
        return sn.startsWith("H35") || sn.startsWith("H3B");
    }
};