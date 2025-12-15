#pragma once

#include <Arduino.h>
#include "RemoteLogger.hpp"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/**
 * Singleton třída pro synchronizaci zápisu do Flash (NVS, SPIFFS)
 * 
 * Na ESP32-S3 sdílí flash a PSRAM stejnou SPI sběrnici.
 * Při zápisu do flash se může přerušit přístup k PSRAM,
 * což způsobuje probliknutí displeje pokud je framebuffer v PSRAM.
 * 
 * Tato třída zajišťuje:
 * 1. Před zápisem počká na dokončení DMA přenosu displeje
 * 2. Zamkne mutex aby se nepřekrývaly flash operace
 * 3. Po zápisu uvolní mutex
 * 
 * Použití:
 *   FlashMutex::lock();
 *   // ... flash operace (NVS, SPIFFS) ...
 *   FlashMutex::unlock();
 * 
 * Nebo pomocí RAII guard:
 *   {
 *       FlashGuard guard;
 *       // ... flash operace ...
 *   } // automatický unlock
 */
class FlashMutex {
private:
    static SemaphoreHandle_t mutex;
    static bool initialized;
    
    // Callback pro čekání na DMA - nastavuje se z app.cpp
    static void (*waitDMACallback)();
    
    static void init() {
        if (!initialized) {
            mutex = xSemaphoreCreateRecursiveMutex();  // Rekurzivní mutex - umožňuje vnořené zamykání
            initialized = true;
            LOGD("FlashMutex initialized (recursive)");
        }
    }
    
public:
    /**
     * Nastaví callback pro čekání na DMA displeje
     * Volat při inicializaci z app.cpp
     */
    static void setWaitDMACallback(void (*callback)()) {
        init();
        waitDMACallback = callback;
        LOGD("FlashMutex DMA callback set");
    }
    
    /**
     * Zamkne mutex pro flash operace
     * Před zamknutím počká na dokončení DMA přenosu displeje
     * 
     * @param timeoutMs timeout v ms, 0 = nekonečno
     * @return true pokud se podařilo zamknout
     */
    static bool lock(uint32_t timeoutMs = 5000) {
        init();
        
        // Počkat na dokončení DMA přenosu displeje
        if (waitDMACallback != nullptr) {
            waitDMACallback();
        }
        
        // Zamknout mutex (rekurzivní - umožňuje vnořené volání)
        TickType_t timeout = (timeoutMs == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);
        if (xSemaphoreTakeRecursive(mutex, timeout) == pdTRUE) {
            return true;
        }
        
        LOGW("FlashMutex lock timeout!");
        return false;
    }
    
    /**
     * Odemkne mutex
     */
    static void unlock() {
        if (initialized && mutex != nullptr) {
            xSemaphoreGiveRecursive(mutex);
        }
    }
    
    /**
     * Zkontroluje zda je mutex zamčený (pro debug)
     */
    static bool isLocked() {
        if (!initialized || mutex == nullptr) {
            return false;
        }
        // Zkusíme zamknout s timeout 0 - pokud se nepodaří, je zamčený
        if (xSemaphoreTakeRecursive(mutex, 0) == pdTRUE) {
            xSemaphoreGiveRecursive(mutex);
            return false;
        }
        return true;
    }
};

// Statické členy
SemaphoreHandle_t FlashMutex::mutex = nullptr;
bool FlashMutex::initialized = false;
void (*FlashMutex::waitDMACallback)() = nullptr;

/**
 * RAII guard pro automatické zamykání/odemykání Flash mutexu
 * 
 * Použití:
 *   {
 *       FlashGuard guard;
 *       if (guard.isLocked()) {
 *           // ... flash operace (NVS, SPIFFS) ...
 *       }
 *   } // automatický unlock
 */
class FlashGuard {
private:
    bool locked;
    
public:
    explicit FlashGuard(uint32_t timeoutMs = 5000) {
        locked = FlashMutex::lock(timeoutMs);
    }
    
    ~FlashGuard() {
        if (locked) {
            FlashMutex::unlock();
        }
    }
    
    // Zakázat kopírování
    FlashGuard(const FlashGuard&) = delete;
    FlashGuard& operator=(const FlashGuard&) = delete;
    
    /**
     * Vrátí true pokud se podařilo zamknout mutex
     */
    bool isLocked() const {
        return locked;
    }
    
    /**
     * Explicitní odemknutí (pokud chcete odemknout dříve než na konci scope)
     */
    void unlock() {
        if (locked) {
            FlashMutex::unlock();
            locked = false;
        }
    }
};
