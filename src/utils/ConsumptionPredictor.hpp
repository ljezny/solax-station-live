#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <cmath>  // Pro exp()
#include "../Spot/ElectricityPriceResult.hpp"  // Pro QUARTERS_OF_DAY
#include "NVSMutex.hpp"

/**
 * Predikce spotřeby na základě historie
 * 
 * Ukládá spotřebu po čtvrthodinách pro každý den v týdnu.
 * Udržuje historii 2 týdnů - tedy 2 vzorky pro každý den týdne.
 * Predikce pro sobotu vychází z předchozích dvou sobot atd.
 * 
 * Obsahuje adaptivní korekci - porovnává skutečnou spotřebu s predikcí
 * a upravuje budoucí predikce podle aktuálního vzoru spotřeby.
 * 
 * Velká pole jsou alokována v PSRAM pro úsporu interní RAM.
 */

#define DAYS_PER_WEEK 7
#define WEEKS_HISTORY 2   // Počet týdnů historie

class ConsumptionPredictor {
private:
    static const char* NAMESPACE;
    
    // Spotřeba [Wh] pro každou čtvrthodinu, pro každý den v týdnu, pro poslední 2 týdny
    // consumption[týden][den][čtvrthodina] kde den 0=neděle, 1=pondělí, ...
    // týden 0 = aktuální týden, týden 1 = minulý týden
    // Alokováno v PSRAM
    float* consumption;  // [WEEKS_HISTORY * DAYS_PER_WEEK * QUARTERS_OF_DAY]
    
    // Zda máme platná data pro daný slot - alokováno v PSRAM
    bool* hasData;  // [WEEKS_HISTORY * DAYS_PER_WEEK * QUARTERS_OF_DAY]
    
    // Akumulátor pro aktuální čtvrthodinu
    float currentQuarterAccumulator;
    int currentQuarterSampleCount;
    int lastRecordedQuarter;
    int lastRecordedDay;
    int lastRecordedWeek;  // Číslo týdne v roce pro detekci přechodu týdne
    
    // === ADAPTIVNÍ KOREKCE ===
    // Kumulativní chyba pro korekci budoucích predikcí
    float cumulativeError;
    static constexpr float CORRECTION_ALPHA = 0.3f;  // Rychlejší adaptace pro spotřebu
    int lastCorrectionQuarter;
    
    // === CROSS-DAY PROPAGACE ===
    // Váha pro propagaci hodnot na ostatní dny (pro opakující se vzorce jako noční topení)
    static constexpr float CROSS_DAY_ALPHA = 0.12f;  // Základní váha pro ostatní dny
    
    // === PROPAGACE NA SOUSEDNÍ ČTVRTHODINY ===
    // Váha klesá s vzdáleností: alpha / (1 + distance)
    static constexpr float NEIGHBOR_QUARTER_ALPHA = 0.20f;  // Základní váha pro sousední čtvrthodiny
    static constexpr int NEIGHBOR_QUARTER_RANGE = 4;        // ±4 čtvrthodiny = ±1 hodina
    
    // === PROPAGACE NA MINULÝ TÝDEN ===
    static constexpr float LAST_WEEK_ALPHA = 0.25f;  // Váha pro stejnou čtvrthodinu minulý týden
    
    // === REALTIME KOREKCE ===
    // Aktuální výkon (W) pro průběžnou korekci predikcí
    float currentPowerW;
    static constexpr float REALTIME_ALPHA = 0.5f;  // Vyhlazení aktuálního výkonu
    
    // Pomocné metody pro přístup k lineárnímu poli
    inline int getIndex(int week, int day, int quarter) const {
        return (week * DAYS_PER_WEEK + day) * QUARTERS_OF_DAY + quarter;
    }
    
    inline float& consumptionAt(int week, int day, int quarter) {
        return consumption[getIndex(week, day, quarter)];
    }
    
    inline float consumptionAt(int week, int day, int quarter) const {
        return consumption[getIndex(week, day, quarter)];
    }
    
    inline bool& hasDataAt(int week, int day, int quarter) {
        return hasData[getIndex(week, day, quarter)];
    }
    
    inline bool hasDataAt(int week, int day, int quarter) const {
        return hasData[getIndex(week, day, quarter)];
    }
    
public:
    ConsumptionPredictor() {
        // Alokace v PSRAM
        size_t dataSize = WEEKS_HISTORY * DAYS_PER_WEEK * QUARTERS_OF_DAY;
        consumption = (float*)heap_caps_malloc(dataSize * sizeof(float), MALLOC_CAP_SPIRAM);
        hasData = (bool*)heap_caps_malloc(dataSize * sizeof(bool), MALLOC_CAP_SPIRAM);
        
        if (!consumption || !hasData) {
            log_e("Failed to allocate ConsumptionPredictor in PSRAM!");
            // Fallback na běžnou RAM
            if (!consumption) consumption = (float*)malloc(dataSize * sizeof(float));
            if (!hasData) hasData = (bool*)malloc(dataSize * sizeof(bool));
        } else {
            log_d("ConsumptionPredictor allocated in PSRAM (%d bytes)", 
                  dataSize * (sizeof(float) + sizeof(bool)));
        }
        
        // Inicializace s výchozími hodnotami
        for (int week = 0; week < WEEKS_HISTORY; week++) {
            for (int day = 0; day < DAYS_PER_WEEK; day++) {
                for (int quarter = 0; quarter < QUARTERS_OF_DAY; quarter++) {
                    consumptionAt(week, day, quarter) = getDefaultConsumption(quarter / 4);
                    hasDataAt(week, day, quarter) = false;
                }
            }
        }
        currentQuarterAccumulator = 0;
        currentQuarterSampleCount = 0;
        lastRecordedQuarter = -1;
        lastRecordedDay = -1;
        lastRecordedWeek = -1;
        
        // Inicializace adaptivní korekce
        cumulativeError = 0;
        lastCorrectionQuarter = -1;
        
        // Inicializace realtime korekce
        currentPowerW = 0;
    }
    
    ~ConsumptionPredictor() {
        if (consumption) heap_caps_free(consumption);
        if (hasData) heap_caps_free(hasData);
    }
    
    /**
     * Vrátí typickou spotřebu pro danou hodinu (Wh za hodinu -> Wh za čtvrthodinu /4)
     * Výchozí profil pro domácnost
     */
    static float getDefaultConsumption(int hour) {
        float hourlyWh;
        // Typický profil: nízká spotřeba v noci, špičky ráno a večer
        if (hour >= 0 && hour < 6) {
            hourlyWh = 300;   // Noc - základní spotřeba (lednice, standby)
        } else if (hour >= 6 && hour < 9) {
            hourlyWh = 1200;  // Ráno - příprava snídaně, sprcha
        } else if (hour >= 9 && hour < 12) {
            hourlyWh = 500;   // Dopoledne
        } else if (hour >= 12 && hour < 14) {
            hourlyWh = 1000;  // Oběd
        } else if (hour >= 14 && hour < 17) {
            hourlyWh = 500;   // Odpoledne
        } else if (hour >= 17 && hour < 21) {
            hourlyWh = 1500;  // Večer - vaření, TV, světla
        } else {
            hourlyWh = 600;   // Pozdní večer
        }
        return hourlyWh / 4.0f;  // Wh za čtvrthodinu
    }
    
    /**
     * Přidá vzorek spotřeby
     * @param loadPowerW aktuální výkon spotřeby ve wattech
     * @param timestamp aktuální čas (nebo 0 pro použití systémového času)
     * @return true pokud se změnila čtvrthodina a byla uložena nová hodnota
     */
    bool addSample(int loadPowerW, time_t timestamp = 0) {
        if (timestamp == 0) {
            timestamp = time(nullptr);
        }
        
        struct tm* timeinfo = localtime(&timestamp);
        int hour = timeinfo->tm_hour;
        int minute = timeinfo->tm_min;
        int quarter = (hour * 60 + minute) / 15;  // 0-95
        int day = timeinfo->tm_wday;  // 0=neděle
        int weekOfYear = getWeekOfYear(timeinfo);
        
        bool quarterChanged = false;
        
        // === REALTIME KOREKCE ===
        // Exponenciální vyhlazení aktuálního výkonu pro stabilnější predikce
        currentPowerW = REALTIME_ALPHA * loadPowerW + (1 - REALTIME_ALPHA) * currentPowerW;
        
        // Detekce přechodu na nový týden - posuneme historii
        if (lastRecordedWeek >= 0 && weekOfYear != lastRecordedWeek) {
            shiftWeekHistory();
        }
        lastRecordedWeek = weekOfYear;
        
        // Pokud se změnila čtvrthodina, uložíme průměr za předchozí čtvrthodinu
        if (lastRecordedQuarter != quarter || lastRecordedDay != day) {
            if (lastRecordedQuarter >= 0 && lastRecordedDay >= 0 && currentQuarterSampleCount > 0) {
                // Vypočítáme průměrný výkon za čtvrthodinu a převedeme na Wh
                float avgPowerW = currentQuarterAccumulator / currentQuarterSampleCount;
                float consumptionWh = avgPowerW / 4.0f;  // Wh za 15 minut = W * 0.25h
                updateQuarterlyConsumption(lastRecordedDay, lastRecordedQuarter, consumptionWh);
                quarterChanged = true;
            }
            
            // Reset pro novou čtvrthodinu
            currentQuarterAccumulator = 0;
            currentQuarterSampleCount = 0;
            lastRecordedQuarter = quarter;
            lastRecordedDay = day;
        }
        
        // Akumulujeme výkon
        currentQuarterAccumulator += loadPowerW;
        currentQuarterSampleCount++;
        
        return quarterChanged;
    }
    
    /**
     * Vrátí číslo týdne v roce
     */
    static int getWeekOfYear(struct tm* timeinfo) {
        // Jednoduchý výpočet - den v roce / 7
        return timeinfo->tm_yday / 7;
    }
    
    /**
     * Posune historii týdnů - aktuální týden se stane minulým
     */
    void shiftWeekHistory() {
        log_d("Shifting week history");
        // Posuneme týden 0 na pozici 1 (starší data se ztratí)
        for (int day = 0; day < DAYS_PER_WEEK; day++) {
            for (int quarter = 0; quarter < QUARTERS_OF_DAY; quarter++) {
                consumptionAt(1, day, quarter) = consumptionAt(0, day, quarter);
                hasDataAt(1, day, quarter) = hasDataAt(0, day, quarter);
                // Reset aktuálního týdne
                consumptionAt(0, day, quarter) = getDefaultConsumption(quarter / 4);
                hasDataAt(0, day, quarter) = false;
            }
        }
    }
    
    /**
     * Aktualizuje spotřebu pro danou čtvrthodinu aktuálního týdne
     * a propaguje hodnotu i na ostatní dny (pro opakující se vzorce)
     */
    void updateQuarterlyConsumption(int day, int quarter, float consumptionWh) {
        if (day < 0 || day >= DAYS_PER_WEEK || quarter < 0 || quarter >= QUARTERS_OF_DAY) {
            return;
        }
        
        // === AKTUALIZACE ADAPTIVNÍ KOREKCE ===
        updateCorrectionFactor(day, quarter, consumptionWh);
        
        // === PRIMÁRNÍ AKTUALIZACE (aktuální den) ===
        consumptionAt(0, day, quarter) = consumptionWh;
        hasDataAt(0, day, quarter) = true;
        
        // === CROSS-DAY PROPAGACE ===
        // Propagujeme hodnotu na ostatní dny s menší vahou
        // To zrychlí učení opakujících se vzorců (např. noční topení každý den)
        propagateToOtherDays(day, quarter, consumptionWh);
        
        // === PROPAGACE NA SOUSEDNÍ ČTVRTHODINY ===
        // Spotřeba často koreluje se sousedními čtvrthodinami
        propagateToNeighboringQuarters(day, quarter, consumptionWh);
        
        // === PROPAGACE NA MINULÝ TÝDEN ===
        // Pomáhá rychleji vytvořit historii pro predikce
        propagateToLastWeek(day, quarter, consumptionWh);
        
        log_d("Updated consumption for day %d, quarter %d: %.1f Wh (correction: %.1f)", 
              day, quarter, consumptionWh, cumulativeError);
    }
    
    /**
     * Propaguje hodnotu na ostatní dny v týdnu s menší vahou.
     * Používá EMA (exponenciální klouzavý průměr) pro plynulou aktualizaci.
     * 
     * Pravidla:
     * - Hodnota se propaguje jen na dny, které už mají data pro danou čtvrthodinu
     * - Použije se menší váha (CROSS_DAY_ALPHA) pro zachování specifik jednotlivých dnů
     * - Pomáhá rychle zachytit opakující se vzorce (noční topení, ranní spotřeba, atd.)
     */
    void propagateToOtherDays(int sourceDay, int quarter, float consumptionWh) {
        for (int otherDay = 0; otherDay < DAYS_PER_WEEK; otherDay++) {
            if (otherDay == sourceDay) continue;  // Přeskočíme zdrojový den
            
            // Propagujeme jen pokud už máme nějaká data pro tento den a čtvrthodinu
            // (nechceme vytvářet "falešná" data pro dny, které jsme ještě neviděli)
            if (hasDataAt(0, otherDay, quarter)) {
                float oldValue = consumptionAt(0, otherDay, quarter);
                // EMA: nová = α * aktuální + (1-α) * stará
                float newValue = CROSS_DAY_ALPHA * consumptionWh + (1 - CROSS_DAY_ALPHA) * oldValue;
                consumptionAt(0, otherDay, quarter) = newValue;
                
                log_d("Cross-day propagation: day %d -> day %d, quarter %d: %.1f -> %.1f Wh",
                      sourceDay, otherDay, quarter, oldValue, newValue);
            }
        }
    }
    
    /**
     * Propaguje hodnotu na sousední čtvrthodiny s klesající vahou.
     * Spotřeba v jedné čtvrthodině často koreluje se sousedními čtvrthodinami.
     * Váha klesá exponenciálně s vzdáleností.
     * 
     * @param day Den v týdnu
     * @param centerQuarter Centrální čtvrthodina (kde máme měření)
     * @param consumptionWh Naměřená spotřeba
     */
    void propagateToNeighboringQuarters(int day, int centerQuarter, float consumptionWh) {
        for (int offset = -NEIGHBOR_QUARTER_RANGE; offset <= NEIGHBOR_QUARTER_RANGE; offset++) {
            if (offset == 0) continue;  // Přeskočíme centrum
            
            int targetQuarter = centerQuarter + offset;
            if (targetQuarter < 0 || targetQuarter >= QUARTERS_OF_DAY) continue;
            
            // Váha klesá s vzdáleností: alpha / (1 + |offset|)
            float distance = abs(offset);
            float alpha = NEIGHBOR_QUARTER_ALPHA / (1.0f + distance);
            
            // Propagujeme na aktuální den
            if (hasDataAt(0, day, targetQuarter)) {
                float oldValue = consumptionAt(0, day, targetQuarter);
                float newValue = alpha * consumptionWh + (1.0f - alpha) * oldValue;
                consumptionAt(0, day, targetQuarter) = newValue;
                
                log_d("Neighbor propagation: q%d -> q%d (±%d), alpha=%.2f: %.1f -> %.1f Wh",
                      centerQuarter, targetQuarter, offset, alpha, oldValue, newValue);
            }
        }
    }
    
    /**
     * Propaguje hodnotu na stejnou čtvrthodinu minulý týden.
     * Pomáhá rychleji vytvořit "historii" pro predikce.
     * Pokud data pro minulý týden neexistují, vytvoří je.
     */
    void propagateToLastWeek(int day, int quarter, float consumptionWh) {
        // Týden 1 = minulý týden
        if (hasDataAt(1, day, quarter)) {
            float oldValue = consumptionAt(1, day, quarter);
            float newValue = LAST_WEEK_ALPHA * consumptionWh + (1.0f - LAST_WEEK_ALPHA) * oldValue;
            consumptionAt(1, day, quarter) = newValue;
            
            log_d("Last week propagation: week 0 -> week 1, day %d, q%d: %.1f -> %.1f Wh",
                  day, quarter, oldValue, newValue);
        } else {
            // Pokud data pro minulý týden neexistují, vytvoříme je
            consumptionAt(1, day, quarter) = consumptionWh;
            hasDataAt(1, day, quarter) = true;
            
            log_d("Last week created: day %d, q%d: %.1f Wh", day, quarter, consumptionWh);
        }
    }
    
    /**
     * Aktualizuje korekční faktor na základě rozdílu skutečné a predikované spotřeby
     * Používá exponenciální vyhlazování s α=0.2 (pomalejší než výroba)
     */
    void updateCorrectionFactor(int day, int quarter, float actualWh) {
        // Získáme základní predikci (bez korekce)
        float basePrediction = getBasePrediction(day, quarter);
        
        // Spočítáme chybu
        float error = actualWh - basePrediction;
        
        // Exponenciální vyhlazování chyby
        cumulativeError = CORRECTION_ALPHA * error + (1 - CORRECTION_ALPHA) * cumulativeError;
        
        // Reset korekce o půlnoci
        if (quarter < lastCorrectionQuarter) {
            cumulativeError = 0;
            log_d("Consumption correction reset at midnight");
        }
        lastCorrectionQuarter = quarter;
        
        log_d("Consumption correction updated: actual=%.1f, predicted=%.1f, error=%.1f, cumError=%.1f",
              actualWh, basePrediction, error, cumulativeError);
    }
    
    /**
     * Vrátí základní predikci BEZ korekce (pro výpočet chyby)
     */
    float getBasePrediction(int day, int quarter) const {
        if (day < 0 || day >= DAYS_PER_WEEK || quarter < 0 || quarter >= QUARTERS_OF_DAY) {
            return getDefaultConsumption(quarter / 4);
        }
        
        float sum = 0;
        int count = 0;
        
        for (int week = 0; week < WEEKS_HISTORY; week++) {
            if (hasDataAt(week, day, quarter)) {
                sum += consumptionAt(week, day, quarter);
                count++;
            }
        }
        
        if (count == 0) {
            return getDefaultConsumption(quarter / 4);
        }
        
        return sum / count;
    }
    
    /**
     * Získá aktuální čtvrthodinu (0-95)
     */
    int getCurrentQuarter() const {
        time_t now = time(nullptr);
        struct tm* timeinfo = localtime(&now);
        return (timeinfo->tm_hour * 60 + timeinfo->tm_min) / 15;
    }
    
    /**
     * Predikuje spotřebu pro danou čtvrthodinu S KOREKCÍ
     * Korekce klesá exponenciálně se vzdáleností od aktuální čtvrthodiny.
     * Příklad: nabíjíš auto teď → predikce za 1h je vysoká, za 6h už normální.
     * 
     * @param day den v týdnu (0=neděle)
     * @param quarter čtvrthodina (0-95)
     * @return predikovaná spotřeba v Wh za čtvrthodinu
     */
    float predictQuarterlyConsumption(int day, int quarter) const {
        float basePrediction = getBasePrediction(day, quarter);
        
        // === VZDÁLENOSTNÍ KOREKCE ===
        // Chyba se propaguje s klesající vahou podle vzdálenosti od aktuální čtvrthodiny
        // Poločas rozpadu ~6 hodin (24 quarters) - za 6h je korekce na 37%
        int currentQ = getCurrentQuarter();
        int distance = quarter - currentQ;
        if (distance < 0) distance += QUARTERS_OF_DAY;  // Wrap around pro příští den
        
        // Exponenciální pokles: e^(-distance/24)
        // distance=0: 100%, distance=4 (1h): 85%, distance=24 (6h): 37%, distance=48 (12h): 14%
        float decayFactor = exp(-distance / 24.0f);
        
        // Aplikujeme historickou korekci s klesající vahou
        float corrected = basePrediction + (cumulativeError * decayFactor);
        
        // === REALTIME KOREKCE ===
        // Pokud aktuální výkon je výrazně vyšší než predikce, použijeme aktuální výkon
        // Toto zajistí rychlou reakci při spuštění velké zátěže (např. nabíječka EV)
        float currentConsumptionWh = currentPowerW / 4.0f;  // Převod W na Wh za čtvrthodinu
        
        // Pokud aktuální spotřeba je vyšší než korigovaná predikce, použijeme mix
        if (currentConsumptionWh > corrected && distance < 48) {  // Max 12 hodin dopředu
            // Realtime faktor také klesá se vzdáleností, ale rychleji
            // Poločas ~3 hodiny (12 quarters)
            float realtimeFactor = exp(-distance / 12.0f) * 0.8f;  // Max 80% pro aktuální quarter
            
            // Mix historické korekce a realtime
            corrected = (1.0f - realtimeFactor) * corrected + realtimeFactor * currentConsumptionWh;
        }
        
        // Omezení: min 0.1× základní predikce, max 10× (pro EV nabíječky apod.)
        float minCorrection = basePrediction * 0.1f;
        float maxCorrection = basePrediction * 10.0f;
        corrected = constrain(corrected, minCorrection, maxCorrection);
        
        return corrected;
    }
    
    /**
     * Vrátí aktuální výkon pro debug/UI
     */
    float getCurrentPowerW() const {
        return currentPowerW;
    }
    
    /**
     * Vrátí aktuální korekční chybu (pro debug/UI)
     */
    float getCorrectionError() const {
        return cumulativeError;
    }
    
    /**
     * Resetuje korekci
     */
    void resetCorrection() {
        cumulativeError = 0;
        lastCorrectionQuarter = -1;
        currentPowerW = 0;
    }
    
    /**
     * Predikuje spotřebu pro danou hodinu (součet 4 čtvrthodin)
     * @param day den v týdnu (0=neděle)
     * @param hour hodina (0-23)
     * @return predikovaná spotřeba v Wh
     */
    float predictHourlyConsumption(int day, int hour) const {
        if (hour < 0 || hour >= 24) {
            return getDefaultConsumption(hour) * 4;  // 4 čtvrthodiny
        }
        
        float total = 0;
        int startQuarter = hour * 4;
        for (int q = startQuarter; q < startQuarter + 4; q++) {
            total += predictQuarterlyConsumption(day, q);
        }
        return total;
    }
    
    /**
     * Predikuje spotřebu pro zbytek dne od dané čtvrthodiny
     * @param fromQuarter od které čtvrthodiny (0-95, včetně), -1 pro aktuální
     * @param day den v týdnu (0=neděle), -1 pro aktuální den
     * @return predikovaná spotřeba v kWh
     */
    float predictRemainingDayConsumption(int fromQuarter = -1, int day = -1) const {
        if (fromQuarter < 0 || day < 0) {
            time_t now = time(nullptr);
            struct tm* timeinfo = localtime(&now);
            if (fromQuarter < 0) fromQuarter = (timeinfo->tm_hour * 60 + timeinfo->tm_min) / 15;
            if (day < 0) day = timeinfo->tm_wday;
        }
        
        // Validace rozsahu
        if (fromQuarter < 0) fromQuarter = 0;
        if (fromQuarter >= QUARTERS_OF_DAY) return 0;
        
        float totalWh = 0;
        for (int quarter = fromQuarter; quarter < QUARTERS_OF_DAY; quarter++) {
            totalWh += predictQuarterlyConsumption(day, quarter);
        }
        
        return totalWh / 1000.0f;  // Převod na kWh
    }
    
    /**
     * Predikuje spotřebu pro dalších N hodin
     * @param hours počet hodin
     * @return predikovaná spotřeba v kWh
     */
    float predictNextHoursConsumption(int hours) const {
        time_t now = time(nullptr);
        struct tm* timeinfo = localtime(&now);
        int currentQuarter = (timeinfo->tm_hour * 60 + timeinfo->tm_min) / 15;
        int currentDay = timeinfo->tm_wday;
        
        float totalWh = 0;
        int quartersToPredict = hours * 4;
        
        for (int i = 0; i < quartersToPredict; i++) {
            int quarter = (currentQuarter + i) % QUARTERS_OF_DAY;
            int day = (currentDay + (currentQuarter + i) / QUARTERS_OF_DAY) % DAYS_PER_WEEK;
            totalWh += predictQuarterlyConsumption(day, quarter);
        }
        
        return totalWh / 1000.0f;  // Převod na kWh
    }
    
    /**
     * Vrátí predikovanou spotřebu pro čtvrthodinu v kWh
     */
    float predictQuarterConsumptionKwh(int day, int quarter) const {
        return predictQuarterlyConsumption(day, quarter) / 1000.0f;
    }
    
    /**
     * Vymaže všechna naučená data a reset na výchozí hodnoty
     * Používá se při RESET tlačítku v UI
     */
    void clearAllData() {
        log_i("Clearing all consumption prediction data");
        
        // Reset všech dat na výchozí hodnoty
        for (int week = 0; week < WEEKS_HISTORY; week++) {
            for (int day = 0; day < DAYS_PER_WEEK; day++) {
                for (int quarter = 0; quarter < QUARTERS_OF_DAY; quarter++) {
                    int hour = quarter / 4;
                    consumptionAt(week, day, quarter) = getDefaultConsumption(hour);
                    hasDataAt(week, day, quarter) = false;
                }
            }
        }
        
        // Reset korekce a stavu
        resetCorrection();
        lastRecordedWeek = -1;
        lastRecordedDay = -1;
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
                log_d("Consumption prediction NVS data cleared");
            }
        }
    }
    
    /**
     * Uloží historii do NVS
     */
    void saveToPreferences() {
        // NVSGuard is expected to be held by caller (app.cpp)
        // This allows batching multiple saves under one lock
        
        Preferences preferences;
        if (preferences.begin(NAMESPACE, false)) {
            // Komprimovaná data: uint16_t místo float (úspora 50%)
            // hasData jako bitfield (12 bytes místo 96)
            uint16_t compressedData[QUARTERS_OF_DAY];
            uint8_t hasDataBits[12];  // 96 bitů = 12 bytes
            
            for (int week = 0; week < WEEKS_HISTORY; week++) {
                for (int day = 0; day < DAYS_PER_WEEK; day++) {
                    // Komprimace consumption na uint16_t
                    for (int q = 0; q < QUARTERS_OF_DAY; q++) {
                        float val = consumptionAt(week, day, q);
                        compressedData[q] = (uint16_t)constrain(val, 0.0f, 65535.0f);
                    }
                    
                    // Komprimace hasData na bitfield
                    memset(hasDataBits, 0, sizeof(hasDataBits));
                    for (int q = 0; q < QUARTERS_OF_DAY; q++) {
                        if (hasDataAt(week, day, q)) {
                            hasDataBits[q / 8] |= (1 << (q % 8));
                        }
                    }
                    
                    char key[16];
                    snprintf(key, sizeof(key), "c%d%d", week, day);
                    preferences.putBytes(key, compressedData, sizeof(compressedData));
                    
                    snprintf(key, sizeof(key), "h%d%d", week, day);
                    preferences.putBytes(key, hasDataBits, sizeof(hasDataBits));
                }
            }
            preferences.putInt("lastWeek", lastRecordedWeek);
            preferences.end();
            log_d("Consumption history saved (compressed)");
        }
    }
    
    /**
     * Načte historii z NVS
     */
    void loadFromPreferences() {
        NVSGuard guard;
        if (!guard.isLocked()) {
            log_e("Failed to lock NVS mutex for loading consumption history");
            return;
        }
        
        Preferences preferences;
        if (preferences.begin(NAMESPACE, true)) {
            uint16_t compressedData[QUARTERS_OF_DAY];
            uint8_t hasDataBits[12];
            
            for (int week = 0; week < WEEKS_HISTORY; week++) {
                for (int day = 0; day < DAYS_PER_WEEK; day++) {
                    char key[16];
                    
                    // Načtení komprimovaných dat
                    snprintf(key, sizeof(key), "c%d%d", week, day);
                    if (preferences.getBytes(key, compressedData, sizeof(compressedData)) > 0) {
                        for (int q = 0; q < QUARTERS_OF_DAY; q++) {
                            consumptionAt(week, day, q) = (float)compressedData[q];
                        }
                    } else {
                        for (int quarter = 0; quarter < QUARTERS_OF_DAY; quarter++) {
                            consumptionAt(week, day, quarter) = getDefaultConsumption(quarter / 4);
                        }
                    }
                    
                    // Načtení hasData bitfield
                    snprintf(key, sizeof(key), "h%d%d", week, day);
                    if (preferences.getBytes(key, hasDataBits, sizeof(hasDataBits)) > 0) {
                        for (int q = 0; q < QUARTERS_OF_DAY; q++) {
                            hasDataAt(week, day, q) = (hasDataBits[q / 8] & (1 << (q % 8))) != 0;
                        }
                    } else {
                        for (int quarter = 0; quarter < QUARTERS_OF_DAY; quarter++) {
                            hasDataAt(week, day, quarter) = false;
                        }
                    }
                }
            }
            lastRecordedWeek = preferences.getInt("lastWeek", -1);
            preferences.end();
            log_d("Consumption history loaded, lastWeek=%d", lastRecordedWeek);
        }
    }
    
    /**
     * Vrátí průměrnou denní spotřebu v kWh
     */
    float getAverageDailyConsumption() const {
        float total = 0;
        int count = 0;
        
        for (int day = 0; day < DAYS_PER_WEEK; day++) {
            float dayTotal = 0;
            bool hasAnyData = false;
            
            for (int quarter = 0; quarter < QUARTERS_OF_DAY; quarter++) {
                float quarterValue = predictQuarterlyConsumption(day, quarter);
                if (hasDataAt(0, day, quarter) || hasDataAt(1, day, quarter)) {
                    hasAnyData = true;
                }
                dayTotal += quarterValue;
            }
            
            if (hasAnyData) {
                total += dayTotal;
                count++;
            }
        }
        
        if (count == 0) {
            // Výchozí hodnota pokud nemáme žádná data
            float defaultTotal = 0;
            for (int quarter = 0; quarter < QUARTERS_OF_DAY; quarter++) {
                defaultTotal += getDefaultConsumption(quarter / 4);
            }
            return defaultTotal / 1000.0f;
        }
        
        return (total / count) / 1000.0f;
    }
    
    /**
     * Vrátí celkový počet čtvrthodin s daty
     */
    int getTotalSampleCount() const {
        int total = 0;
        for (int week = 0; week < WEEKS_HISTORY; week++) {
            for (int day = 0; day < DAYS_PER_WEEK; day++) {
                for (int quarter = 0; quarter < QUARTERS_OF_DAY; quarter++) {
                    if (hasDataAt(week, day, quarter)) {
                        total++;
                    }
                }
            }
        }
        return total;
    }
    
    /**
     * Kontroluje, zda máme dostatek dat pro spolehlivou predikci
     * Vyžaduje alespoň 1 týden dat pro každý den
     */
    bool hasReliableData() const {
        for (int day = 0; day < DAYS_PER_WEEK; day++) {
            bool hasDayData = false;
            for (int quarter = 0; quarter < QUARTERS_OF_DAY; quarter++) {
                if (hasDataAt(0, day, quarter) || hasDataAt(1, day, quarter)) {
                    hasDayData = true;
                    break;
                }
            }
            if (!hasDayData) {
                return false;
            }
        }
        return true;
    }
    
    /**
     * Vrátí zda máme data pro konkrétní den a čtvrthodinu
     */
    bool hasDataForQuarter(int day, int quarter) const {
        if (day < 0 || day >= DAYS_PER_WEEK || quarter < 0 || quarter >= QUARTERS_OF_DAY) {
            return false;
        }
        return hasDataAt(0, day, quarter) || hasDataAt(1, day, quarter);
    }
};

// Definice static členské proměnné
const char* ConsumptionPredictor::NAMESPACE = "conspred";
