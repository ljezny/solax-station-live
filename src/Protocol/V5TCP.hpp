#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <CRC.h>
#include <CRC16.h>
#include "utils/CustomNetworkClient.hpp"
#include "Inverters/InverterResult.hpp"

#define V5TCP_READ_TIMEOUT_MS 2000

// Error codes for V5TCP communication
enum class V5Error {
    OK = 0,
    CONNECTION_FAILED = -1,
    SEND_FAILED = -2,
    TIMEOUT = -3,
    INVALID_HEADER = -4,
    BUFFER_TOO_SMALL = -5,
    INVALID_RESPONSE = -6,
    INVALID_SENSOR = -7,
    INCOMPLETE_READ = -8,
    INVALID_SN = -9,
    CRC_ERROR = -10
};

class V5TCP
{
private:
    CustomNetworkClient client;
    uint8_t sequenceNumber = 0;
    static constexpr int MAX_RETRIES = 3;
    V5Error lastError = V5Error::OK;
    String lastErrorMessage;

public:
    IPAddress ip;
    
    V5Error getLastError() const { return lastError; }
    String getLastErrorMessage() const { return lastErrorMessage; }
    
    const char* errorToString(V5Error err) {
        switch(err) {
            case V5Error::OK: return "OK";
            case V5Error::CONNECTION_FAILED: return "Connection failed";
            case V5Error::SEND_FAILED: return "Send failed";
            case V5Error::TIMEOUT: return "Read timeout - dongle not responding";
            case V5Error::INVALID_HEADER: return "Invalid header (expected 0xA5)";
            case V5Error::BUFFER_TOO_SMALL: return "Buffer too small";
            case V5Error::INVALID_RESPONSE: return "Invalid response packet type";
            case V5Error::INVALID_SENSOR: return "Invalid sensor in response";
            case V5Error::INCOMPLETE_READ: return "Incomplete read";
            case V5Error::INVALID_SN: return "Invalid serial number";
            case V5Error::CRC_ERROR: return "CRC verification failed";
            default: return "Unknown error";
        }
    }

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
            log_d("Failed to connect to V5TCP at %s", ip.toString().c_str());
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
            lastError = V5Error::INVALID_SN;
            lastErrorMessage = "SN is zero";
            log_e("SN is zero, cannot send request");
            return false;
        }
        if (sn == 0xFFFFFFFF)
        {
            lastError = V5Error::INVALID_SN;
            lastErrorMessage = "SN overflow (0xFFFFFFFF) - check SN length";
            log_e("SN is 0xFFFFFFFF (overflow), cannot send request. Check if SN has correct length (10 digits).");
            return false;
        }
        sequenceNumber++;

        uint8_t modbusRTURequest[] = {0x1, 0x03, 0, 0, 0, 0, 0, 0};
        modbusRTURequest[2] = addr >> 8;
        modbusRTURequest[3] = addr & 0xff;
        modbusRTURequest[5] = len;
        unsigned c = crc16(modbusRTURequest, 6, 0x8005, 0xFFFF, 0, true, true);
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

        log_d("Sending solarmanv5 request. Sequence: %d, SN: %lu, Addr: %d, Len: %d",
              sequenceNumber, sn, addr, len);

        size_t requestSize = sizeof(request);

        String dump = "";
        for (size_t i = 0; i < requestSize; i++)
        {
            dump += String(request[i], HEX) + " ";
        }
        log_d("Request: %s", dump.c_str());

        bool result = client.write(request, requestSize) == requestSize;
        
        // Small delay after sending request - some dongles need time to process
        if (result) {
            delay(50);
        }
        
        return result;
    }

    int readModbusRTUResponse(byte *packetBuffer, size_t bufferLength)
    {
        int bytesRead = client.read(packetBuffer, 1);
        if (bytesRead <= 0)
        {
            lastError = V5Error::TIMEOUT;
            lastErrorMessage = String("No response from dongle (read returned ") + bytesRead + "). Dongle may be offline, busy, or SN mismatch.";
            log_e("Read timeout waiting for response. Dongle at %s not responding. Check: 1) Dongle is online 2) SN matches dongle 3) Dongle not busy with cloud", ip.toString().c_str());
            return -1;
        }
        if (packetBuffer[0] != 0xA5)
        {
            lastError = V5Error::INVALID_HEADER;
            lastErrorMessage = String("Expected 0xA5, got 0x") + String(packetBuffer[0], HEX);
            log_e("Invalid header: expected 0xA5, got 0x%02X", packetBuffer[0]);
            return -1;
        }
        if (client.read(packetBuffer, 2) != 2)
        {
            lastError = V5Error::INCOMPLETE_READ;
            lastErrorMessage = "Failed to read packet length";
            log_e("Failed to read packet length");
            return -1;
        }
        uint16_t length = packetBuffer[0] | (packetBuffer[1] << 8);
        if (length > bufferLength)
        {
            lastError = V5Error::BUFFER_TOO_SMALL;
            lastErrorMessage = String("Packet length ") + length + " exceeds buffer " + bufferLength;
            log_e("Buffer too small: packet %d, buffer %d", length, bufferLength);
            return -1;
        }

        log_d("Payload length: %d", length);
        if (client.read(packetBuffer, 8) != 8)
        { // read rest of header
            lastError = V5Error::INCOMPLETE_READ;
            lastErrorMessage = "Failed to read header";
            log_e("Failed to read header (8 bytes)");
            return -1;
        }

        if (packetBuffer[0] != 0x10 || packetBuffer[1] != 0x15)
        {
            lastError = V5Error::INVALID_RESPONSE;
            lastErrorMessage = String("Expected 0x10 0x15, got 0x") + String(packetBuffer[0], HEX) + " 0x" + String(packetBuffer[1], HEX);
            log_e("Invalid response type: 0x%02X 0x%02X (expected 0x10 0x15)", packetBuffer[0], packetBuffer[1]);
            return -1;
        }

        int PAYLOAD_HEADER = 14;

        if (client.read(packetBuffer, PAYLOAD_HEADER) != PAYLOAD_HEADER)
        { // payload header
            lastError = V5Error::INCOMPLETE_READ;
            lastErrorMessage = "Failed to read payload header";
            log_e("Failed to read payload header (%d bytes)", PAYLOAD_HEADER);
            return -1;
        }

        if (packetBuffer[0] != 0x02 || packetBuffer[1] != 0x01)
        {
            lastError = V5Error::INVALID_SENSOR;
            lastErrorMessage = String("Invalid sensor: 0x") + String(packetBuffer[0], HEX) + " 0x" + String(packetBuffer[1], HEX);
            log_e("Invalid sensor in response: 0x%02X 0x%02X", packetBuffer[0], packetBuffer[1]);
            return -1;
        }

        int MODBUS_RTU_FRAME_LENGTH = length - PAYLOAD_HEADER;
        if (client.read(packetBuffer, MODBUS_RTU_FRAME_LENGTH) != MODBUS_RTU_FRAME_LENGTH)
        { // modbus rtu packet
            lastError = V5Error::INCOMPLETE_READ;
            lastErrorMessage = "Failed to read Modbus RTU frame";
            log_e("Failed to read Modbus RTU frame (%d bytes)", MODBUS_RTU_FRAME_LENGTH);
            return -1;
        }

        byte trailerBuffer[2];
        if (client.read(trailerBuffer, 2) != 2)
        {
            // read trailer - not critical, just log warning
            log_w("Failed to read trailer (2 bytes)");
        }
        
        lastError = V5Error::OK;
        lastErrorMessage = "";
        
        String dump = "";
        for (int i = 0; i < MODBUS_RTU_FRAME_LENGTH; i++)
        {
            dump += String(packetBuffer[i], HEX) + " ";
        }
        log_d("Response: %s", dump.c_str());
        return MODBUS_RTU_FRAME_LENGTH;
    }

    bool tryReadWithRetries(uint16_t startReg, uint16_t length, uint32_t sn, byte *buffer, std::function<void()> onSuccess)
    {
        lastError = V5Error::OK;
        
        for (int i = 0; i < MAX_RETRIES; ++i)
        {
            log_d("Attempt %d/%d for register 0x%04X (SN: %lu)", i + 1, MAX_RETRIES, startReg, sn);
            
            if (connect(ip))
            {
                if (sendReadDataRequest(startReg, length, sn))
                {
                    int result = readModbusRTUResponse(buffer, 1024);
                    if (result > 0)
                    {
                        log_i("Successfully read %d bytes from register 0x%04X", result, startReg);
                        onSuccess();
                        disconnect();
                        return true;
                    }
                    else
                    {
                        log_w("Read failed for 0x%04X (attempt %d/%d): %s", 
                              startReg, i + 1, MAX_RETRIES, errorToString(lastError));
                    }
                }
                else
                {
                    log_w("Send request failed for 0x%04X (attempt %d/%d): %s", 
                          startReg, i + 1, MAX_RETRIES, errorToString(lastError));
                }
                disconnect();
                
                // Small delay between retries
                if (i < MAX_RETRIES - 1) {
                    delay(100);
                }
            }
            else
            {
                lastError = V5Error::CONNECTION_FAILED;
                lastErrorMessage = String("Cannot connect to ") + ip.toString() + ":8899";
                log_w("Failed to connect to dongle at %s (attempt %d/%d)", 
                      ip.toString().c_str(), i + 1, MAX_RETRIES);
            }
        }
        
        log_e("All %d attempts failed for register 0x%04X. Last error: %s - %s", 
              MAX_RETRIES, startReg, errorToString(lastError), lastErrorMessage.c_str());
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
};