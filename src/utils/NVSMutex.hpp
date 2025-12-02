#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/**
 * Singleton třída pro synchronizaci zápisu do NVS (flash)
 * 
 * Na ESP32-S3 sdílí flash a PSRAM stejnou SPI sběrnici.
 * Při zápisu do flash (NVS) se může přerušit přístup k PSRAM,
 * což způsobuje probliknutí displeje pokud je framebuffer v PSRAM.
 * 
 * Tato třída zajišťuje:
 * 1. Před zápisem počká na dokončení DMA přenosu displeje
 * 2. Zamkne mutex aby se nepřekrývaly NVS operace
 * 3. Po zápisu uvolní mutex
 * 
 * Použití:
 *   NVSMutex::lock();
 *   // ... NVS operace ...
 *   NVSMutex::unlock();
 * 
 * Nebo pomocí RAII guard:
 *   {
 *       NVSGuard guard;
 *       // ... NVS operace ...
 *   } // automatický unlock
 */
class NVSMutex {
private:
    static SemaphoreHandle_t mutex;
    static bool initialized;
    
    // Callback pro čekání na DMA - nastavuje se z app.cpp
    static void (*waitDMACallback)();
    
    static void init() {
        if (!initialized) {
            mutex = xSemaphoreCreateMutex();
            initialized = true;
            log_d("NVSMutex initialized");
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
        log_d("NVSMutex DMA callback set");
    }
    
    /**
     * Zamkne mutex pro NVS operace
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
        
        // Zamknout mutex
        TickType_t timeout = (timeoutMs == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);
        if (xSemaphoreTake(mutex, timeout) == pdTRUE) {
            return true;
        }
        
        log_w("NVSMutex lock timeout!");
        return false;
    }
    
    /**
     * Odemkne mutex
     */
    static void unlock() {
        if (initialized && mutex != nullptr) {
            xSemaphoreGive(mutex);
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
        if (xSemaphoreTake(mutex, 0) == pdTRUE) {
            xSemaphoreGive(mutex);
            return false;
        }
        return true;
    }
};

// Statické členy
SemaphoreHandle_t NVSMutex::mutex = nullptr;
bool NVSMutex::initialized = false;
void (*NVSMutex::waitDMACallback)() = nullptr;

/**
 * RAII guard pro automatické zamykání/odemykání NVS mutexu
 * 
 * Použití:
 *   {
 *       NVSGuard guard;
 *       if (guard.isLocked()) {
 *           // ... NVS operace ...
 *       }
 *   } // automatický unlock
 */
class NVSGuard {
private:
    bool locked;
    
public:
    explicit NVSGuard(uint32_t timeoutMs = 5000) {
        locked = NVSMutex::lock(timeoutMs);
    }
    
    ~NVSGuard() {
        if (locked) {
            NVSMutex::unlock();
        }
    }
    
    // Zakázat kopírování
    NVSGuard(const NVSGuard&) = delete;
    NVSGuard& operator=(const NVSGuard&) = delete;
    
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
            NVSMutex::unlock();
            locked = false;
        }
    }
};
