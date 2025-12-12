#pragma once

#include <Arduino.h>
#include "../utils/RemoteLogger.hpp"
#include <WiFi.h>
#include <CRC.h>
#include <CRC16.h>
#include "utils/CustomNetworkClient.hpp"
#include "Inverters/InverterResult.hpp"

#define READ_TIMEOUT 2000

class V5TCP
{
private:
    CustomNetworkClient client;
    uint8_t sequenceNumber = 0;
    static constexpr int MAX_RETRIES = 3;

public:
    IPAddress ip;
    uint16_t readUInt16(byte *buf, byte reg)
    {
        return (buf[3 + reg * 2] << 8 | buf[3 + reg * 2 + 1]);
    }

    int16_t readInt16(byte *buf, byte reg)
    {
        return (buf[3 + reg * 2] << 8 | buf[3 + reg * 2 + 1]);
    }

    uint32_t readUInt32(byte *buf, byte reg)
    {
        return readUInt16(buf, reg) << 16 | readUInt16(buf, reg + 1);
    }

    uint32_t readUInt32L(byte *buf, byte reg)
    {
        return (buf[3 + reg * 2 + 1] << 16 | buf[3 + reg * 2]);
    }

    int32_t readInt32(byte *buf, byte reg)
    {
        return readInt16(buf, reg) << 16 | readUInt16(buf, reg + 1);
    }

    float readIEEE754(byte *buf, byte reg)
    {
        uint32_t v = readUInt32(buf, reg);
        return *(float *)&v;
    }

    String readString(byte *buf, byte reg, uint8_t length)
    {
        String str = "";
        for (uint8_t i = 0; i < length; i++)
        {
            uint8_t index = 3 + reg * 2 + i; // 3 is the offset for the header
            if (index < RX_BUFFER_SIZE)
            {
                str += (char)buf[index];
            }
        }
        return str;
    }

    bool connect(IPAddress ip)
    {
        if (!client.connect(ip, 8899))
        {
            LOGD("Failed to connect to V5TCP at %s", ip.toString().c_str());
            return false;
        }
        return true;
    }

    void disconnect()
    {
        client.stop();
    }

    bool sendReadDataRequest(uint16_t addr, uint8_t len, uint32_t sn)
    {
        if (sn == 0)
        {
            LOGD("SN is zero, cannot send request");
            return false;
        }
        sequenceNumber++;

        uint8_t modbusRTURequest[] = {0x1, 0x03, 0, 0, 0, 0, 0, 0};
        modbusRTURequest[2] = addr >> 8;
        modbusRTURequest[3] = addr & 0xff;
        modbusRTURequest[5] = len;
        unsigned c = calcCRC16(modbusRTURequest, 6, 0x8005, 0xFFFF, 0, true, true);
        modbusRTURequest[6] = c;
        modbusRTURequest[7] = c >> 8;

        uint8_t request[] = {
            0xA5 /*Start of packet*/,
            0x17 /*Packet length*/,
            0x00 /*Packet length*/,
            0x10 /*Packet type, request*/,
            0x45 /*Packet type, request*/,
            (uint8_t) (sequenceNumber & 0xff) /*Request number*/,
            0 /*Request number*/,
            (uint8_t) (sn & 0xff) /*Serial number*/,
            (uint8_t) ((sn >> 8) & 0xff) /*Serial number*/,
            (uint8_t) ((sn >> 16) & 0xff) /*Serial number*/,
            (uint8_t) ((sn >> 24) & 0xff) /*Serial number*/, 0x02 /*Frame type*/,
            0x00 /*Sensor type*/,
            0x00 /*Sensor type*/,
            0x00 /*Total Working Time*/,
            0x00 /*Total Working Time*/,
            0x00 /*Total Working Time*/,
            0x00 /*Total Working Time*/,
            0x00 /*Power on Time*/,
            0x00 /*Power on Time*/,
            0x00 /*Power on Time*/,
            0x00 /*Power on Time*/,
            0x00 /*Offset time*/,
            0x00 /*Offset time*/,
            0x00 /*Offset time*/,
            0x00 /*Offset time*/,
            modbusRTURequest[0],
            modbusRTURequest[1],
            modbusRTURequest[2],
            modbusRTURequest[3],
            modbusRTURequest[4],
            modbusRTURequest[5],
            modbusRTURequest[6],
            modbusRTURequest[7],
            0,
            0x15};

        int checksum = 0;
        for (int i = 1; i < sizeof(request) - 2; i++)
        {
            checksum += request[i] & 0xff;
        }
        request[sizeof(request) - 2] = checksum & 0xff;

        LOGD("Sending solarmanv5 request. Sequence: %d, SN: %lu, Addr: %d, Len: %d",
              sequenceNumber, sn, addr, len);

        size_t requestSize = sizeof(request);

        String dump = "";
        for (size_t i = 0; i < requestSize; i++)
        {
            dump += String(request[i], HEX) + " ";
        }
        LOGD("Request: %s", dump.c_str());

        bool result = client.write(request, requestSize) == requestSize;
        return result;
    }

    int readModbusRTUResponse(byte *packetBuffer, size_t bufferLength)
    {
        if (client.read(packetBuffer, 1) != 1)
        {
            LOGD("Unable to read client.");
            return -1;
        }
        if (packetBuffer[0] != 0xA5)
        {
            LOGD("Invalid header");
            return -1;
        }
        if (client.read(packetBuffer, 2) != 2)
        {
            LOGD("Unable to read client.");
            return -1;
        }
        uint16_t length = packetBuffer[0] | (packetBuffer[1] << 8);
        if (length > bufferLength)
        {
            LOGD("Buffer too small");
            return -1;
        }

        LOGD("Payload length: %d", length);
        if (client.read(packetBuffer, 8) != 8)
        { // read rest of header
            LOGD("Unable to read client.");
            return -1;
        }

        if (packetBuffer[0] != 0x10 || packetBuffer[1] != 0x15)
        {
            LOGD("Invalid response");
            return -1;
        }

        int PAYLOAD_HEADER = 14;

        if (client.read(packetBuffer, PAYLOAD_HEADER) != PAYLOAD_HEADER)
        { // payload header
            LOGD("Unable to read client.");
            return -1;
        }

        if (packetBuffer[0] != 0x02 || packetBuffer[1] != 0x01)
        {

            LOGD("Invalid sensor in response");
            return -1;
        }

        int MODBUS_RTU_FRAME_LENGTH = length - PAYLOAD_HEADER;
        if (client.read(packetBuffer, MODBUS_RTU_FRAME_LENGTH) != MODBUS_RTU_FRAME_LENGTH)
        { // modbus rtu packet
            LOGD("Unable to read client.");
            return -1;
        }

        // for (int i = 0; i < MODBUS_RTU_FRAME_LENGTH; i++)
        // {
        //     LOGD("%02X ", packetBuffer[i]);
        // }

        byte trailerBuffer[2];
        if (client.read(trailerBuffer, 2) != 2)
        {
            // read trailer
            LOGD("Unable to read client.");
        }
        String dump = "";
        for (int i = 0; i < MODBUS_RTU_FRAME_LENGTH; i++)
        {
            dump += String(packetBuffer[i], HEX) + " ";
        }
        LOGD("Request: %s", dump.c_str());
        return MODBUS_RTU_FRAME_LENGTH;
    }

    bool tryReadWithRetries(uint16_t startReg, uint16_t length, uint32_t sn, byte *buffer, std::function<void()> onSuccess)
    {
        for (int i = 0; i < MAX_RETRIES; ++i)
        {
            if (connect(ip))
            {
                if (sendReadDataRequest(startReg, length, sn))
                {
                    if (readModbusRTUResponse(buffer, 1024) > 0)
                    {
                        onSuccess();
                        disconnect();
                        return true;
                    }
                    else
                    {
                        LOGD("Read failed for 0x%04X", startReg);
                    }
                }
                else
                {
                    LOGD("Send request failed for 0x%04X", startReg);
                }
                disconnect();
            }
            else
            {
                LOGD("Failed to connect to dongle at %s", ip.toString().c_str());
            }
        }
        return false;
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

                LOGD("Received IP address: %s", String(d).c_str());
                int indexOfComma = String(d).indexOf(',');
                String ip = String(d).substring(0, indexOfComma);
                LOGD("Parsed IP address: %s", ip.c_str());
                dongleIP.fromString(ip);
                LOGD("Dongle IP: %s", dongleIP.toString());
                break;
            }
        }
        udp.stop();
        return dongleIP;
    }

    void ensureIPAddress(const String &ipAddress)
    {
        if (ip == IPAddress(0, 0, 0, 0))
        {
            if (!ipAddress.isEmpty())
            {
                ip.fromString(ipAddress);
            }
            if (ip == IPAddress(0, 0, 0, 0))
            {
                ip = discoverDongleIP();
                if (ip == IPAddress(0, 0, 0, 0))
                    ip = IPAddress(10, 10, 100, 254); // fallback
            }
        }
    }

    /**
     * Write a single holding register (Modbus function code 0x06)
     * 
     * @param addr Register address
     * @param value Value to write
     * @param sn Dongle serial number
     * @return true if write was successful
     */
    bool writeSingleRegister(uint16_t addr, uint16_t value, uint32_t sn)
    {
        if (sn == 0)
        {
            LOGD("SN is zero, cannot send write request");
            return false;
        }

        // Build Modbus RTU frame for function code 0x06 (Write Single Register)
        // Format: [slave_id, function_code, addr_hi, addr_lo, value_hi, value_lo, crc_lo, crc_hi]
        uint8_t modbusRTURequest[] = {0x01, 0x06, 0, 0, 0, 0, 0, 0};
        modbusRTURequest[2] = addr >> 8;
        modbusRTURequest[3] = addr & 0xff;
        modbusRTURequest[4] = value >> 8;
        modbusRTURequest[5] = value & 0xff;
        unsigned c = calcCRC16(modbusRTURequest, 6, 0x8005, 0xFFFF, 0, true, true);
        modbusRTURequest[6] = c;
        modbusRTURequest[7] = c >> 8;

        return sendV5WriteRequest(modbusRTURequest, sizeof(modbusRTURequest), sn);
    }

    /**
     * Write multiple holding registers (Modbus function code 0x10)
     * 
     * @param startAddr Starting register address
     * @param values Array of 16-bit values to write
     * @param count Number of registers to write
     * @param sn Dongle serial number
     * @return true if write was successful
     */
    bool writeMultipleRegisters(uint16_t startAddr, const uint16_t* values, uint16_t count, uint32_t sn)
    {
        if (sn == 0)
        {
            LOGD("SN is zero, cannot send write request");
            return false;
        }
        if (count == 0 || count > 123)
        {
            LOGD("Invalid register count: %d", count);
            return false;
        }

        // Build Modbus RTU frame for function code 0x10 (Write Multiple Registers)
        // Format: [slave_id, function_code, addr_hi, addr_lo, count_hi, count_lo, byte_count, data..., crc_lo, crc_hi]
        uint8_t byteCount = count * 2;
        size_t frameLen = 7 + byteCount + 2;  // header (7) + data + CRC (2)
        uint8_t* modbusRTURequest = new uint8_t[frameLen];
        
        modbusRTURequest[0] = 0x01;  // Slave ID
        modbusRTURequest[1] = 0x10;  // Function code: Write Multiple Registers
        modbusRTURequest[2] = startAddr >> 8;
        modbusRTURequest[3] = startAddr & 0xff;
        modbusRTURequest[4] = count >> 8;
        modbusRTURequest[5] = count & 0xff;
        modbusRTURequest[6] = byteCount;
        
        // Copy register values (big endian)
        for (uint16_t i = 0; i < count; i++)
        {
            modbusRTURequest[7 + i * 2] = values[i] >> 8;
            modbusRTURequest[7 + i * 2 + 1] = values[i] & 0xff;
        }
        
        // Calculate and append CRC
        unsigned c = calcCRC16(modbusRTURequest, 7 + byteCount, 0x8005, 0xFFFF, 0, true, true);
        modbusRTURequest[7 + byteCount] = c;
        modbusRTURequest[8 + byteCount] = c >> 8;

        bool result = sendV5WriteRequest(modbusRTURequest, frameLen, sn);
        delete[] modbusRTURequest;
        return result;
    }

    /**
     * Write multiple registers from raw bytes (Modbus function code 0x10)
     * Useful when data is already in network byte order
     * 
     * @param startAddr Starting register address
     * @param data Raw data bytes (already in big endian)
     * @param byteCount Number of bytes to write (must be even)
     * @param sn Dongle serial number
     * @return true if write was successful
     */
    bool writeMultipleRegistersRaw(uint16_t startAddr, const uint8_t* data, uint8_t byteCount, uint32_t sn)
    {
        if (sn == 0)
        {
            LOGD("SN is zero, cannot send write request");
            return false;
        }
        if (byteCount == 0 || byteCount > 246 || (byteCount % 2) != 0)
        {
            LOGD("Invalid byte count: %d", byteCount);
            return false;
        }

        uint16_t regCount = byteCount / 2;
        size_t frameLen = 7 + byteCount + 2;  // header (7) + data + CRC (2)
        uint8_t* modbusRTURequest = new uint8_t[frameLen];
        
        modbusRTURequest[0] = 0x01;  // Slave ID
        modbusRTURequest[1] = 0x10;  // Function code: Write Multiple Registers
        modbusRTURequest[2] = startAddr >> 8;
        modbusRTURequest[3] = startAddr & 0xff;
        modbusRTURequest[4] = regCount >> 8;
        modbusRTURequest[5] = regCount & 0xff;
        modbusRTURequest[6] = byteCount;
        
        // Copy raw data
        memcpy(modbusRTURequest + 7, data, byteCount);
        
        // Calculate and append CRC
        unsigned c = calcCRC16(modbusRTURequest, 7 + byteCount, 0x8005, 0xFFFF, 0, true, true);
        modbusRTURequest[7 + byteCount] = c;
        modbusRTURequest[8 + byteCount] = c >> 8;

        bool result = sendV5WriteRequest(modbusRTURequest, frameLen, sn);
        delete[] modbusRTURequest;
        return result;
    }

private:
    /**
     * Send a Solarman V5 frame containing a Modbus RTU write request and wait for response
     * 
     * V5 Frame structure:
     * - Start: 0xA5 (1 byte)
     * - Length: payload length (2 bytes, little endian)
     * - Control Code: 0x4510 for request (2 bytes, little endian)
     * - Sequence Number: (2 bytes)
     * - Logger Serial Number: (4 bytes, little endian)
     * - Frame Type: 0x02 for inverter (1 byte)
     * - Sensor Type: 0x0000 (2 bytes)
     * - Total Working Time: 0x00000000 (4 bytes)
     * - Power On Time: 0x00000000 (4 bytes)
     * - Offset Time: 0x00000000 (4 bytes)
     * - Modbus RTU Frame: (variable)
     * - Checksum: sum of all bytes from Length to end of Modbus frame (1 byte)
     * - End: 0x15 (1 byte)
     */
    bool sendV5WriteRequest(const uint8_t* modbusFrame, size_t modbusFrameLen, uint32_t sn)
    {
        sequenceNumber++;

        // V5 payload length = 15 (fixed header) + modbus frame length
        uint16_t payloadLength = 15 + modbusFrameLen;
        
        // Total frame size = 1 (start) + 2 (length) + payload + 1 (checksum) + 1 (end) = payload + 5
        // But length field contains payload length only
        size_t frameSize = 11 + payloadLength + 2;  // header(11) + payload(15+modbus) + checksum(1) + end(1)
        
        uint8_t* v5Frame = new uint8_t[frameSize];
        int idx = 0;
        
        // Start
        v5Frame[idx++] = 0xA5;
        
        // Length (little endian)
        v5Frame[idx++] = payloadLength & 0xFF;
        v5Frame[idx++] = (payloadLength >> 8) & 0xFF;
        
        // Control Code: REQUEST = 0x4510 (little endian)
        v5Frame[idx++] = 0x10;
        v5Frame[idx++] = 0x45;
        
        // Sequence Number (little endian)
        v5Frame[idx++] = sequenceNumber & 0xFF;
        v5Frame[idx++] = (sequenceNumber >> 8) & 0xFF;
        
        // Logger Serial Number (little endian)
        v5Frame[idx++] = sn & 0xFF;
        v5Frame[idx++] = (sn >> 8) & 0xFF;
        v5Frame[idx++] = (sn >> 16) & 0xFF;
        v5Frame[idx++] = (sn >> 24) & 0xFF;
        
        // Frame Type: 0x02 (Solar Inverter)
        v5Frame[idx++] = 0x02;
        
        // Sensor Type: 0x0000
        v5Frame[idx++] = 0x00;
        v5Frame[idx++] = 0x00;
        
        // Total Working Time: 0x00000000
        v5Frame[idx++] = 0x00;
        v5Frame[idx++] = 0x00;
        v5Frame[idx++] = 0x00;
        v5Frame[idx++] = 0x00;
        
        // Power On Time: 0x00000000
        v5Frame[idx++] = 0x00;
        v5Frame[idx++] = 0x00;
        v5Frame[idx++] = 0x00;
        v5Frame[idx++] = 0x00;
        
        // Offset Time: 0x00000000
        v5Frame[idx++] = 0x00;
        v5Frame[idx++] = 0x00;
        v5Frame[idx++] = 0x00;
        v5Frame[idx++] = 0x00;
        
        // Modbus RTU Frame
        memcpy(v5Frame + idx, modbusFrame, modbusFrameLen);
        idx += modbusFrameLen;
        
        // Calculate checksum: sum of all bytes from index 1 to idx-1
        uint8_t checksum = 0;
        for (int i = 1; i < idx; i++)
        {
            checksum += v5Frame[i];
        }
        v5Frame[idx++] = checksum;
        
        // End
        v5Frame[idx++] = 0x15;

        // Log the frame
        String dump = "";
        for (int i = 0; i < idx; i++)
        {
            dump += String(v5Frame[i], HEX) + " ";
        }
        LOGD("Sending V5 write frame: %s", dump.c_str());

        // Send the frame
        size_t written = client.write(v5Frame, idx);
        delete[] v5Frame;
        
        if (written != idx)
        {
            LOGD("Failed to send V5 write frame");
            return false;
        }

        // Read response
        byte responseBuffer[256];
        int respLen = readV5Response(responseBuffer, sizeof(responseBuffer));
        if (respLen < 0)
        {
            LOGD("Failed to read V5 write response");
            return false;
        }

        // Validate response - check if it's a valid V5 response with matching sequence
        // Response control code should be 0x1510 (response to 0x4510 request)
        if (respLen < 11)
        {
            LOGD("V5 write response too short: %d bytes", respLen);
            return false;
        }

        // Log response
        dump = "";
        for (int i = 0; i < respLen; i++)
        {
            dump += String(responseBuffer[i], HEX) + " ";
        }
        LOGD("V5 write response: %s", dump.c_str());

        // Check for Modbus exception in response
        // The Modbus RTU frame starts at byte 25 (after V5 header)
        // If function code has high bit set (0x80+), it's an exception
        if (respLen >= 28)
        {
            uint8_t functionCode = responseBuffer[26];  // After V5 header (25 bytes) + slave ID (1 byte)
            if (functionCode & 0x80)
            {
                uint8_t exceptionCode = responseBuffer[27];
                LOGD("Modbus exception in write response: function=0x%02X, exception=%d", functionCode, exceptionCode);
                return false;
            }
        }

        LOGD("V5 write request successful");
        return true;
    }

    /**
     * Read a complete V5 response frame
     * @return Number of bytes read, or -1 on error
     */
    int readV5Response(byte* buffer, size_t bufferSize)
    {
        // Read start byte
        if (client.read(buffer, 1) != 1 || buffer[0] != 0xA5)
        {
            LOGD("Invalid V5 response start byte");
            return -1;
        }

        // Read length (2 bytes, little endian)
        if (client.read(buffer + 1, 2) != 2)
        {
            LOGD("Failed to read V5 response length");
            return -1;
        }
        uint16_t length = buffer[1] | (buffer[2] << 8);

        if (length + 5 > bufferSize)
        {
            LOGD("V5 response too large: %d bytes", length + 5);
            return -1;
        }

        // Read rest of frame: header remainder (8) + payload (length) + checksum (1) + end (1)
        int remaining = 8 + length + 2;
        if (client.read(buffer + 3, remaining) != remaining)
        {
            LOGD("Failed to read V5 response body");
            return -1;
        }

        // Verify end byte
        int totalLen = 3 + remaining;
        if (buffer[totalLen - 1] != 0x15)
        {
            LOGD("Invalid V5 response end byte: 0x%02X", buffer[totalLen - 1]);
            return -1;
        }

        return totalLen;
    }
};