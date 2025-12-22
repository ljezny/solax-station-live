#pragma once

#include <Arduino.h>
#include <RemoteLogger.hpp>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

/**
 * Synchronizace Flash operací s VSYNC a LVGL DMA
 * 
 * Problém: RGB LCD používá DMA pro kontinuální čtení framebufferu z PSRAM.
 * Flash operace mohou blokovat PSRAM přístup, což způsobuje blikání displeje.
 * 
 * Řešení: Patchnutý LovyanGFX Bus_RGB volá náš callback při každém VSYNC.
 * Flash operace počká na VSYNC signál (vertical blanking period) před přístupem.
 */

/**
 * Singleton pro VSYNC synchronizaci
 */
class VSyncManager {
private:
    static VSyncManager* instance;
    
    SemaphoreHandle_t vsyncSemaphore = nullptr;
    SemaphoreHandle_t flashMutex = nullptr;
    bool initialized = false;
    
    // Callback pro čekání na DMA (nastaví se z app.cpp)
    void (*waitDMACallback)() = nullptr;
    
    VSyncManager() {}
    
public:
    static VSyncManager& getInstance() {
        if (!instance) {
            instance = new VSyncManager();
        }
        return *instance;
    }
    
    /**
     * VSYNC callback - voláno z Bus_RGB ISR při každém VSYNC
     * POZOR: Toto běží v ISR kontextu!
     */
    static void IRAM_ATTR vsyncCallback(void* arg) {
        VSyncManager* mgr = (VSyncManager*)arg;
        if (mgr && mgr->vsyncSemaphore) {
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            xSemaphoreGiveFromISR(mgr->vsyncSemaphore, &xHigherPriorityTaskWoken);
            if (xHigherPriorityTaskWoken) {
                portYIELD_FROM_ISR();
            }
        }
    }
    
    /**
     * Inicializace - vytvoří semafory a mutex
     * VSYNC callback se nastaví zvlášť přes setVSyncCallback na Bus_RGB
     */
    bool begin() {
        if (initialized) return true;
        
        // Vytvořit binární semafor pro VSYNC signalizaci
        vsyncSemaphore = xSemaphoreCreateBinary();
        if (!vsyncSemaphore) {
            LOGE("VSyncManager: Failed to create vsync semaphore");
            return false;
        }
        
        // Vytvořit mutex pro exkluzivní přístup k flash
        flashMutex = xSemaphoreCreateMutex();
        if (!flashMutex) {
            LOGE("VSyncManager: Failed to create flash mutex");
            vSemaphoreDelete(vsyncSemaphore);
            return false;
        }
        
        initialized = true;
        LOGI("VSyncManager: Initialized, waiting for VSYNC callback registration");
        return true;
    }
    
    /**
     * Zaregistrovat VSYNC callback na Bus_RGB
     * Volat PO tft.begin() a PO VSyncManager::begin()
     */
    template<typename T>
    void registerVSyncCallback(T& bus) {
        bus.setVSyncCallback(vsyncCallback, this);
        LOGI("VSyncManager: VSYNC callback registered on Bus_RGB");
    }
    
    /**
     * Nastavit callback pro čekání na dokončení DMA
     */
    void setWaitDMACallback(void (*callback)()) {
        waitDMACallback = callback;
    }
    
    /**
     * Počkat na VSYNC (max timeout ms)
     * Vrací true pokud se dočkali VSYNC, false při timeout
     */
    bool waitForVSync(uint32_t timeoutMs = 20) {
        if (!initialized) return true;
        
        // Vyčistit semafor (pokud už byl signalizován)
        xSemaphoreTake(vsyncSemaphore, 0);
        
        // Počkat na další VSYNC
        return xSemaphoreTake(vsyncSemaphore, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
    }
    
    // Diagnostické informace z poslední operace (pro logování PO uvolnění mutexu)
    struct LastOpInfo {
        uint32_t waitTimeUs;      // Čas čekání na VSYNC v mikrosekundách
        bool vsyncOk;             // Zda se VSYNC podařil
        bool mutexOk;             // Zda se mutex podařil
        const char* tag;          // Tag operace
    };
    LastOpInfo lastOp = {0, false, false, nullptr};
    
    /**
     * Získat exkluzivní přístup pro flash operaci
     * 1. Získá mutex (ostatní flash operace musí čekat)
     * 2. Počká na dokončení LVGL DMA
     * 3. Počká na VSYNC (vertical blanking - bezpečná perioda)
     * 
     * DŮLEŽITÉ: Žádné logování během držení mutexu - sériový výstup je pomalý!
     */
    bool lockForFlash(const char* tag, uint32_t timeoutMs = 5000) {
        lastOp = {0, false, false, tag};
        
        if (!initialized) {
            return true;  // Log pouze při verbose debug
        }
        
        // 1. Získat mutex
        if (xSemaphoreTake(flashMutex, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) {
            return false;  // Timeout - volající může zalogovat
        }
        lastOp.mutexOk = true;
        
        // 2. Počkat na dokončení DMA z LVGL
        if (waitDMACallback) {
            waitDMACallback();
        }
        
        // 3. Počkat na VSYNC (bezpečná perioda pro flash) - měříme čas
        uint32_t startUs = micros();
        lastOp.vsyncOk = waitForVSync(20);
        lastOp.waitTimeUs = micros() - startUs;
        
        return true;
    }
    
    /**
     * Uvolnit flash mutex
     */
    void unlockFlash(const char* tag = nullptr) {
        if (!initialized) return;
        xSemaphoreGive(flashMutex);
    }
    
    /**
     * Vrátí info o poslední operaci (pro logování PO uvolnění mutexu)
     */
    const LastOpInfo& getLastOpInfo() const { return lastOp; }
    
    /**
     * Synchronizovat LVGL flush s VSYNC (volitelné - pro tearing prevention)
     */
    void syncLVGLFlush() {
        if (!initialized) return;
        // Krátké čekání - pokud VSYNC přijde rychle, synchronizujeme
        waitForVSync(5);
    }
    
    bool isInitialized() const { return initialized; }
};

/**
 * RAII guard pro flash operace se synchronizací
 * Logování probíhá pouze mimo kritickou sekci!
 */
class FlashGuard {
private:
    const char* tag;
    bool locked;
    uint32_t waitTimeUs;
    uint32_t startTimeUs;  // Čas začátku flash operace
    bool vsyncOk;
    
public:
    explicit FlashGuard(const char* tag = "Flash", uint32_t timeoutMs = 5000) 
        : tag(tag), locked(false), waitTimeUs(0), startTimeUs(0), vsyncOk(false) {
        locked = VSyncManager::getInstance().lockForFlash(tag, timeoutMs);
        // Uložit info pro pozdější logování
        if (locked) {
            const auto& info = VSyncManager::getInstance().getLastOpInfo();
            waitTimeUs = info.waitTimeUs;
            vsyncOk = info.vsyncOk;
            startTimeUs = micros();  // Začátek flash operace
        }
        // Nelogovat zde - jsme v kritické sekci!
    }
    
    ~FlashGuard() {
        if (locked) {
            uint32_t durationUs = micros() - startTimeUs;  // Doba trvání flash operace
            VSyncManager::getInstance().unlockFlash(tag);
            // Log AŽ PO uvolnění mutexu - bezpečné!
            // VBLANK trvá cca 1-2ms, pokud operace trvá déle, může bliknout
            if (durationUs > 2000) {
                LOGW("Flash[%s]: %luus (>2ms!), wait %luus, vsync %s", 
                     tag, durationUs, waitTimeUs, vsyncOk ? "OK" : "TIMEOUT");
            } else {
                LOGD("Flash[%s]: %luus, wait %luus, vsync %s", 
                     tag, durationUs, waitTimeUs, vsyncOk ? "OK" : "TIMEOUT");
            }
        }
    }
    
    // Zakázat kopírování
    FlashGuard(const FlashGuard&) = delete;
    FlashGuard& operator=(const FlashGuard&) = delete;
    
    bool isLocked() const { return locked; }
    bool wasVSyncOk() const { return vsyncOk; }
    uint32_t getWaitTimeUs() const { return waitTimeUs; }
    
    void unlock() {
        if (locked) {
            uint32_t durationUs = micros() - startTimeUs;
            VSyncManager::getInstance().unlockFlash(tag);
            // Log po uvolnění
            if (durationUs > 2000) {
                LOGW("Flash[%s]: %luus (>2ms!), wait %luus, vsync %s", 
                     tag, durationUs, waitTimeUs, vsyncOk ? "OK" : "TIMEOUT");
            } else {
                LOGD("Flash[%s]: %luus, wait %luus, vsync %s", 
                     tag, durationUs, waitTimeUs, vsyncOk ? "OK" : "TIMEOUT");
            }
            locked = false;
        }
    }
};

// Statická instance VSyncManager
inline VSyncManager* VSyncManager::instance = nullptr;

/**
 * Statická třída - zachována pro zpětnou kompatibilitu
 */
class FlashMutex {
public:
    static void setWaitDMACallback(void (*callback)()) {
        VSyncManager::getInstance().setWaitDMACallback(callback);
    }
    
    static bool lock(const char* tag = "unknown", uint32_t timeoutMs = 5000) {
        return VSyncManager::getInstance().lockForFlash(tag, timeoutMs);
    }
    
    static void unlock(const char* tag = "unknown") {
        VSyncManager::getInstance().unlockFlash(tag);
    }
    
    static bool isLocked() {
        return false;
    }
};
