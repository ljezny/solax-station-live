#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include "../Spot/ElectricityPriceResult.hpp"  // Pro QUARTERS_OF_DAY

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
    static constexpr float CORRECTION_ALPHA = 0.2f;  // Pomalejší adaptace pro spotřebu (špičky)
    int lastCorrectionQuarter;
    
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
     */
    void updateQuarterlyConsumption(int day, int quarter, float consumptionWh) {
        if (day < 0 || day >= DAYS_PER_WEEK || quarter < 0 || quarter >= QUARTERS_OF_DAY) {
            return;
        }
        
        // === AKTUALIZACE ADAPTIVNÍ KOREKCE ===
        updateCorrectionFactor(day, quarter, consumptionWh);
        
        consumptionAt(0, day, quarter) = consumptionWh;
        hasDataAt(0, day, quarter) = true;
        
        log_d("Updated consumption for day %d, quarter %d: %.1f Wh (correction: %.1f)", 
              day, quarter, consumptionWh, cumulativeError);
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
     * Predikuje spotřebu pro danou čtvrthodinu S KOREKCÍ
     * Vrací průměr z dostupných dat pro stejný den v týdnu za poslední 2 týdny
     * + korekce na základě aktuálního vzoru spotřeby
     * @param day den v týdnu (0=neděle)
     * @param quarter čtvrthodina (0-95)
     * @return predikovaná spotřeba v Wh za čtvrthodinu
     */
    float predictQuarterlyConsumption(int day, int quarter) const {
        float basePrediction = getBasePrediction(day, quarter);
        
        // Aplikujeme korekci
        float corrected = basePrediction + cumulativeError;
        
        // Omezení: min 0.3× základní predikce, max 3× základní predikce
        float minCorrection = basePrediction * 0.3f;
        float maxCorrection = basePrediction * 3.0f;
        corrected = constrain(corrected, minCorrection, maxCorrection);
        
        return corrected;
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
     * Vrátí predikci spotřeby pro konkrétní čtvrthodinu v kWh
     */
    float predictQuarterConsumptionKwh(int day, int quarter) const {
        return predictQuarterlyConsumption(day, quarter) / 1000.0f;
    }
    
    /**
     * Uloží historii do NVS
     */
    void saveToPreferences() {
        Preferences preferences;
        if (preferences.begin(NAMESPACE, false)) {
            for (int week = 0; week < WEEKS_HISTORY; week++) {
                for (int day = 0; day < DAYS_PER_WEEK; day++) {
                    char key[16];
                    snprintf(key, sizeof(key), "c_w%dd%d", week, day);
                    // Pointer na začátek řádku v lineárním poli
                    preferences.putBytes(key, &consumptionAt(week, day, 0), QUARTERS_OF_DAY * sizeof(float));
                    
                    snprintf(key, sizeof(key), "h_w%dd%d", week, day);
                    preferences.putBytes(key, &hasDataAt(week, day, 0), QUARTERS_OF_DAY * sizeof(bool));
                }
            }
            preferences.putInt("lastWeek", lastRecordedWeek);
            preferences.end();
            log_d("Consumption history saved");
        }
    }
    
    /**
     * Načte historii z NVS
     */
    void loadFromPreferences() {
        Preferences preferences;
        if (preferences.begin(NAMESPACE, true)) {
            for (int week = 0; week < WEEKS_HISTORY; week++) {
                for (int day = 0; day < DAYS_PER_WEEK; day++) {
                    char key[16];
                    snprintf(key, sizeof(key), "c_w%dd%d", week, day);
                    if (preferences.getBytes(key, &consumptionAt(week, day, 0), QUARTERS_OF_DAY * sizeof(float)) == 0) {
                        // Žádná data - použijeme výchozí
                        for (int quarter = 0; quarter < QUARTERS_OF_DAY; quarter++) {
                            consumptionAt(week, day, quarter) = getDefaultConsumption(quarter / 4);
                        }
                    }
                    
                    snprintf(key, sizeof(key), "h_w%dd%d", week, day);
                    if (preferences.getBytes(key, &hasDataAt(week, day, 0), QUARTERS_OF_DAY * sizeof(bool)) == 0) {
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
