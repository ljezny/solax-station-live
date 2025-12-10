#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <cmath>
#include "NVSMutex.hpp"

/**
 * Predikce výroby na základě historie
 * 
 * Zjednodušená verze: ukládá pouze 96 čtvrthodin (1 den).
 * Každý nový vzorek se přidává s váhou 10% (EMA α=0.1).
 * To znamená, že predikce reflektuje přibližně posledních 10 dní.
 * 
 * Automaticky se adaptuje na aktuální sezónu - není potřeba
 * ukládat data pro každý měsíc zvlášť.
 * 
 * Obsahuje adaptivní korekci pro aktuální den - porovnává skutečnou 
 * výrobu s predikcí a upravuje budoucí predikce podle aktuálního počasí.
 */

#define QUARTERS_PER_DAY 96

class ProductionPredictor {
private:
    static const char* NAMESPACE;
    
    // Průměrná výroba [Wh] pro každou čtvrthodinu
    // production[čtvrthodina] kde čtvrthodina 0-95
    float production[QUARTERS_PER_DAY];
    
    // Počet vzorků pro každou čtvrthodinu (pro inicializaci)
    int sampleCount[QUARTERS_PER_DAY];
    
    // Koeficient exponenciálního klouzavého průměru
    // α=0.1 znamená ~10 dní "paměti"
    static constexpr float ALPHA = 0.1f;
    
    // Akumulátor pro aktuální čtvrthodinu
    float currentQuarterAccumulator;
    int currentQuarterSampleCount;
    int lastRecordedQuarter;
    
    // Instalovaný výkon FVE pro škálování výchozích hodnot
    int installedPowerWp;
    
    // === ADAPTIVNÍ KOREKCE ===
    // Kumulativní chyba pro korekci budoucích predikcí (aktuální den)
    float cumulativeError;
    static constexpr float CORRECTION_ALPHA = 0.3f;  // Rychlejší adaptace pro počasí
    int lastCorrectionQuarter;
    
    /**
     * Vrací číslo čtvrthodiny (0-95) z času
     */
    int getQuarter(const struct tm* timeinfo) const {
        return timeinfo->tm_hour * 4 + timeinfo->tm_min / 15;
    }
    
    /**
     * Vrátí typickou výrobu pro danou čtvrthodinu (Wh)
     * Přibližný profil založený na aktuálním měsíci
     */
    float getDefaultProduction(int quarter) const {
        time_t now = time(nullptr);
        struct tm* timeinfo = localtime(&now);
        int month = timeinfo->tm_mon;
        
        // Sezónní koeficienty (0=leden, relativní k maximu)
        static const float seasonalFactors[12] = {
            0.15f,  // Leden
            0.25f,  // Únor
            0.45f,  // Březen
            0.65f,  // Duben
            0.85f,  // Květen
            0.95f,  // Červen
            1.00f,  // Červenec
            0.90f,  // Srpen
            0.65f,  // Září
            0.40f,  // Říjen
            0.20f,  // Listopad
            0.12f   // Prosinec
        };
        
        // Hodinový profil závisí na měsíci
        float sunriseHour, sunsetHour;
        
        if (month >= 4 && month <= 7) {  // Květen - Srpen (léto)
            sunriseHour = 5.0f;
            sunsetHour = 21.0f;
        } else if (month >= 2 && month <= 9) {  // Březen - Říjen (jaro/podzim)
            sunriseHour = 6.5f;
            sunsetHour = 18.5f;
        } else {  // Listopad - Únor (zima)
            sunriseHour = 7.5f;
            sunsetHour = 16.5f;
        }
        
        // Převod čtvrthodiny na hodinu
        float hourF = (float)quarter / 4.0f;
        if (hourF < sunriseHour || hourF > sunsetHour) {
            return 0;
        }
        
        // Normalizovaná pozice v rámci dne
        float dayLength = sunsetHour - sunriseHour;
        float normalizedHour = (hourF - sunriseHour) / dayLength;
        
        // Sinusový profil (max uprostřed dne)
        float hourlyFactor = sin(normalizedHour * PI);
        
        // Maximální čtvrthodinová výroba = 80% instalovaného výkonu * 0.25h
        float maxQuarterlyProduction = installedPowerWp * 0.8f * 0.25f;
        
        return maxQuarterlyProduction * seasonalFactors[month] * hourlyFactor;
    }
    
public:
    ProductionPredictor(int installedPowerWp = 10000) : installedPowerWp(installedPowerWp) {
        // Inicializace s výchozími hodnotami
        for (int quarter = 0; quarter < QUARTERS_PER_DAY; quarter++) {
            production[quarter] = getDefaultProduction(quarter);
            sampleCount[quarter] = 0;
        }
        currentQuarterAccumulator = 0;
        currentQuarterSampleCount = 0;
        lastRecordedQuarter = -1;
        
        // Inicializace adaptivní korekce
        cumulativeError = 0;
        lastCorrectionQuarter = -1;
        
        log_d("ProductionPredictor initialized with %d Wp, using %d bytes", 
              installedPowerWp, sizeof(production) + sizeof(sampleCount));
    }
    
    /**
     * Nastaví instalovaný výkon FVE
     */
    void setInstalledPower(int powerWp) {
        installedPowerWp = powerWp;
    }
    
    /**
     * Přidá vzorek výroby
     * @param pvPowerW aktuální výkon FVE ve wattech
     * @param timestamp aktuální čas (nebo 0 pro použití systémového času)
     * @return true pokud se změnila čtvrthodina a byla uložena nová hodnota
     */
    bool addSample(int pvPowerW, time_t timestamp = 0) {
        if (timestamp == 0) {
            timestamp = time(nullptr);
        }
        
        struct tm* timeinfo = localtime(&timestamp);
        int quarter = getQuarter(timeinfo);
        
        bool quarterChanged = false;
        
        // Pokud se změnila čtvrthodina, uložíme průměr za předchozí čtvrthodinu
        if (lastRecordedQuarter != quarter) {
            if (lastRecordedQuarter >= 0 && currentQuarterSampleCount > 0) {
                float avgPowerW = currentQuarterAccumulator / currentQuarterSampleCount;
                updateQuarterlyProduction(lastRecordedQuarter, avgPowerW);
                quarterChanged = true;
            }
            
            currentQuarterAccumulator = 0;
            currentQuarterSampleCount = 0;
            lastRecordedQuarter = quarter;
        }
        
        currentQuarterAccumulator += pvPowerW;
        currentQuarterSampleCount++;
        
        return quarterChanged;
    }
    
    /**
     * Aktualizuje průměrnou výrobu pro danou čtvrthodinu pomocí EMA
     */
    void updateQuarterlyProduction(int quarter, float powerW) {
        if (quarter < 0 || quarter >= QUARTERS_PER_DAY) {
            return;
        }
        
        // Wh = W * 0.25h (čtvrthodina)
        float productionWh = powerW * 0.25f;
        
        // === AKTUALIZACE ADAPTIVNÍ KOREKCE ===
        // Pouze během slunečních hodin (8:00 - 16:00 = čtvrthodiny 32-64)
        if (quarter >= 32 && quarter <= 64) {
            updateCorrectionFactor(quarter, productionWh);
        }
        
        // EMA aktualizace
        if (sampleCount[quarter] == 0) {
            // První vzorek - přímé přiřazení
            production[quarter] = productionWh;
        } else {
            // EMA: nová = α * aktuální + (1-α) * stará
            production[quarter] = ALPHA * productionWh + (1 - ALPHA) * production[quarter];
        }
        
        sampleCount[quarter]++;
        
        log_d("Updated production for quarter %d: %.1f Wh (samples: %d, correction: %.1f)", 
              quarter, production[quarter], sampleCount[quarter], cumulativeError);
    }
    
    /**
     * Aktualizuje korekční faktor na základě rozdílu skutečné a predikované výroby
     */
    void updateCorrectionFactor(int quarter, float actualWh) {
        // Získáme základní predikci (bez korekce)
        float basePrediction = getBasePrediction(quarter);
        
        // Spočítáme chybu
        float error = actualWh - basePrediction;
        
        // Exponenciální vyhlazování chyby
        cumulativeError = CORRECTION_ALPHA * error + (1 - CORRECTION_ALPHA) * cumulativeError;
        
        // Reset korekce o půlnoci
        if (quarter < lastCorrectionQuarter) {
            cumulativeError = 0;
            log_d("Production correction reset at midnight");
        }
        lastCorrectionQuarter = quarter;
        
        log_d("Production correction updated: actual=%.1f, predicted=%.1f, error=%.1f, cumError=%.1f",
              actualWh, basePrediction, error, cumulativeError);
    }
    
    /**
     * Vrátí základní predikci BEZ korekce (pro výpočet chyby)
     */
    float getBasePrediction(int quarter) const {
        if (quarter < 0 || quarter >= QUARTERS_PER_DAY) {
            return getDefaultProduction(quarter);
        }
        
        if (sampleCount[quarter] == 0) {
            return getDefaultProduction(quarter);
        }
        
        return production[quarter];
    }
    
    /**
     * Predikuje výrobu pro danou čtvrthodinu S KOREKCÍ
     * Korekce klesá exponenciálně se vzdáleností od aktuální čtvrthodiny.
     * Příklad: je zataženo teď → predikce za 1h je nízká, za 4h už částečná korekce,
     * za 8h už skoro bez korekce (počasí se může změnit).
     * 
     * @param quarter čtvrthodina (0-95)
     * @return predikovaná výroba v Wh (korigovaná podle aktuálního počasí)
     */
    float predictQuarterlyProduction(int quarter) const {
        float basePrediction = getBasePrediction(quarter);
        
        // === VZDÁLENOSTNÍ KOREKCE ===
        // Chyba se propaguje s klesající vahou podle vzdálenosti od aktuální čtvrthodiny
        // Poločas rozpadu ~4 hodiny (16 quarters) - počasí se mění rychleji než spotřeba
        // distance=0: 100%, distance=4 (1h): 78%, distance=16 (4h): 37%, distance=32 (8h): 14%
        time_t now = time(nullptr);
        struct tm* timeinfo = localtime(&now);
        int currentQ = getQuarter(timeinfo);
        int distance = quarter - currentQ;
        if (distance < 0) distance += QUARTERS_PER_DAY;  // Wrap around pro příští den
        
        // Exponenciální pokles: e^(-distance/16)
        float decayFactor = exp(-distance / 16.0f);
        
        // Aplikujeme korekci s klesající vahou
        float corrected = basePrediction + (cumulativeError * decayFactor);
        
        // Omezení: min 0, max 3× základní predikce (pro případ velmi jasného dne)
        float maxCorrection = max(basePrediction * 3.0f, 100.0f);
        corrected = constrain(corrected, 0.0f, maxCorrection);
        
        return corrected;
    }
    
    /**
     * Přetížení pro zpětnou kompatibilitu (ignoruje měsíc)
     */
    float predictQuarterlyProduction(int month, int quarter) const {
        return predictQuarterlyProduction(quarter);
    }
    
    /**
     * Vrátí aktuální korekční chybu (pro debug/UI)
     */
    float getCorrectionError() const {
        return cumulativeError;
    }
    
    /**
     * Resetuje korekci (např. při změně počasí)
     */
    void resetCorrection() {
        cumulativeError = 0;
        lastCorrectionQuarter = -1;
    }
    
    /**
     * Predikuje výrobu pro danou hodinu (součet 4 čtvrthodin)
     * @param hour hodina (0-23)
     * @return predikovaná výroba v Wh
     */
    float predictHourlyProduction(int hour) const {
        float totalWh = 0;
        int startQuarter = hour * 4;
        for (int q = 0; q < 4; q++) {
            totalWh += predictQuarterlyProduction(startQuarter + q);
        }
        return totalWh;
    }
    
    /**
     * Přetížení pro zpětnou kompatibilitu (ignoruje měsíc)
     */
    float predictHourlyProduction(int month, int hour) const {
        return predictHourlyProduction(hour);
    }
    
    /**
     * Predikuje výrobu pro zbytek dne
     * @param fromQuarter od které čtvrthodiny (včetně), -1 pro aktuální
     * @return predikovaná výroba v kWh
     */
    float predictRemainingDayProduction(int fromQuarter = -1) const {
        if (fromQuarter < 0) {
            time_t now = time(nullptr);
            struct tm* timeinfo = localtime(&now);
            fromQuarter = getQuarter(timeinfo);
        }
        
        float totalWh = 0;
        for (int quarter = fromQuarter; quarter < QUARTERS_PER_DAY; quarter++) {
            totalWh += predictQuarterlyProduction(quarter);
        }
        
        return totalWh / 1000.0f;
    }
    
    /**
     * Predikuje výrobu pro dalších N čtvrthodin
     * @param quarters počet čtvrthodin
     * @return predikovaná výroba v kWh
     */
    float predictNextQuartersProduction(int quarters) const {
        time_t now = time(nullptr);
        struct tm* timeinfo = localtime(&now);
        int currentQuarter = getQuarter(timeinfo);
        
        float totalWh = 0;
        for (int i = 0; i < quarters; i++) {
            int quarter = (currentQuarter + i) % QUARTERS_PER_DAY;
            totalWh += predictQuarterlyProduction(quarter);
        }
        
        return totalWh / 1000.0f;
    }
    
    /**
     * Predikuje výrobu pro dalších N hodin (zpětná kompatibilita)
     */
    float predictNextHoursProduction(int hours) const {
        return predictNextQuartersProduction(hours * 4);
    }
    
    /**
     * Vrátí pole predikcí výroby po čtvrthodinách pro zbytek dne
     * @param predictions výstupní pole (musí mít velikost alespoň 96)
     * @param count výstupní počet čtvrthodin
     */
    void getPredictionsForRemainingDay(float* predictions, int& count) const {
        time_t now = time(nullptr);
        struct tm* timeinfo = localtime(&now);
        int currentQuarter = getQuarter(timeinfo);
        
        count = QUARTERS_PER_DAY - currentQuarter;
        for (int i = 0; i < count; i++) {
            predictions[i] = predictQuarterlyProduction(currentQuarter + i) / 1000.0f;  // kWh
        }
    }
    
    /**
     * Vymaže všechna naučená data a reset na výchozí hodnoty
     * Používá se při RESET tlačítku v UI
     */
    void clearAllData() {
        log_i("Clearing all production prediction data");
        
        // Reset všech dat na výchozí hodnoty
        for (int q = 0; q < QUARTERS_PER_DAY; q++) {
            production[q] = 0;
            sampleCount[q] = 0;
        }
        
        // Reset korekce a stavu
        resetCorrection();
        lastRecordedQuarter = -1;
        currentQuarterAccumulator = 0;
        currentQuarterSampleCount = 0;
        
        // Smazání z NVS
        NVSGuard guard;
        if (guard.isLocked()) {
            Preferences preferences;
            if (preferences.begin(NAMESPACE, false)) {
                preferences.clear();
                preferences.end();
                log_d("Production prediction NVS data cleared");
            }
        }
    }
    
    /**
     * Uloží historii do NVS
     */
    void saveToPreferences() {
        NVSGuard guard;
        if (!guard.isLocked()) {
            log_e("Failed to lock NVS mutex for saving production history");
            return;
        }
        
        Preferences preferences;
        if (preferences.begin(NAMESPACE, false)) {
            preferences.putInt("instPwr", installedPowerWp);
            
            // Komprimovaná data: uint16_t místo float
            uint16_t compressedProd[QUARTERS_PER_DAY];
            uint8_t compressedCnt[QUARTERS_PER_DAY];
            
            for (int q = 0; q < QUARTERS_PER_DAY; q++) {
                compressedProd[q] = (uint16_t)constrain(production[q], 0.0f, 65535.0f);
                compressedCnt[q] = (uint8_t)min(sampleCount[q], 255);
            }
            
            preferences.putBytes("prod", compressedProd, sizeof(compressedProd));
            preferences.putBytes("cnt", compressedCnt, sizeof(compressedCnt));
            
            preferences.end();
            log_d("Production history saved (simplified format)");
        }
    }
    
    /**
     * Načte historii z NVS
     */
    void loadFromPreferences() {
        NVSGuard guard;
        if (!guard.isLocked()) {
            log_e("Failed to lock NVS mutex for loading production history");
            return;
        }
        
        Preferences preferences;
        if (preferences.begin(NAMESPACE, true)) {
            installedPowerWp = preferences.getInt("instPwr", installedPowerWp);
            
            uint16_t compressedProd[QUARTERS_PER_DAY];
            uint8_t compressedCnt[QUARTERS_PER_DAY];
            
            // Zkusíme načíst nový formát
            if (preferences.getBytes("prod", compressedProd, sizeof(compressedProd)) > 0) {
                for (int q = 0; q < QUARTERS_PER_DAY; q++) {
                    production[q] = (float)compressedProd[q];
                }
                log_d("Production loaded from new format");
            } else {
                // Zkusíme starý formát (měsíční) - načteme aktuální měsíc
                time_t now = time(nullptr);
                struct tm* timeinfo = localtime(&now);
                int month = timeinfo->tm_mon;
                
                char key[16];
                snprintf(key, sizeof(key), "p%d", month);
                if (preferences.getBytes(key, compressedProd, sizeof(compressedProd)) > 0) {
                    for (int q = 0; q < QUARTERS_PER_DAY; q++) {
                        production[q] = (float)compressedProd[q];
                    }
                    log_d("Production migrated from old format (month %d)", month);
                } else {
                    // Žádná data - použijeme výchozí hodnoty
                    for (int q = 0; q < QUARTERS_PER_DAY; q++) {
                        production[q] = getDefaultProduction(q);
                    }
                    log_d("Production initialized with defaults");
                }
            }
            
            // Načtení sample count
            if (preferences.getBytes("cnt", compressedCnt, sizeof(compressedCnt)) > 0) {
                for (int q = 0; q < QUARTERS_PER_DAY; q++) {
                    sampleCount[q] = (int)compressedCnt[q];
                }
            } else {
                for (int q = 0; q < QUARTERS_PER_DAY; q++) {
                    sampleCount[q] = 0;
                }
            }
            
            preferences.end();
        }
    }
    
    /**
     * Vrátí očekávanou denní výrobu v kWh
     */
    float getExpectedDailyProduction() const {
        float totalWh = 0;
        for (int quarter = 0; quarter < QUARTERS_PER_DAY; quarter++) {
            totalWh += predictQuarterlyProduction(quarter);
        }
        return totalWh / 1000.0f;
    }
    
    /**
     * Kontroluje, zda máme dostatek dat pro spolehlivou predikci
     */
    bool hasReliableData() const {
        // Pro denní hodiny 6-20 = čtvrthodiny 24-80
        int samplesFound = 0;
        for (int quarter = 24; quarter < 80; quarter++) {
            if (sampleCount[quarter] > 0) {
                samplesFound++;
            }
        }
        return samplesFound >= 40;  // Alespoň 40 čtvrthodin s daty (= 10 hodin)
    }
    
    /**
     * Vrátí instalovaný výkon
     */
    int getInstalledPower() const {
        return installedPowerWp;
    }
    
    /**
     * Vrátí aktuální čtvrthodinu (pro debug)
     */
    int getCurrentQuarter() const {
        time_t now = time(nullptr);
        struct tm* timeinfo = localtime(&now);
        return getQuarter(timeinfo);
    }
};

const char* ProductionPredictor::NAMESPACE = "prodpred";
