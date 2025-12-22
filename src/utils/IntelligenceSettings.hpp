#pragma once

#include <Arduino.h>
#include <RemoteLogger.hpp>
#include <Preferences.h>
#include "FlashMutex.hpp"

/**
 * Nastavení inteligentního řízení střídače
 * 
 * Ekonomický model:
 * - Nákupní cena = buyK * spotPrice + buyQ
 * - Prodejní cena = sellK * spotPrice + sellQ
 * - Cena baterie = náklady na jeden cyklus kWh (opotřebení, ztráty)
 */
typedef struct IntelligenceSettings {
    bool enabled;                      // Inteligentní řízení zapnuto
    
    // Cena za použití 1 kWh baterie (opotřebení cyklu + ztráty)
    // Příklad: baterie 10kWh za 100000 Kč, 6000 cyklů = 100000/(10*6000) = 1.67 Kč/kWh
    float batteryCostPerKwh;           // Kč/kWh nebo ct/kWh dle měny
    
    // Lineární koeficienty pro NÁKUP: skutečná_cena = buyK * spot + buyQ
    // buyK zahrnuje DPH, distribuční poplatky jako násobitel
    // buyQ zahrnuje fixní poplatky za kWh
    float buyK;                        // např. 1.21 (DPH 21%)
    float buyQ;                        // např. 2.5 Kč (distribuční poplatek/kWh)
    
    // Lineární koeficienty pro PRODEJ: skutečná_cena = sellK * spot + sellQ
    // sellK < 1 protože výkupní cena bývá nižší než spot
    // sellQ může být záporné (fixní srážka)
    float sellK;                       // např. 0.9
    float sellQ;                       // např. -0.5 Kč
    
    // Ochranné limity baterie
    int minSocPercent;                 // Minimální SOC (ochrana baterie), default 10%
    int maxSocPercent;                 // Maximální SOC pro nabíjení ze sítě, default 95%
    
    // Parametry baterie (použijí se pokud se nepodaří načíst ze střídače)
    float batteryCapacityKwh;          // Kapacita baterie v kWh, default 10
    float maxChargePowerKw;            // Maximální nabíjecí výkon v kW, default 5
    float maxDischargePowerKw;         // Maximální vybíjecí výkon v kW, default 5
    
    // Výchozí hodnoty
    static IntelligenceSettings getDefault() {
        IntelligenceSettings settings;
        settings.enabled = false;
        settings.batteryCostPerKwh = 1.0f;  // 1 Kč/kWh (výchozí)
        settings.buyK = 1.21f;              // +21% DPH
        settings.buyQ = 2.5f;               // +2.5 Kč distribuční poplatky
        settings.sellK = 0.9f;              // 90% spotu
        settings.sellQ = 0.0f;              // žádná fixní srážka
        settings.minSocPercent = 30;
        settings.maxSocPercent = 85;
        settings.batteryCapacityKwh = 10.0f;   // 10 kWh
        settings.maxChargePowerKw = 8.0f;      // 8 kW default
        settings.maxDischargePowerKw = 8.0f;   // 8 kW default
        return settings;
    }
} IntelligenceSettings_t;

/**
 * Třída pro ukládání a načítání nastavení inteligence
 */
class IntelligenceSettingsStorage {
private:
    static const char* NAMESPACE;
    static uint16_t lastKnownCapacityWh;  // Cache pro poslední známou kapacitu
    
public:
    /**
     * Načte nastavení z NVS
     */
    static IntelligenceSettings_t load() {
        IntelligenceSettings_t settings = IntelligenceSettings_t::getDefault();
        
        FlashGuard guard("Intelligence:load");
        if (!guard.isLocked()) {
            return settings;  // Nelogovat - jsme v kritické sekci
        }
        
        Preferences preferences;
        if (preferences.begin(NAMESPACE, true)) {  // read-only
            // Use short keys matching save()
            settings.enabled = preferences.getBool("en", settings.enabled);
            settings.batteryCostPerKwh = preferences.getFloat("bc", settings.batteryCostPerKwh);
            settings.buyK = preferences.getFloat("bk", settings.buyK);
            settings.buyQ = preferences.getFloat("bq", settings.buyQ);
            settings.sellK = preferences.getFloat("sk", settings.sellK);
            settings.sellQ = preferences.getFloat("sq", settings.sellQ);
            settings.minSocPercent = preferences.getInt("mn", settings.minSocPercent);
            settings.maxSocPercent = preferences.getInt("mx", settings.maxSocPercent);
            settings.batteryCapacityKwh = preferences.getFloat("cap", settings.batteryCapacityKwh);
            settings.maxChargePowerKw = preferences.getFloat("mcp", settings.maxChargePowerKw);
            settings.maxDischargePowerKw = preferences.getFloat("mdp", settings.maxDischargePowerKw);
            preferences.end();
        }
        
        // Nelogovat - jsme v kritické sekci
        
        return settings;
    }
    
    /**
     * Uloží nastavení do NVS
     */
    static void save(const IntelligenceSettings_t& settings) {
        FlashGuard guard("Intelligence:save");
        if (!guard.isLocked()) {
            return;  // Nelogovat - jsme v kritické sekci
        }
        
        Preferences preferences;
        if (preferences.begin(NAMESPACE, false)) {  // read-write
            // Clear old entries first to prevent fragmentation
            preferences.clear();
            
            // Use shorter keys to save space
            preferences.putBool("en", settings.enabled);
            preferences.putFloat("bc", settings.batteryCostPerKwh);
            preferences.putFloat("bk", settings.buyK);
            preferences.putFloat("bq", settings.buyQ);
            preferences.putFloat("sk", settings.sellK);
            preferences.putFloat("sq", settings.sellQ);
            preferences.putInt("mn", settings.minSocPercent);
            preferences.putInt("mx", settings.maxSocPercent);
            preferences.putFloat("cap", settings.batteryCapacityKwh);
            preferences.putFloat("mcp", settings.maxChargePowerKw);
            preferences.putFloat("mdp", settings.maxDischargePowerKw);
            preferences.end();
            // Nelogovat - jsme v kritické sekci
        }
    }
    
    /**
     * Resetuje nastavení na výchozí hodnoty
     */
    static void reset() {
        save(IntelligenceSettings_t::getDefault());
    }
    
    /**
     * Aktualizuje nastavení hodnotami ze střídače (pokud jsou k dispozici)
     * Volat po úspěšném načtení dat ze střídače.
     * 
     * Poznámka: Charge/discharge power se z inverteru NEnačítá, protože:
     * - Hodnoty se mění v čase podle stavu baterie
     * - Když je střídač v idle režimu, vrací nesmysly
     * - Uživatel si zadá hodnoty ručně v nastavení
     * 
     * @param batteryCapacityWh kapacita baterie ve Wh (0 = neaktualizovat)
     * @return true pokud byly hodnoty aktualizovány
     */
    static bool updateFromInverter(uint16_t batteryCapacityWh) {
        // Pouze pokud máme nějakou hodnotu kapacity ze střídače
        if (batteryCapacityWh == 0) {
            return false;
        }
        
        // Rychlá kontrola cache - pokud je kapacita stejná, nemusíme nic dělat
        if (lastKnownCapacityWh == batteryCapacityWh) {
            return false;  // Kapacita se nezměnila
        }
        
        LOGD("updateFromInverter: capacity changed %d -> %d Wh", lastKnownCapacityWh, batteryCapacityWh);
        lastKnownCapacityWh = batteryCapacityWh;
        
        IntelligenceSettings_t settings = load();
        bool changed = false;
        
        float newCapacity = batteryCapacityWh / 1000.0f;
        if (abs(settings.batteryCapacityKwh - newCapacity) > 0.1f) {
            settings.batteryCapacityKwh = newCapacity;
            changed = true;
            LOGD("Updated battery capacity from inverter: %.1f kWh", newCapacity);
        }
        
        if (changed) {
            save(settings);
        }
        
        return changed;
    }
    
    /**
     * Vypočítá skutečnou nákupní cenu
     * @param spotPrice spotová cena
     * @param settings nastavení
     * @return skutečná cena za nákup 1 kWh
     */
    static float calculateBuyPrice(float spotPrice, const IntelligenceSettings_t& settings) {
        return settings.buyK * spotPrice + settings.buyQ;
    }
    
    /**
     * Vypočítá skutečnou prodejní cenu
     * @param spotPrice spotová cena
     * @param settings nastavení
     * @return skutečná cena za prodej 1 kWh
     */
    static float calculateSellPrice(float spotPrice, const IntelligenceSettings_t& settings) {
        return settings.sellK * spotPrice + settings.sellQ;
    }
};

// Definice static členských proměnných
const char* IntelligenceSettingsStorage::NAMESPACE = "intel";
uint16_t IntelligenceSettingsStorage::lastKnownCapacityWh = 0;
