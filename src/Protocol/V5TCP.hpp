#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <CRC.h>
#include <CRC16.h>
#include "utils/CustomNetworkClient.hpp"
#include "Inverters/InverterResult.hpp"

// Increased timeout - dongles can be slow when busy with cloud communication
#define V5TCP_READ_TIMEOUT_MS 5000

// Maximum read attempts when receiving wrong sequence (cloud response)
#define V5TCP_MAX_READ_RETRIES 5

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
    CRC_ERROR = -10,
    SEQUENCE_MISMATCH = -11,
    SN_MISMATCH = -12,
    V5_CHECKSUM_ERROR = -13,
    HEARTBEAT_ONLY = -14
};

class V5TCP
{
private:
    CustomNetworkClient client;
    uint8_t sequenceNumber = 0;
    uint8_t lastSentSequence = 0;  // Track sent sequence for validation
    uint32_t expectedSN = 0;        // Track expected SN for validation
    static constexpr int MAX_RETRIES = 3;
    V5Error lastError = V5Error::OK;
    String lastErrorMessage;
    
    // Calculate V5 frame checksum (sum of bytes from index 1 to len-2) & 0xFF
    uint8_t calculateV5Checksum(const byte* frame, size_t frameLength) {
        uint8_t checksum = 0;
        for (size_t i = 1; i < frameLength - 2; i++) {
            checksum += frame[i];
        }
        return checksum & 0xFF;
    }

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
            case V5Error::SEQUENCE_MISMATCH: return "Sequence number mismatch (cloud response?)";
            case V5Error::SN_MISMATCH: return "Serial number mismatch in response";
            case V5Error::V5_CHECKSUM_ERROR: return "V5 frame checksum error";
            case V5Error::HEARTBEAT_ONLY: return "Only heartbeat frames received";
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
        lastSentSequence = sequenceNumber;  // Store for validation
        expectedSN = sn;                     // Store for validation

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
        // Try multiple reads in case we receive cloud responses or heartbeats
        for (int readAttempt = 0; readAttempt < V5TCP_MAX_READ_RETRIES; readAttempt++)
        {
            if (readAttempt > 0) {
                log_d("Read retry %d/%d (previous frame was not for us)", readAttempt + 1, V5TCP_MAX_READ_RETRIES);
            }
            
            // Read start byte
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
            
            // Read length (2 bytes)
            if (client.read(packetBuffer, 2) != 2)
            {
                lastError = V5Error::INCOMPLETE_READ;
                lastErrorMessage = "Failed to read packet length";
                log_e("Failed to read packet length");
                return -1;
            }
            uint16_t length = packetBuffer[0] | (packetBuffer[1] << 8);
            
            // Read rest of header (8 bytes: control code 2B + sequence 2B + SN 4B)
            byte headerBuffer[8];
            if (client.read(headerBuffer, 8) != 8)
            {
                lastError = V5Error::INCOMPLETE_READ;
                lastErrorMessage = "Failed to read header";
                log_e("Failed to read header (8 bytes)");
                return -1;
            }
            
            uint8_t controlCode1 = headerBuffer[0];  // Should be 0x10
            uint8_t controlCode2 = headerBuffer[1];  // 0x15 for response, 0x47 for heartbeat
            uint8_t respSequence = headerBuffer[2];
            // headerBuffer[3] is second seq byte (dongle's counter)
            uint32_t respSN = headerBuffer[4] | (headerBuffer[5] << 8) | (headerBuffer[6] << 16) | (headerBuffer[7] << 24);
            
            log_d("Frame: ctrl=0x%02X%02X seq=%d respSN=%lu (expected seq=%d, SN=%lu)", 
                  controlCode1, controlCode2, respSequence, respSN, lastSentSequence, expectedSN);
            
            // Check for heartbeat frame (0x47) - skip and continue reading
            if (controlCode2 == 0x47)
            {
                log_d("Received heartbeat frame (0x47), skipping and reading next frame");
                // Read and discard rest of heartbeat frame (just checksum + end = 2 bytes since length is typically 1)
                byte discardBuffer[32];
                int remainingBytes = length + 2;  // payload + checksum + end
                if (remainingBytes > 0 && remainingBytes < 32) {
                    client.read(discardBuffer, remainingBytes);
                }
                continue;  // Try reading next frame
            }
            
            // Validate control code for Modbus response (0x10 0x15)
            if (controlCode1 != 0x10 || controlCode2 != 0x15)
            {
                lastError = V5Error::INVALID_RESPONSE;
                lastErrorMessage = String("Expected 0x10 0x15, got 0x") + String(controlCode1, HEX) + " 0x" + String(controlCode2, HEX);
                log_e("Invalid response type: 0x%02X 0x%02X (expected 0x10 0x15)", controlCode1, controlCode2);
                return -1;
            }
            
            // Validate sequence number - if mismatch, this might be response to cloud request
            if (respSequence != lastSentSequence)
            {
                log_w("Sequence mismatch: got %d, expected %d. This may be a response meant for cloud. Trying next frame...", 
                      respSequence, lastSentSequence);
                // Read and discard rest of this frame
                byte discardBuffer[256];
                int remainingBytes = length + 2;  // payload + checksum + end
                while (remainingBytes > 0) {
                    int toRead = min(remainingBytes, 256);
                    int read = client.read(discardBuffer, toRead);
                    if (read <= 0) break;
                    remainingBytes -= read;
                }
                continue;  // Try reading next frame
            }
            
            // Validate serial number
            if (respSN != expectedSN)
            {
                lastError = V5Error::SN_MISMATCH;
                lastErrorMessage = String("SN mismatch: got ") + respSN + ", expected " + expectedSN;
                log_e("Serial number mismatch in response: got %lu, expected %lu", respSN, expectedSN);
                return -1;
            }
            
            if (length > bufferLength)
            {
                lastError = V5Error::BUFFER_TOO_SMALL;
                lastErrorMessage = String("Packet length ") + length + " exceeds buffer " + bufferLength;
                log_e("Buffer too small: packet %d, buffer %d", length, bufferLength);
                return -1;
            }

            log_d("Payload length: %d, sequence OK, SN OK", length);
            
            // Read payload header (14 bytes for response: frametype 1B + status 1B + times 12B)
            int PAYLOAD_HEADER = 14;
            byte payloadHeader[14];
            if (client.read(payloadHeader, PAYLOAD_HEADER) != PAYLOAD_HEADER)
            {
                lastError = V5Error::INCOMPLETE_READ;
                lastErrorMessage = "Failed to read payload header";
                log_e("Failed to read payload header (%d bytes)", PAYLOAD_HEADER);
                return -1;
            }

            // Check frame type (0x02 = inverter) and status (0x01 = OK)
            if (payloadHeader[0] != 0x02 || payloadHeader[1] != 0x01)
            {
                lastError = V5Error::INVALID_SENSOR;
                lastErrorMessage = String("Invalid frame type/status: 0x") + String(payloadHeader[0], HEX) + " 0x" + String(payloadHeader[1], HEX);
                log_e("Invalid frame type/status: 0x%02X 0x%02X (expected 0x02 0x01)", payloadHeader[0], payloadHeader[1]);
                return -1;
            }

            // Read Modbus RTU frame
            int MODBUS_RTU_FRAME_LENGTH = length - PAYLOAD_HEADER;
            if (MODBUS_RTU_FRAME_LENGTH <= 0 || MODBUS_RTU_FRAME_LENGTH > (int)bufferLength)
            {
                lastError = V5Error::INVALID_RESPONSE;
                lastErrorMessage = String("Invalid Modbus frame length: ") + MODBUS_RTU_FRAME_LENGTH;
                log_e("Invalid Modbus frame length: %d", MODBUS_RTU_FRAME_LENGTH);
                return -1;
            }
            
            if (client.read(packetBuffer, MODBUS_RTU_FRAME_LENGTH) != MODBUS_RTU_FRAME_LENGTH)
            {
                lastError = V5Error::INCOMPLETE_READ;
                lastErrorMessage = "Failed to read Modbus RTU frame";
                log_e("Failed to read Modbus RTU frame (%d bytes)", MODBUS_RTU_FRAME_LENGTH);
                return -1;
            }

            // Read trailer (checksum + end byte)
            byte trailerBuffer[2];
            if (client.read(trailerBuffer, 2) != 2)
            {
                log_w("Failed to read trailer (2 bytes)");
            }
            else
            {
                // Verify V5 checksum (optional - just log warning if mismatch)
                // We would need to reconstruct the full frame to verify, which is complex
                // For now, just verify end byte
                if (trailerBuffer[1] != 0x15)
                {
                    log_w("Invalid end byte: 0x%02X (expected 0x15)", trailerBuffer[1]);
                }
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
        
        // If we get here, we exhausted all read retries without getting our response
        lastError = V5Error::SEQUENCE_MISMATCH;
        lastErrorMessage = "Exhausted read retries - only received cloud/heartbeat frames";
        log_e("Failed to receive matching response after %d read attempts", V5TCP_MAX_READ_RETRIES);
        return -1;
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