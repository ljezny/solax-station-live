#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "NVSMutex.hpp"

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
        settings.batteryCostPerKwh = 2.0f;  // 2 Kč/kWh
        settings.buyK = 1.21f;              // +21% DPH
        settings.buyQ = 2.5f;               // +2.5 Kč distribuční poplatky
        settings.sellK = 0.9f;              // 90% spotu
        settings.sellQ = 0.0f;              // žádná fixní srážka
        settings.minSocPercent = 10;
        settings.maxSocPercent = 95;
        settings.batteryCapacityKwh = 10.0f;   // 10 kWh
        settings.maxChargePowerKw = 5.0f;      // 5 kW
        settings.maxDischargePowerKw = 5.0f;   // 5 kW
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
        
        log_d("Intelligence settings loaded: enabled=%d, batCost=%.2f, buyK=%.2f, buyQ=%.2f, sellK=%.2f, sellQ=%.2f, minSoc=%d, maxSoc=%d, cap=%.1f, chg=%.1f, dis=%.1f",
              settings.enabled, settings.batteryCostPerKwh, settings.buyK, settings.buyQ, 
              settings.sellK, settings.sellQ, settings.minSocPercent, settings.maxSocPercent,
              settings.batteryCapacityKwh, settings.maxChargePowerKw, settings.maxDischargePowerKw);
        
        return settings;
    }
    
    /**
     * Uloží nastavení do NVS
     */
    static void save(const IntelligenceSettings_t& settings) {
        NVSGuard guard;
        if (!guard.isLocked()) {
            log_e("Failed to lock NVS mutex for saving intelligence settings");
            return;
        }
        
        Preferences preferences;
        if (preferences.begin(NAMESPACE, false)) {  // read-write
            // Clear old entries first to prevent fragmentation
            preferences.clear();
            
            // Use shorter keys to save space
            bool ok = true;
            ok &= preferences.putBool("en", settings.enabled);
            ok &= preferences.putFloat("bc", settings.batteryCostPerKwh);
            ok &= preferences.putFloat("bk", settings.buyK);
            ok &= preferences.putFloat("bq", settings.buyQ);
            ok &= preferences.putFloat("sk", settings.sellK);
            ok &= preferences.putFloat("sq", settings.sellQ);
            ok &= preferences.putInt("mn", settings.minSocPercent);
            ok &= preferences.putInt("mx", settings.maxSocPercent);
            ok &= preferences.putFloat("cap", settings.batteryCapacityKwh);
            ok &= preferences.putFloat("mcp", settings.maxChargePowerKw);
            ok &= preferences.putFloat("mdp", settings.maxDischargePowerKw);
            preferences.end();
            
            if (ok) {
                log_d("Intelligence settings saved");
            } else {
                log_e("Failed to save some intelligence settings");
            }
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
     * Aktualizuje nastavení hodnotami ze střídače (pokud jsou k dispozici)
     * Volat po úspěšném načtení dat ze střídače.
     * 
     * @param batteryCapacityWh kapacita baterie ve Wh (0 = neaktualizovat)
     * @param maxChargePowerW max nabíjecí výkon ve W (0 = neaktualizovat)
     * @param maxDischargePowerW max vybíjecí výkon ve W (0 = neaktualizovat)
     * @return true pokud byly hodnoty aktualizovány
     */
    static bool updateFromInverter(uint16_t batteryCapacityWh, uint16_t maxChargePowerW, uint16_t maxDischargePowerW) {
        log_d("updateFromInverter called: capacity=%d Wh, charge=%d W, discharge=%d W", 
              batteryCapacityWh, maxChargePowerW, maxDischargePowerW);
        
        // Pouze pokud máme nějaké hodnoty ze střídače
        if (batteryCapacityWh == 0 && maxChargePowerW == 0 && maxDischargePowerW == 0) {
            log_d("No inverter values available, skipping update");
            return false;
        }
        
        IntelligenceSettings_t settings = load();
        bool changed = false;
        
        if (batteryCapacityWh > 0) {
            float newCapacity = batteryCapacityWh / 1000.0f;
            if (abs(settings.batteryCapacityKwh - newCapacity) > 0.1f) {
                settings.batteryCapacityKwh = newCapacity;
                changed = true;
                log_d("Updated battery capacity from inverter: %.1f kWh", newCapacity);
            }
        }
        
        if (maxChargePowerW > 0) {
            float newPower = maxChargePowerW / 1000.0f;
            // Ignore values below 1 kW as likely invalid (typical batteries are 3-10 kW)
            if (newPower >= 1.0f && abs(settings.maxChargePowerKw - newPower) > 0.1f) {
                settings.maxChargePowerKw = newPower;
                changed = true;
                log_d("Updated max charge power from inverter: %.1f kW", newPower);
            }
        }
        
        if (maxDischargePowerW > 0) {
            float newPower = maxDischargePowerW / 1000.0f;
            // Ignore values below 1 kW as likely invalid (typical batteries are 3-10 kW)
            if (newPower >= 1.0f && abs(settings.maxDischargePowerKw - newPower) > 0.1f) {
                settings.maxDischargePowerKw = newPower;
                changed = true;
                log_d("Updated max discharge power from inverter: %.1f kW", newPower);
            }
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

// Definice static členské proměnné
const char* IntelligenceSettingsStorage::NAMESPACE = "intel";
