#pragma once

#include <Arduino.h>
#include <RemoteLogger.hpp>
#include "FlashMutex.hpp"

// Remote logger instance (defined in app.cpp)
extern RemoteLogger remoteLogger;

/**
 * Logging shim with remote logging support.
 * 
 * Logs are written to:
 * 1. Serial (via log_d/w/e)
 * 2. RemoteLogger (if enabled) with FlashGuard synchronization
 * 
 * FlashGuard ensures LittleFS writes don't interfere with RGB LCD DMA.
 */

// Helper function to format and send log to RemoteLogger with FlashGuard
inline void _remoteLog(char level, const char* file, int line, const char* format, ...) {
    // Format the message
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    // Use FlashGuard to synchronize with display
    FlashGuard guard("Log");
    if (guard.isLocked()) {
        remoteLogger.addLog(level, buffer, file, line);
    }
}

#ifndef LOGD
#define LOGD(format, ...) do { \
    log_d(format, ##__VA_ARGS__); \
    _remoteLog('D', __FILE__, __LINE__, format, ##__VA_ARGS__); \
} while(0)
#endif

#ifndef LOGW
#define LOGW(format, ...) do { \
    log_w(format, ##__VA_ARGS__); \
    _remoteLog('W', __FILE__, __LINE__, format, ##__VA_ARGS__); \
} while(0)
#endif

#ifndef LOGE
#define LOGE(format, ...) do { \
    log_e(format, ##__VA_ARGS__); \
    _remoteLog('E', __FILE__, __LINE__, format, ##__VA_ARGS__); \
} while(0)
#endif
