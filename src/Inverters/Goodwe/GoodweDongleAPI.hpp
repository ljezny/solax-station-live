#pragma once

#include <Arduino.h>
#include "../../Protocol/ModbusRTU.hpp"
#include "../../Protocol/ModbusTCP.hpp"
#include "Inverters/InverterResult.hpp"

class GoodweDongleAPI
{
public:
    InverterData_t loadData(String sn)
    {
        return readData(sn);
    }

private:
    ModbusRTU rtuChannel;
    ModbusTCP tcpChannel;

    double gridBuyTotal = 0;
    double gridSellTotal = 0;
    int day = -1;

    ModbusResponse sendRunningDataRequestPacket()
    {
        if(tcpChannel.isConnected())
        {
            return tcpChannel.sendModbusRequest(0xF7, 0x03, 35100, 125);
        }
        return rtuChannel.sendDataRequest(IPAddress(10, 10, 100, 253), 8899, 35100, 125);
    }

    ModbusResponse sendBMSInfoRequestPacket()
    {
        if(tcpChannel.isConnected())
        {
            return tcpChannel.sendModbusRequest(0xF7, 0x03, 37000, 8);
        }
        return rtuChannel.sendDataRequest(IPAddress(10, 10, 100, 253), 8899, 37000, 8);
    }

    ModbusResponse sendSmartMeterRequestPacket()
    {
        if(tcpChannel.isConnected())
        {
            return tcpChannel.sendModbusRequest(0xF7, 0x03, 8899, 44);
        }
        return rtuChannel.sendDataRequest(IPAddress(10, 10, 100, 253), 8899, 36000, 44);
    }

    InverterData_t readData(String sn)
    {
        InverterData_t inverterData;
        log_d("Connecting to dongle...");
        if (tcpChannel.connect(IPAddress(10, 10, 100, 253), 502) || rtuChannel.connect())
        {
            log_d("Connected.");
            ModbusResponse response;
            for (int i = 0; i < 3; i++)
            {
                response = sendRunningDataRequestPacket();
                if (response.isValid)
                {
                    inverterData.status = DONGLE_STATUS_OK;
                    inverterData.millis = millis();
                    inverterData.pv1Power = response.readUInt32(5);
                    inverterData.pv2Power = response.readUInt32(9);
                    inverterData.pv3Power = response.readUInt32(13);
                    inverterData.pv4Power = response.readUInt32(17);

                    inverterData.batteryPower -= response.readInt16(83); // TODO: maybe sign readuw(84);
                    // _ac = readsw(40);
                    inverterData.L1Power = response.readInt16(25) + response.readInt16(50);
                    inverterData.L2Power = response.readInt16(30) + response.readInt16(56);
                    inverterData.L3Power = response.readInt16(35) + response.readInt16(62);
                    inverterData.inverterPower = inverterData.L1Power + inverterData.L2Power + inverterData.L3Power; // readInt16(38); //38 - total inverter power
                    bool backupConnectedToLoad = response.readUInt16(60) == 0x00;                                    // 0x00 - backup is connected to load, 0x01 - inverter disconnects to load
                    if (backupConnectedToLoad)
                    {
                        inverterData.loadPower = response.readInt16(72); // total load power
                    }
                    else
                    {
                        inverterData.loadPower = response.readInt16(70)    // total backup load power
                                                 + response.readInt16(72); // total load power
                    }
                    inverterData.inverterTemperature = response.readInt16(74) / 10;
                    inverterData.pvTotal = response.readUInt32(91) / 10.0;
                    inverterData.pvToday = response.readUInt32(93) / 10.0;
                    inverterData.loadToday = response.readUInt16(105) / 10.0;
                    inverterData.loadTotal = response.readUInt32(103) / 10.0;
                    inverterData.batteryChargedToday = response.readUInt16(108) / 10.0;
                    inverterData.batteryDischargedToday = response.readUInt16(111) / 10.0;
                    inverterData.sn = sn;
                    logInverterData(inverterData);

                    // this is a hack - Goodwe returns incorrect day values for grid sell/buy
                    // so count it manually from total values
                    int day = (response.readUInt16(1) >> 8) & 0xFF;
                    log_d("Day: %d", day);
                    if (this->day != day)
                    {
                        log_d("Day changed, resetting counters");
                        this->day = day;
                        gridBuyTotal = 0;
                        gridSellTotal = 0;
                    }
                    break;
                }
            }

            for (int i = 0; i < 3; i++)
            { // it is UDP so retries are needed
                response = sendSmartMeterRequestPacket();
                if (response.isValid)
                {
                    inverterData.gridSellTotal = response.readIEEE754(15) / 1000.0f;
                    inverterData.gridBuyTotal = response.readIEEE754(17) / 1000.0f;

                    // debug logging
                    int register25 = response.readInt32(25);
                    log_d("Register 25: %d", register25);
                    int register8 = response.readInt16(8);
                    log_d("Register 8: %d", register8);
                    // end debug logging

                    inverterData.feedInPower = response.readInt32(25);

                    log_d("Grid sell total: %f", inverterData.gridSellTotal);
                    log_d("Grid buy total: %f", inverterData.gridBuyTotal);
                    if (gridBuyTotal == 0)
                    {
                        gridBuyTotal = inverterData.gridBuyTotal;
                    }
                    if (gridSellTotal == 0)
                    {
                        gridSellTotal = inverterData.gridSellTotal;
                    }
                    inverterData.gridSellToday = inverterData.gridSellTotal - gridSellTotal;
                    inverterData.gridBuyToday = inverterData.gridBuyTotal - gridBuyTotal;
                    break;
                }
            }

            for (int i = 0; i < 3; i++)
            { // it is UDP so retries are needed
                response = sendBMSInfoRequestPacket();
                if (response.isValid)
                {

                    inverterData.batteryTemperature = response.readUInt16(3) / 10;
                    inverterData.soc = response.readUInt16(7);
                    break;
                }
            }
        }
        inverterData.hasBattery = inverterData.soc != 0 || inverterData.batteryPower != 0;
        tcpChannel.disconnect();
        rtuChannel.disconnect();
        logInverterData(inverterData);
        return inverterData;
    }
};