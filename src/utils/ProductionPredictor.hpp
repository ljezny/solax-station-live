#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <esp_heap_caps.h>

/**
 * Predikce výroby na základě historie
 * 
 * Ukládá průměrnou výrobu po čtvrthodinách pro každý měsíc.
 * Výroba je silně závislá na ročním období a počasí.
 * Používá exponenciální klouzavý průměr pro vyhlazení.
 * 
 * Obsahuje adaptivní korekci - porovnává skutečnou výrobu s predikcí
 * a upravuje budoucí predikce podle aktuálního počasí.
 * 
 * Velká pole jsou alokována v PSRAM pro úsporu interní RAM.
 */

#define QUARTERS_PER_DAY 96
#define MONTHS_PER_YEAR 12

class ProductionPredictor {
private:
    static const char* NAMESPACE;
    
    // Průměrná výroba [Wh] pro každou čtvrthodinu každého měsíce
    // production[měsíc][čtvrthodina] kde měsíc 0=leden, 11=prosinec
    // Alokováno v PSRAM
    float* production;  // [MONTHS_PER_YEAR * QUARTERS_PER_DAY]
    
    // Koeficient exponenciálního klouzavého průměru (0-1)
    static constexpr float ALPHA = 0.2f;  // Pomalejší adaptace než u spotřeby
    
    // Počet vzorků pro každou čtvrthodinu - alokováno v PSRAM
    int* sampleCount;  // [MONTHS_PER_YEAR * QUARTERS_PER_DAY]
    
    // Akumulátor pro aktuální čtvrthodinu
    float currentQuarterAccumulator;
    int currentQuarterSampleCount;
    int lastRecordedQuarter;
    int lastRecordedMonth;
    
    // Instalovaný výkon FVE pro škálování výchozích hodnot
    int installedPowerWp;
    
    // === ADAPTIVNÍ KOREKCE ===
    // Kumulativní chyba pro korekci budoucích predikcí
    float cumulativeError;
    static constexpr float CORRECTION_ALPHA = 0.3f;  // Rychlejší adaptace pro počasí
    // Poslední skutečná výroba pro aktualizaci korekce
    float lastActualProductionWh;
    int lastCorrectionQuarter;
    
    /**
     * Vrací číslo čtvrthodiny (0-95) z času
     */
    int getQuarter(const struct tm* timeinfo) const {
        return timeinfo->tm_hour * 4 + timeinfo->tm_min / 15;
    }
    
    // Pomocné metody pro přístup k lineárnímu poli
    inline int getIndex(int month, int quarter) const {
        return month * QUARTERS_PER_DAY + quarter;
    }
    
    inline float& productionAt(int month, int quarter) {
        return production[getIndex(month, quarter)];
    }
    
    inline float productionAt(int month, int quarter) const {
        return production[getIndex(month, quarter)];
    }
    
    inline int& sampleCountAt(int month, int quarter) {
        return sampleCount[getIndex(month, quarter)];
    }
    
    inline int sampleCountAt(int month, int quarter) const {
        return sampleCount[getIndex(month, quarter)];
    }
    
public:
    ProductionPredictor(int installedPowerWp = 10000) : installedPowerWp(installedPowerWp) {
        // Alokace v PSRAM
        size_t dataSize = MONTHS_PER_YEAR * QUARTERS_PER_DAY;
        production = (float*)heap_caps_malloc(dataSize * sizeof(float), MALLOC_CAP_SPIRAM);
        sampleCount = (int*)heap_caps_malloc(dataSize * sizeof(int), MALLOC_CAP_SPIRAM);
        
        if (!production || !sampleCount) {
            log_e("Failed to allocate ProductionPredictor in PSRAM!");
            // Fallback na běžnou RAM
            if (!production) production = (float*)malloc(dataSize * sizeof(float));
            if (!sampleCount) sampleCount = (int*)malloc(dataSize * sizeof(int));
        } else {
            log_d("ProductionPredictor allocated in PSRAM (%d bytes)", 
                  dataSize * (sizeof(float) + sizeof(int)));
        }
        
        // Inicializace s výchozími hodnotami
        for (int month = 0; month < MONTHS_PER_YEAR; month++) {
            for (int quarter = 0; quarter < QUARTERS_PER_DAY; quarter++) {
                productionAt(month, quarter) = getDefaultProduction(month, quarter);
                sampleCountAt(month, quarter) = 0;
            }
        }
        currentQuarterAccumulator = 0;
        currentQuarterSampleCount = 0;
        lastRecordedQuarter = -1;
        lastRecordedMonth = -1;
        
        // Inicializace adaptivní korekce
        cumulativeError = 0;
        lastActualProductionWh = 0;
        lastCorrectionQuarter = -1;
    }
    
    ~ProductionPredictor() {
        if (production) heap_caps_free(production);
        if (sampleCount) heap_caps_free(sampleCount);
    }
    
    /**
     * Nastaví instalovaný výkon FVE
     */
    void setInstalledPower(int powerWp) {
        installedPowerWp = powerWp;
    }
    
    /**
     * Vrátí typickou výrobu pro daný měsíc a čtvrthodinu (Wh)
     * Přibližný profil pro střední Evropu
     * @param quarter čtvrthodina 0-95
     */
    float getDefaultProduction(int month, int quarter) const {
        // Sezónní koeficienty (0=leden, relativní k maximu)
        static const float seasonalFactors[MONTHS_PER_YEAR] = {
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
        
        // Hodinový profil (normalizovaný na maximální hodinu)
        // Závisí na měsíci - v létě delší den
        float sunriseHour, sunsetHour, peakHour;
        
        if (month >= 4 && month <= 7) {  // Květen - Srpen (léto)
            sunriseHour = 5.0f;
            sunsetHour = 21.0f;
            peakHour = 13.0f;
        } else if (month >= 2 && month <= 9) {  // Březen - Říjen (jaro/podzim)
            sunriseHour = 6.5f;
            sunsetHour = 18.5f;
            peakHour = 12.5f;
        } else {  // Listopad - Únor (zima)
            sunriseHour = 7.5f;
            sunsetHour = 16.5f;
            peakHour = 12.0f;
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
        int month = timeinfo->tm_mon;  // 0=leden
        
        bool quarterChanged = false;
        
        // Pokud se změnila čtvrthodina, uložíme průměr za předchozí čtvrthodinu
        if (lastRecordedQuarter != quarter || lastRecordedMonth != month) {
            if (lastRecordedQuarter >= 0 && lastRecordedMonth >= 0 && currentQuarterSampleCount > 0) {
                float avgPowerW = currentQuarterAccumulator / currentQuarterSampleCount;
                updateQuarterlyProduction(lastRecordedMonth, lastRecordedQuarter, avgPowerW);
                quarterChanged = true;
            }
            
            currentQuarterAccumulator = 0;
            currentQuarterSampleCount = 0;
            lastRecordedQuarter = quarter;
            lastRecordedMonth = month;
        }
        
        currentQuarterAccumulator += pvPowerW;
        currentQuarterSampleCount++;
        
        return quarterChanged;
    }
    
    /**
     * Aktualizuje průměrnou výrobu pro danou čtvrthodinu pomocí EMA
     */
    void updateQuarterlyProduction(int month, int quarter, float powerW) {
        if (month < 0 || month >= MONTHS_PER_YEAR || quarter < 0 || quarter >= QUARTERS_PER_DAY) {
            return;
        }
        
        // Wh = W * 0.25h (čtvrthodina)
        float productionWh = powerW * 0.25f;
        
        // === AKTUALIZACE ADAPTIVNÍ KOREKCE ===
        // Pouze během slunečních hodin (8:00 - 16:00 = čtvrthodiny 32-64)
        if (quarter >= 32 && quarter <= 64) {
            updateCorrectionFactor(month, quarter, productionWh);
        }
        
        if (sampleCountAt(month, quarter) == 0) {
            productionAt(month, quarter) = productionWh;
        } else {
            productionAt(month, quarter) = ALPHA * productionWh + (1 - ALPHA) * productionAt(month, quarter);
        }
        
        sampleCountAt(month, quarter)++;
        
        log_d("Updated production for month %d, quarter %d: %.1f Wh (samples: %d, correction: %.1f)", 
              month, quarter, productionAt(month, quarter), sampleCountAt(month, quarter), cumulativeError);
    }
    
    /**
     * Aktualizuje korekční faktor na základě rozdílu skutečné a predikované výroby
     * Používá exponenciální vyhlazování s α=0.3
     */
    void updateCorrectionFactor(int month, int quarter, float actualWh) {
        // Získáme základní predikci (bez korekce)
        float basePrediction = getBasePrediction(month, quarter);
        
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
    float getBasePrediction(int month, int quarter) const {
        if (month < 0 || month >= MONTHS_PER_YEAR || quarter < 0 || quarter >= QUARTERS_PER_DAY) {
            return getDefaultProduction(month, quarter);
        }
        
        if (sampleCountAt(month, quarter) == 0) {
            return getDefaultProduction(month, quarter);
        }
        
        return productionAt(month, quarter);
    }
    
    /**
     * Predikuje výrobu pro danou čtvrthodinu S KOREKCÍ
     * @param month měsíc (0=leden)
     * @param quarter čtvrthodina (0-95)
     * @return predikovaná výroba v Wh (korigovaná podle aktuálního počasí)
     */
    float predictQuarterlyProduction(int month, int quarter) const {
        float basePrediction = getBasePrediction(month, quarter);
        
        // Aplikujeme korekci
        float corrected = basePrediction + cumulativeError;
        
        // Omezení: min 0, max 3× základní predikce (pro případ velmi jasného dne)
        float maxCorrection = basePrediction * 3.0f;
        corrected = constrain(corrected, 0.0f, maxCorrection);
        
        return corrected;
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
     * @param month měsíc (0=leden)
     * @param hour hodina (0-23)
     * @return predikovaná výroba v Wh
     */
    float predictHourlyProduction(int month, int hour) const {
        float totalWh = 0;
        int startQuarter = hour * 4;
        for (int q = 0; q < 4; q++) {
            totalWh += predictQuarterlyProduction(month, startQuarter + q);
        }
        return totalWh;
    }
    
    /**
     * Predikuje výrobu pro zbytek dne
     * @param fromQuarter od které čtvrthodiny (včetně), -1 pro aktuální
     * @return predikovaná výroba v kWh
     */
    float predictRemainingDayProduction(int fromQuarter = -1) const {
        time_t now = time(nullptr);
        struct tm* timeinfo = localtime(&now);
        if (fromQuarter < 0) fromQuarter = getQuarter(timeinfo);
        int month = timeinfo->tm_mon;
        
        float totalWh = 0;
        for (int quarter = fromQuarter; quarter < QUARTERS_PER_DAY; quarter++) {
            totalWh += predictQuarterlyProduction(month, quarter);
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
        int currentMonth = timeinfo->tm_mon;
        
        float totalWh = 0;
        for (int i = 0; i < quarters; i++) {
            int quarter = (currentQuarter + i) % QUARTERS_PER_DAY;
            // Zjednodušení: předpokládáme stejný měsíc
            totalWh += predictQuarterlyProduction(currentMonth, quarter);
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
        int currentMonth = timeinfo->tm_mon;
        
        count = QUARTERS_PER_DAY - currentQuarter;
        for (int i = 0; i < count; i++) {
            predictions[i] = predictQuarterlyProduction(currentMonth, currentQuarter + i) / 1000.0f;  // kWh
        }
    }
    
    /**
     * Uloží historii do NVS
     */
    void saveToPreferences() {
        Preferences preferences;
        if (preferences.begin(NAMESPACE, false)) {
            preferences.putInt("instPwr", installedPowerWp);
            
            // Komprimovaná data: uint16_t místo float, uint8_t místo int
            uint16_t compressedProd[QUARTERS_PER_DAY];
            uint8_t compressedCnt[QUARTERS_PER_DAY];
            
            for (int month = 0; month < MONTHS_PER_YEAR; month++) {
                // Komprimace production na uint16_t (Wh hodnoty 0-65535)
                for (int q = 0; q < QUARTERS_PER_DAY; q++) {
                    float val = productionAt(month, q);
                    compressedProd[q] = (uint16_t)constrain(val, 0.0f, 65535.0f);
                    compressedCnt[q] = (uint8_t)min(sampleCountAt(month, q), 255);
                }
                
                char key[16];
                snprintf(key, sizeof(key), "p%d", month);
                preferences.putBytes(key, compressedProd, sizeof(compressedProd));
                
                snprintf(key, sizeof(key), "n%d", month);
                preferences.putBytes(key, compressedCnt, sizeof(compressedCnt));
            }
            preferences.end();
            log_d("Production history saved (compressed)");
        }
    }
    
    /**
     * Načte historii z NVS
     */
    void loadFromPreferences() {
        Preferences preferences;
        if (preferences.begin(NAMESPACE, true)) {
            installedPowerWp = preferences.getInt("instPwr", installedPowerWp);
            
            uint16_t compressedProd[QUARTERS_PER_DAY];
            uint8_t compressedCnt[QUARTERS_PER_DAY];
            
            for (int month = 0; month < MONTHS_PER_YEAR; month++) {
                char key[16];
                
                // Načtení komprimovaných dat výroby
                snprintf(key, sizeof(key), "p%d", month);
                if (preferences.getBytes(key, compressedProd, sizeof(compressedProd)) > 0) {
                    for (int q = 0; q < QUARTERS_PER_DAY; q++) {
                        productionAt(month, q) = (float)compressedProd[q];
                    }
                } else {
                    for (int quarter = 0; quarter < QUARTERS_PER_DAY; quarter++) {
                        productionAt(month, quarter) = getDefaultProduction(month, quarter);
                    }
                }
                
                // Načtení sampleCount
                snprintf(key, sizeof(key), "n%d", month);
                if (preferences.getBytes(key, compressedCnt, sizeof(compressedCnt)) > 0) {
                    for (int q = 0; q < QUARTERS_PER_DAY; q++) {
                        sampleCountAt(month, q) = (int)compressedCnt[q];
                    }
                } else {
                    for (int q = 0; q < QUARTERS_PER_DAY; q++) {
                        sampleCountAt(month, q) = 0;
                    }
                }
            }
            preferences.end();
            log_d("Production history loaded");
        }
    }
    
    /**
     * Vrátí očekávanou denní výrobu pro aktuální měsíc v kWh
     */
    float getExpectedDailyProduction() const {
        time_t now = time(nullptr);
        struct tm* timeinfo = localtime(&now);
        int month = timeinfo->tm_mon;
        
        float totalWh = 0;
        for (int quarter = 0; quarter < QUARTERS_PER_DAY; quarter++) {
            totalWh += predictQuarterlyProduction(month, quarter);
        }
        
        return totalWh / 1000.0f;
    }
    
    /**
     * Kontroluje, zda máme dostatek dat pro spolehlivou predikci
     */
    bool hasReliableData() const {
        time_t now = time(nullptr);
        struct tm* timeinfo = localtime(&now);
        int month = timeinfo->tm_mon;
        
        // Pro aktuální měsíc potřebujeme alespoň nějaké vzorky pro denní čtvrthodiny
        // Denní hodiny 6-20 = čtvrthodiny 24-80
        int samplesFound = 0;
        for (int quarter = 24; quarter < 80; quarter++) {
            if (sampleCountAt(month, quarter) > 0) {
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
};

const char* ProductionPredictor::NAMESPACE = "prodpred";
