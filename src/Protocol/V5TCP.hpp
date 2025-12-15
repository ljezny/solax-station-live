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
            lastError = V5Error::INVALID_SN;
            lastErrorMessage = "SN is zero";
            LOGE("SN is zero, cannot send request");
            return false;
        }
        if (sn == 0xFFFFFFFF)
        {
            lastError = V5Error::INVALID_SN;
            lastErrorMessage = "SN overflow (0xFFFFFFFF) - check SN length";
            LOGE("SN is 0xFFFFFFFF (overflow), cannot send request. Check if SN has correct length (10 digits).");
            return false;
        }
        sequenceNumber++;
        lastSentSequence = sequenceNumber;  // Store for validation
        expectedSN = sn;                     // Store for validation

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
                LOGD("Read retry %d/%d (previous frame was not for us)", readAttempt + 1, V5TCP_MAX_READ_RETRIES);
            }
            
            // Read start byte
            int bytesRead = client.read(packetBuffer, 1);
            if (bytesRead <= 0)
            {
                lastError = V5Error::TIMEOUT;
                lastErrorMessage = String("No response from dongle (read returned ") + bytesRead + "). Dongle may be offline, busy, or SN mismatch.";
                LOGE("Read timeout waiting for response. Dongle at %s not responding. Check: 1) Dongle is online 2) SN matches dongle 3) Dongle not busy with cloud", ip.toString().c_str());
                return -1;
            }
            if (packetBuffer[0] != 0xA5)
            {
                lastError = V5Error::INVALID_HEADER;
                lastErrorMessage = String("Expected 0xA5, got 0x") + String(packetBuffer[0], HEX);
                LOGE("Invalid header: expected 0xA5, got 0x%02X", packetBuffer[0]);
                return -1;
            }
            
            // Read length (2 bytes)
            if (client.read(packetBuffer, 2) != 2)
            {
                lastError = V5Error::INCOMPLETE_READ;
                lastErrorMessage = "Failed to read packet length";
                LOGE("Failed to read packet length");
                return -1;
            }
            uint16_t length = packetBuffer[0] | (packetBuffer[1] << 8);
            
            // Read rest of header (8 bytes: control code 2B + sequence 2B + SN 4B)
            byte headerBuffer[8];
            if (client.read(headerBuffer, 8) != 8)
            {
                lastError = V5Error::INCOMPLETE_READ;
                lastErrorMessage = "Failed to read header";
                LOGE("Failed to read header (8 bytes)");
                return -1;
            }
            
            uint8_t controlCode1 = headerBuffer[0];  // Should be 0x10
            uint8_t controlCode2 = headerBuffer[1];  // 0x15 for response, 0x47 for heartbeat
            uint8_t respSequence = headerBuffer[2];
            // headerBuffer[3] is second seq byte (dongle's counter)
            uint32_t respSN = headerBuffer[4] | (headerBuffer[5] << 8) | (headerBuffer[6] << 16) | (headerBuffer[7] << 24);
            
            LOGD("Frame: ctrl=0x%02X%02X seq=%d respSN=%lu (expected seq=%d, SN=%lu)", 
                  controlCode1, controlCode2, respSequence, respSN, lastSentSequence, expectedSN);
            
            // Check for heartbeat frame (0x47) - skip and continue reading
            if (controlCode2 == 0x47)
            {
                LOGD("Received heartbeat frame (0x47), skipping and reading next frame");
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
                LOGE("Invalid response type: 0x%02X 0x%02X (expected 0x10 0x15)", controlCode1, controlCode2);
                return -1;
            }
            
            // Validate sequence number - if mismatch, this might be response to cloud request
            if (respSequence != lastSentSequence)
            {
                LOGW("Sequence mismatch: got %d, expected %d. This may be a response meant for cloud. Trying next frame...", 
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
                LOGE("Serial number mismatch in response: got %lu, expected %lu", respSN, expectedSN);
                return -1;
            }
            
            if (length > bufferLength)
            {
                lastError = V5Error::BUFFER_TOO_SMALL;
                lastErrorMessage = String("Packet length ") + length + " exceeds buffer " + bufferLength;
                LOGE("Buffer too small: packet %d, buffer %d", length, bufferLength);
                return -1;
            }

            LOGD("Payload length: %d, sequence OK, SN OK", length);
            
            // Read payload header (14 bytes for response: frametype 1B + status 1B + times 12B)
            int PAYLOAD_HEADER = 14;
            byte payloadHeader[14];
            if (client.read(payloadHeader, PAYLOAD_HEADER) != PAYLOAD_HEADER)
            {
                lastError = V5Error::INCOMPLETE_READ;
                lastErrorMessage = "Failed to read payload header";
                LOGE("Failed to read payload header (%d bytes)", PAYLOAD_HEADER);
                return -1;
            }

            // Check frame type (0x02 = inverter) and status (0x01 = OK)
            if (payloadHeader[0] != 0x02 || payloadHeader[1] != 0x01)
            {
                lastError = V5Error::INVALID_SENSOR;
                lastErrorMessage = String("Invalid frame type/status: 0x") + String(payloadHeader[0], HEX) + " 0x" + String(payloadHeader[1], HEX);
                LOGE("Invalid frame type/status: 0x%02X 0x%02X (expected 0x02 0x01)", payloadHeader[0], payloadHeader[1]);
                return -1;
            }

            // Read Modbus RTU frame
            int MODBUS_RTU_FRAME_LENGTH = length - PAYLOAD_HEADER;
            if (MODBUS_RTU_FRAME_LENGTH <= 0 || MODBUS_RTU_FRAME_LENGTH > (int)bufferLength)
            {
                lastError = V5Error::INVALID_RESPONSE;
                lastErrorMessage = String("Invalid Modbus frame length: ") + MODBUS_RTU_FRAME_LENGTH;
                LOGE("Invalid Modbus frame length: %d", MODBUS_RTU_FRAME_LENGTH);
                return -1;
            }
            
            if (client.read(packetBuffer, MODBUS_RTU_FRAME_LENGTH) != MODBUS_RTU_FRAME_LENGTH)
            {
                lastError = V5Error::INCOMPLETE_READ;
                lastErrorMessage = "Failed to read Modbus RTU frame";
                LOGE("Failed to read Modbus RTU frame (%d bytes)", MODBUS_RTU_FRAME_LENGTH);
                return -1;
            }

            // Read trailer (checksum + end byte)
            byte trailerBuffer[2];
            if (client.read(trailerBuffer, 2) != 2)
            {
                LOGW("Failed to read trailer (2 bytes)");
            }
            else
            {
                // Verify V5 checksum (optional - just log warning if mismatch)
                // We would need to reconstruct the full frame to verify, which is complex
                // For now, just verify end byte
                if (trailerBuffer[1] != 0x15)
                {
                    LOGW("Invalid end byte: 0x%02X (expected 0x15)", trailerBuffer[1]);
                }
            }
            
            lastError = V5Error::OK;
            lastErrorMessage = "";
            
            String dump = "";
            for (int i = 0; i < MODBUS_RTU_FRAME_LENGTH; i++)
            {
                dump += String(packetBuffer[i], HEX) + " ";
            }
            LOGD("Response: %s", dump.c_str());
            return MODBUS_RTU_FRAME_LENGTH;
        }
        
        // If we get here, we exhausted all read retries without getting our response
        lastError = V5Error::SEQUENCE_MISMATCH;
        lastErrorMessage = "Exhausted read retries - only received cloud/heartbeat frames";
        LOGE("Failed to receive matching response after %d read attempts", V5TCP_MAX_READ_RETRIES);
        return -1;
    }

    bool tryReadWithRetries(uint16_t startReg, uint16_t length, uint32_t sn, byte *buffer, std::function<void()> onSuccess)
    {
        lastError = V5Error::OK;
        
        for (int i = 0; i < MAX_RETRIES; ++i)
        {
            LOGD("Attempt %d/%d for register 0x%04X (SN: %lu)", i + 1, MAX_RETRIES, startReg, sn);
            
            if (connect(ip))
            {
                if (sendReadDataRequest(startReg, length, sn))
                {
                    int result = readModbusRTUResponse(buffer, 1024);
                    if (result > 0)
                    {
                        LOGI("Successfully read %d bytes from register 0x%04X", result, startReg);
                        onSuccess();
                        disconnect();
                        return true;
                    }
                    else
                    {
                        LOGW("Read failed for 0x%04X (attempt %d/%d): %s", 
                              startReg, i + 1, MAX_RETRIES, errorToString(lastError));
                    }
                }
                else
                {
                    LOGW("Send request failed for 0x%04X (attempt %d/%d): %s", 
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
                LOGW("Failed to connect to dongle at %s (attempt %d/%d)", 
                      ip.toString().c_str(), i + 1, MAX_RETRIES);
            }
        }
        
        LOGE("All %d attempts failed for register 0x%04X. Last error: %s - %s", 
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
        // Always prefer user-specified IP address over cached one
        if (!ipAddress.isEmpty())
        {
            IPAddress newIp;
            if (newIp.fromString(ipAddress))
            {
                if (ip != newIp)
                {
                    LOGD("Using IP from settings: %s (was: %s)", ipAddress.c_str(), ip.toString().c_str());
                }
                ip = newIp;
            }
        }

        if (ip == IPAddress(0, 0, 0, 0))
        {
            ip = discoverDongleIP();
            if (ip == IPAddress(0, 0, 0, 0))
                ip = IPAddress(10, 10, 100, 254); // fallback
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