#pragma once

#include <Arduino.h>
#include "RemoteLogger.hpp"

/**
 * Jednoduchý logger pro sledování přístupů k Flash (NVS, SPIFFS)
 * 
 * Původně zde byl mutex se zastavováním LCD DMA, ale po implementaci
 * cache pro časté operace (DongleInfo, SSID, IntelligenceSettings)
 * už není potřeba - flash se čte/zapisuje jen občas.
 * 
 * Tento soubor zachovává API pro zpětnou kompatibilitu,
 * ale pouze loguje operace bez synchronizace.
 */

/**
 * RAII guard pro logování flash operací
 * 
 * Použití:
 *   {
 *       FlashGuard guard("NVS:read");
 *       // ... flash operace ...
 *   }
 */
class FlashGuard {
private:
    const char* tag;
    
public:
    explicit FlashGuard(const char* tag = "Flash", uint32_t timeoutMs = 5000) 
        : tag(tag) {
        LOGD("Flash: %s", tag);
    }
    
    ~FlashGuard() {
        // Nic - jen logování při vytvoření
    }
    
    // Zakázat kopírování
    FlashGuard(const FlashGuard&) = delete;
    FlashGuard& operator=(const FlashGuard&) = delete;
    
    /**
     * Vždy vrací true - pro zpětnou kompatibilitu
     */
    bool isLocked() const {
        return true;
    }
    
    /**
     * Nic nedělá - pro zpětnou kompatibilitu
     */
    void unlock() {
    }
};

/**
 * Statická třída - zachována pro zpětnou kompatibilitu
 */
class FlashMutex {
public:
    static void setWaitDMACallback(void (*callback)()) {
        // Nic - callback už není potřeba
    }
    
    static bool lock(const char* tag = "unknown", uint32_t timeoutMs = 5000) {
        LOGD("Flash: %s", tag);
        return true;
    }
    
    static void unlock(const char* tag = "unknown") {
        // Nic
    }
    
    static bool isLocked() {
        return false;
    }
};
