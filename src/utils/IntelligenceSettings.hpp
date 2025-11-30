#pragma once

#include <Arduino.h>
#include <Preferences.h>

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
    
    // Výchozí hodnoty
    static IntelligenceSettings getDefault() {
        IntelligenceSettings settings;
        settings.enabled = false;
        settings.batteryCostPerKwh = 2.0f;  // 2 Kč/kWh
        settings.buyK = 1.21f;              // +21% DPH
        settings.buyQ = 2.5f;               // +2.5 Kč distribuční poplatky
        settings.sellK = 0.9f;              // 90% spotu
        settings.sellQ = 0.0f;              // žádná fixní srážka
        settings.minSocPercent = 10;
        settings.maxSocPercent = 95;
        return settings;
    }
} IntelligenceSettings_t;

/**
 * Třída pro ukládání a načítání nastavení inteligence
 */
class IntelligenceSettingsStorage {
private:
    static const char* NAMESPACE;
    
public:
    /**
     * Načte nastavení z NVS
     */
    static IntelligenceSettings_t load() {
        IntelligenceSettings_t settings = IntelligenceSettings_t::getDefault();
        
        Preferences preferences;
        if (preferences.begin(NAMESPACE, true)) {  // read-only
            settings.enabled = preferences.getBool("enabled", settings.enabled);
            settings.batteryCostPerKwh = preferences.getFloat("batCost", settings.batteryCostPerKwh);
            settings.buyK = preferences.getFloat("buyK", settings.buyK);
            settings.buyQ = preferences.getFloat("buyQ", settings.buyQ);
            settings.sellK = preferences.getFloat("sellK", settings.sellK);
            settings.sellQ = preferences.getFloat("sellQ", settings.sellQ);
            settings.minSocPercent = preferences.getInt("minSoc", settings.minSocPercent);
            settings.maxSocPercent = preferences.getInt("maxSoc", settings.maxSocPercent);
            preferences.end();
        }
        
        log_d("Intelligence settings loaded: enabled=%d, batCost=%.2f, buyK=%.2f, buyQ=%.2f, sellK=%.2f, sellQ=%.2f, minSoc=%d, maxSoc=%d",
              settings.enabled, settings.batteryCostPerKwh, settings.buyK, settings.buyQ, 
              settings.sellK, settings.sellQ, settings.minSocPercent, settings.maxSocPercent);
        
        return settings;
    }
    
    /**
     * Uloží nastavení do NVS
     */
    static void save(const IntelligenceSettings_t& settings) {
        Preferences preferences;
        if (preferences.begin(NAMESPACE, false)) {  // read-write
            preferences.putBool("enabled", settings.enabled);
            preferences.putFloat("batCost", settings.batteryCostPerKwh);
            preferences.putFloat("buyK", settings.buyK);
            preferences.putFloat("buyQ", settings.buyQ);
            preferences.putFloat("sellK", settings.sellK);
            preferences.putFloat("sellQ", settings.sellQ);
            preferences.putInt("minSoc", settings.minSocPercent);
            preferences.putInt("maxSoc", settings.maxSocPercent);
            preferences.end();
            
            log_d("Intelligence settings saved");
        } else {
            log_e("Failed to open preferences for writing");
        }
    }
    
    /**
     * Resetuje nastavení na výchozí hodnoty
     */
    static void reset() {
        save(IntelligenceSettings_t::getDefault());
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

// Definice static členské proměnné
const char* IntelligenceSettingsStorage::NAMESPACE = "intel";
