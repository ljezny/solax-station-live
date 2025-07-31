#pragma once

#include <Arduino.h>
#include <NetworkClient.h>
#include "ModbusResponse.hpp"

class ModbusTCP
{
public:
    ModbusTCP() : sequenceNumber(0) {}

    bool isConnected()
    {
        return client.connected();
    }

    bool connect(String hostName, uint16_t port)
    {
        return client.connect(hostName.c_str(), port);
    }

    bool connect(IPAddress ip, uint16_t port)
    {
        return client.connect(ip, port);
    }

    void disconnect()
    {
        client.stop();
    }

    ModbusResponse sendModbusRequest(uint8_t unit, uint8_t functionCode, uint16_t addr, uint8_t count)
    {
        if (functionCode < 1 || functionCode > 4) {
            log_d("Unsupported function code: %d", functionCode);
            ModbusResponse response;
            response.sequenceNumber = sequenceNumber;
            response.address = addr;
            return response;
        }

        sequenceNumber++;

        uint8_t request[] = {
            static_cast<uint8_t>(sequenceNumber >> 8),
            static_cast<uint8_t>(sequenceNumber & 0xFF),
            0, 0, // Protocol ID
            0, 6, // Length
            unit,
            functionCode,
            static_cast<uint8_t>(addr >> 8),
            static_cast<uint8_t>(addr & 0xFF),
            0, // High byte for count
            count
        };

        ModbusResponse response;
        response.sequenceNumber = sequenceNumber;
        response.address = addr;
        if(!isConnected()) {
            log_d("Not connected, cannot send request");
            return response;
        }
        int len = client.write(request, sizeof(request));
        if (len != sizeof(request)) {
            log_d("Failed to send request");
            return response;
        }

        uint8_t header[6];
        if (readFully(header, 6, 5000) != 6) {
            log_d("Response timeout or incomplete header");
            return response;
        }

        uint16_t receivedSequence = (header[0] << 8) | header[1];
        if (receivedSequence != sequenceNumber) {
            log_d("Expected sequence number %d, but got %d", sequenceNumber, receivedSequence);
            flushInput();
            return response;
        }

        uint16_t protocolId = (header[2] << 8) | header[3];
        if (protocolId != 0) {
            log_d("Invalid protocol ID: %d", protocolId);
            flushInput();
            return response;
        }

        uint16_t responseLength = (header[4] << 8) | header[5];
        if (responseLength < 3 || responseLength > RX_BUFFER_SIZE) {
            log_d("Invalid response length: %d", responseLength);
            flushInput();
            return response;
        }

        uint8_t body[RX_BUFFER_SIZE];
        if (readFully(body, responseLength, 5000) != responseLength) {
            log_d("Incomplete body read");
            return response;
        }

        // Ignore unit ID for now, but it's body[0]
        uint8_t receivedFunctionCode = body[1];
        response.functionCode = receivedFunctionCode;

        if (receivedFunctionCode == functionCode) {
            uint8_t byteCount = body[2];
            int expectedByteCount;
            if (functionCode == 1 || functionCode == 2) {
                expectedByteCount = (count + 7) / 8;
            } else { // 3 or 4
                expectedByteCount = count * 2;
            }

            if (byteCount != expectedByteCount || responseLength != 3 + byteCount) {
                log_d("Invalid byte count: %d, expected: %d", byteCount, expectedByteCount);
                return response;
            }

            memcpy(response.data, body + 3, byteCount);
            response.length = byteCount;
            response.isValid = true;

            // Log response data as hex
            // log_d("Request address: %d", addr);
            // String dataHex = "";
            // for (int i = 0; i < response.length; i++) {
            //     dataHex += String(response.data[i], HEX);
            // }
            // log_d("Response data: %s", dataHex.c_str());
        } else if (receivedFunctionCode == functionCode + 0x80) {
            if (responseLength != 3) {
                log_d("Invalid exception response length");
                return response;
            }
            uint8_t exceptionCode = body[2];
            log_d("Modbus exception: code %d", exceptionCode);
        } else {
            log_d("Invalid function code: expected %d, got %d", functionCode, receivedFunctionCode);
        }

        return response;
    }

private:
    NetworkClient client;
    uint16_t sequenceNumber;

    int readFully(uint8_t* buf, int len, unsigned long timeoutMs) {
        int bytesRead = 0;
        unsigned long start = millis();
        while (bytesRead < len && (millis() - start) < timeoutMs) {
            int availableBytes = client.available();
            if (availableBytes > 0) {
                int got = client.read(buf + bytesRead, len - bytesRead);
                if (got > 0) {
                    bytesRead += got;
                }
            } else {
                delay(1);
            }
        }
        return bytesRead;
    }

    void flushInput() {
        while (client.available()) {
            client.read();
        }
    }
};