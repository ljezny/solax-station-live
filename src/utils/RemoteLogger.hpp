#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <vector>
#include <WiFi.h>

#define REMOTE_LOGGER_SERVER "http://141.144.227.173"
#define REMOTE_LOGGER_MAX_BUFFER_SIZE 20  // Maximum buffer size (reduced to save RAM)
#define REMOTE_LOGGER_AUTO_FLUSH_SIZE 10  // Auto-flush after this many logs

// Logging levels (from least to most verbose)
// LEVEL_NONE = no remote logging
// LEVEL_ERROR = E only
// LEVEL_WARNING = W, E
// LEVEL_INFO = I, W, E
// LEVEL_DEBUG = D, I, W, E (all)
enum RemoteLogLevel {
    LEVEL_NONE = 0,
    LEVEL_ERROR = 1,
    LEVEL_WARNING = 2,
    LEVEL_INFO = 3,
    LEVEL_DEBUG = 4
};

struct RemoteLogEntry {
    char level;      // D, I, W, E
    String message;
    String file;
    int line;
    unsigned long timestamp;
};

class RemoteLogger {
public:
    RemoteLogger() : _level(LEVEL_NONE), _initialized(false), _levelChecked(false) {}
    
    /**
     * @brief Initialize the remote logger with device serial number
     * @param serialNumber Device serial number (ESP.getEfuseMac())
     * @param fwVersion Firmware version string
     * @param email User email (optional)
     * @param lat Latitude (0.0 if not available)
     * @param lng Longitude (0.0 if not available)
     */
    void begin(uint64_t serialNumber, const String& fwVersion, const String& email = "", float lat = 0.0, float lng = 0.0) {
        char snBuffer[20];
        snprintf(snBuffer, sizeof(snBuffer), "%llX", serialNumber);
        _serialNumber = String(snBuffer);
        _fwVersion = fwVersion;
        _email = email;
        _lat = lat;
        _lng = lng;
        _sessionId = generateSessionId();
        _initialized = true;
        Serial.println("[RemoteLogger] Initialized. SN: " + _serialNumber);
    }
    
    /**
     * @brief Check logging level for this device and register it on server
     *        Sends a "heartbeat" POST to /api/logs without a log message
     * @return Current logging level
     */
    RemoteLogLevel checkLevel() {
        Serial.println("[RemoteLogger] Registering device " + _serialNumber);
        
        HTTPClient http;
        String url = String(REMOTE_LOGGER_SERVER) + "/api/logs";
        
        http.begin(url);
        http.setTimeout(10000);
        http.addHeader("Content-Type", "application/json");
        
        // Send device info (no log message = just registration/check)
        DynamicJsonDocument doc(256);
        doc["sn"] = _serialNumber;
        doc["name"] = "solar-station-live";
        doc["fw_version"] = _fwVersion;
        doc["session_id"] = _sessionId;
        if (_email.length() > 0) {
            doc["email"] = _email;
        }
        if (_lat != 0.0 || _lng != 0.0) {
            doc["lat"] = _lat;
            doc["lng"] = _lng;
        }
        if (_batteryPercent >= 0) {
            doc["battery"] = _batteryPercent;
        }
        // No "level" and "message" = just heartbeat/registration
        
        String payload;
        serializeJson(doc, payload);
        Serial.println("[RemoteLogger] Sending: " + payload);
        
        int httpCode = http.POST(payload);
        
        if (httpCode == HTTP_CODE_OK) {
            String response = http.getString();
            Serial.println("[RemoteLogger] Response: " + response);
            
            DynamicJsonDocument responseDoc(256);
            DeserializationError error = deserializeJson(responseDoc, response);
            
            if (!error) {
                String levelStr = responseDoc["level"].as<String>();
                _level = parseLevel(levelStr);
                Serial.println("[RemoteLogger] Logging level = " + levelStr + " (" + String(_level) + ")");
            } else {
                Serial.println("[RemoteLogger] JSON parse error: " + String(error.c_str()));
                _level = LEVEL_NONE;
            }
        } else {
            Serial.println("[RemoteLogger] HTTP error: " + String(httpCode));
            _level = LEVEL_NONE;
        }
        
        http.end();
        
        // Mark that level check was performed
        _levelChecked = true;
        
        // Process pre-check buffer based on the obtained level
        if (_level == LEVEL_NONE) {
            // Logging disabled - discard all buffered logs
            Serial.println("[RemoteLogger] Discarding " + String(_buffer.size()) + " pre-check logs (logging disabled)");
            _buffer.clear();
        } else {
            // Filter buffer to keep only logs matching current level
            filterBufferByLevel();
            Serial.println("[RemoteLogger] Kept " + String(_buffer.size()) + " pre-check logs after filtering");
        }
        
        return _level;
    }
    
    /**
     * @brief Get current logging level (cached value)
     */
    RemoteLogLevel getLevel() const {
        return _level;
    }
    
    /**
     * @brief Check if any logging is enabled
     */
    bool isEnabled() const {
        return _level > LEVEL_NONE;
    }
    
    /**
     * @brief Check if logger is initialized
     */
    bool isInitialized() const {
        return _initialized;
    }
    
    /**
     * @brief Log a debug message (requires LEVEL_DEBUG)
     * @note Use RLOG_D() macro to automatically include file/line
     */
    void logDebug(const String& message, const char* file = nullptr, int line = 0) {
        addLog('D', message, file, line);
    }
    
    /**
     * @brief Log an info message (requires LEVEL_INFO or higher)
     * @note Use RLOG_I() macro to automatically include file/line
     */
    void logInfo(const String& message, const char* file = nullptr, int line = 0) {
        addLog('I', message, file, line);
    }
    
    /**
     * @brief Log a warning message (requires LEVEL_WARNING or higher)
     * @note Use RLOG_W() macro to automatically include file/line
     */
    void logWarning(const String& message, const char* file = nullptr, int line = 0) {
        addLog('W', message, file, line);
    }
    
    /**
     * @brief Log an error message (requires LEVEL_ERROR or higher)
     * @note Use RLOG_E() macro to automatically include file/line
     */
    void logError(const String& message, const char* file = nullptr, int line = 0) {
        addLog('E', message, file, line);
    }
    
    /**
     * @brief Send all buffered logs to the server
     * @return Number of logs successfully sent
     */
    int flush() {
        if (_buffer.empty()) {
            Serial.println("[RemoteLogger] No logs to send");
            return 0;
        }
        
        if (_level == LEVEL_NONE) {
            Serial.println("[RemoteLogger] Logging disabled, clearing buffer");
            _buffer.clear();
            return 0;
        }
        
        Serial.println("[RemoteLogger] Flushing " + String(_buffer.size()) + " log entries");
        
        int sentCount = 0;
        HTTPClient http;
        String url = String(REMOTE_LOGGER_SERVER) + "/api/logs";
        
        for (const auto& entry : _buffer) {
            http.begin(url);
            http.setTimeout(5000);
            http.addHeader("Content-Type", "application/json");
            
            DynamicJsonDocument doc(512);
            doc["sn"] = _serialNumber;
            doc["name"] = "solar-station-live";
            doc["fw_version"] = _fwVersion;
            doc["session_id"] = _sessionId;
            doc["level"] = String(entry.level);
            doc["message"] = entry.message;
            if (entry.file.length() > 0) {
                doc["file"] = entry.file;
                doc["line"] = entry.line;
            }
            if (_email.length() > 0) {
                doc["email"] = _email;
            }
            if (_lat != 0.0 || _lng != 0.0) {
                doc["lat"] = _lat;
                doc["lng"] = _lng;
            }
            
            String payload;
            serializeJson(doc, payload);
            
            int httpCode = http.POST(payload);
            
            if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED) {
                sentCount++;
                // Parse response to update logging level
                String response = http.getString();
                DynamicJsonDocument responseDoc(128);
                DeserializationError error = deserializeJson(responseDoc, response);
                if (!error && responseDoc.containsKey("level")) {
                    String levelStr = responseDoc["level"].as<String>();
                    _level = parseLevel(levelStr);
                }
            } else {
                Serial.println("[RemoteLogger] Failed to send log, HTTP code: " + String(httpCode));
            }
            
            http.end();
            
            // Small delay to prevent overwhelming the server
            delay(10);
        }
        
        Serial.println("[RemoteLogger] Sent " + String(sentCount) + "/" + String(_buffer.size()) + " logs");
        _buffer.clear();
        
        return sentCount;
    }
    
    /**
     * @brief Get number of buffered log entries
     */
    size_t getBufferSize() const {
        return _buffer.size();
    }
    
    /**
     * @brief Clear the log buffer without sending
     */
    void clearBuffer() {
        _buffer.clear();
    }
    
    /**
     * @brief Generate a new session ID
     */
    void newSession() {
        _sessionId = generateSessionId();
        _levelChecked = false;  // Reset for new session - collect logs until checkLevel
        Serial.println("[RemoteLogger] New session: " + _sessionId);
    }

    /**
     * @brief Set battery percentage (call before checkLevel)
     */
    void setBatteryPercent(int percent) {
        _batteryPercent = percent;
    }

private:
    String _serialNumber;
    String _fwVersion;
    String _email;
    float _lat = 0.0;
    float _lng = 0.0;
    int _batteryPercent = -1;
    String _sessionId;
    RemoteLogLevel _level;
    bool _initialized;
    bool _levelChecked;  // true after checkLevel() was called
    std::vector<RemoteLogEntry> _buffer;
    
    RemoteLogLevel parseLevel(const String& levelStr) {
        if (levelStr == "debug") return LEVEL_DEBUG;
        if (levelStr == "info") return LEVEL_INFO;
        if (levelStr == "warning") return LEVEL_WARNING;
        if (levelStr == "error") return LEVEL_ERROR;
        return LEVEL_NONE;
    }
    
    void addLog(char level, const String& message, const char* file = nullptr, int line = 0) {
        if (!_initialized) {
            return;
        }
        
        // After level check: skip if logging is disabled or level doesn't match
        if (_levelChecked) {
            if (_level == LEVEL_NONE) {
                return;
            }
            // Check if this log level should be recorded
            if (!shouldLogLevel(level)) {
                return;
            }
        }
        // Before level check: collect ALL logs (will be filtered later)
        
        // Prevent buffer overflow
        if (_buffer.size() >= REMOTE_LOGGER_MAX_BUFFER_SIZE) {
            _buffer.erase(_buffer.begin());
        }
        
        RemoteLogEntry entry;
        entry.level = level;
        entry.message = message;
        entry.file = file ? extractFilename(file) : "";
        entry.line = line;
        entry.timestamp = millis();
        
        _buffer.push_back(entry);
        
        // Auto-flush when buffer reaches threshold (if WiFi connected)
        if (_buffer.size() >= REMOTE_LOGGER_AUTO_FLUSH_SIZE && WiFi.status() == WL_CONNECTED) {
            Serial.println("[RemoteLogger] Auto-flushing " + String(_buffer.size()) + " logs");
            flush();
        }
    }
    
    bool shouldLogLevel(char level) const {
        switch (level) {
            case 'E': return _level >= LEVEL_ERROR;
            case 'W': return _level >= LEVEL_WARNING;
            case 'I': return _level >= LEVEL_INFO;
            case 'D': return _level >= LEVEL_DEBUG;
            default: return false;
        }
    }
    
    void filterBufferByLevel() {
        // Remove logs that don't match current level
        _buffer.erase(
            std::remove_if(_buffer.begin(), _buffer.end(),
                [this](const RemoteLogEntry& entry) {
                    return !shouldLogLevel(entry.level);
                }),
            _buffer.end());
    }
    
    String generateSessionId() {
        // Generate a simple session ID based on millis and random
        char buffer[17];
        snprintf(buffer, sizeof(buffer), "%08lX%08lX", millis(), esp_random());
        return String(buffer);
    }
    
    String extractFilename(const char* path) {
        // Extract just filename from full path (e.g., /path/to/file.cpp -> file.cpp)
        if (!path) return "";
        const char* lastSlash = strrchr(path, '/');
        if (!lastSlash) lastSlash = strrchr(path, '\\');
        return String(lastSlash ? lastSlash + 1 : path);
    }
};

// Global instance declaration
extern RemoteLogger remoteLogger;

// ============================================================================
// LOGGING MACROS
// ============================================================================
// These macros replace the standard ESP32 log_d/log_i/log_w/log_e
// They log both to Serial (via standard log_X) AND to RemoteLogger
//
// Usage: LOGD("message"), LOGI("message"), LOGW("message"), LOGE("message")
// With formatting: LOGD("value = %d", value)
// ============================================================================

// Helper macro to format message using snprintf
#define _RLOG_FORMAT(fmt, ...) \
    ([&]() -> String { \
        char _rlog_buf[256]; \
        snprintf(_rlog_buf, sizeof(_rlog_buf), fmt, ##__VA_ARGS__); \
        return String(_rlog_buf); \
    }())

// Debug log - logs to both Serial and RemoteLogger
#define LOGD(fmt, ...) do { \
    log_d(fmt, ##__VA_ARGS__); \
    remoteLogger.logDebug(_RLOG_FORMAT(fmt, ##__VA_ARGS__), __FILE__, __LINE__); \
} while(0)

// Info log - logs to both Serial and RemoteLogger
#define LOGI(fmt, ...) do { \
    log_i(fmt, ##__VA_ARGS__); \
    remoteLogger.logInfo(_RLOG_FORMAT(fmt, ##__VA_ARGS__), __FILE__, __LINE__); \
} while(0)

// Warning log - logs to both Serial and RemoteLogger
#define LOGW(fmt, ...) do { \
    log_w(fmt, ##__VA_ARGS__); \
    remoteLogger.logWarning(_RLOG_FORMAT(fmt, ##__VA_ARGS__), __FILE__, __LINE__); \
} while(0)

// Error log - logs to both Serial and RemoteLogger
#define LOGE(fmt, ...) do { \
    log_e(fmt, ##__VA_ARGS__); \
    remoteLogger.logError(_RLOG_FORMAT(fmt, ##__VA_ARGS__), __FILE__, __LINE__); \
} while(0)

// Remote-only logging (without Serial output)
#define RLOG_D(fmt, ...) remoteLogger.logDebug(_RLOG_FORMAT(fmt, ##__VA_ARGS__), __FILE__, __LINE__)
#define RLOG_I(fmt, ...) remoteLogger.logInfo(_RLOG_FORMAT(fmt, ##__VA_ARGS__), __FILE__, __LINE__)
#define RLOG_W(fmt, ...) remoteLogger.logWarning(_RLOG_FORMAT(fmt, ##__VA_ARGS__), __FILE__, __LINE__)
#define RLOG_E(fmt, ...) remoteLogger.logError(_RLOG_FORMAT(fmt, ##__VA_ARGS__), __FILE__, __LINE__)
