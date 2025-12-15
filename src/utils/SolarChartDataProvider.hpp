#pragma once

#include <Arduino.h>
#include "RemoteLogger.hpp"
#include <Preferences.h>
#include <esp_heap_caps.h>
#include "FlashMutex.hpp"
#include "../Spot/ElectricityPriceResult.hpp"  // Pro QUARTERS_OF_DAY, QUARTERS_TWO_DAYS

/**
 * Struktura pro jeden slot grafu (čtvrthodina)
 */
typedef struct SolarChartDataItem
{
    float pvPowerWh = 0;      // Výroba v Wh za čtvrthodinu (nebo průměrný výkon pro průběžná data)
    float loadPowerWh = 0;    // Spotřeba v Wh za čtvrthodinu
    int soc = 0;              // SOC baterie (%)
    int samples = 0;          // Počet vzorků (0 = prázdný slot)
    bool isPrediction = false; // true = predikce, false = reálná data
} SolarChartDataItem_t;

#define CHART_QUARTERS_PER_DAY QUARTERS_OF_DAY  // 96 čtvrthodin
#define CHART_QUARTERS_TWO_DAYS QUARTERS_TWO_DAYS  // 192 čtvrthodin (2 dny)
#define CHART_SAMPLES_PER_DAY CHART_QUARTERS_PER_DAY  // Pro zpětnou kompatibilitu

/**
 * Provider dat pro solar chart
 * 
 * Ukládá data pro 2 dny:
 * - Index 0-95: dnešní den (00:00 - 23:59)
 * - Index 96-191: zítřejší den (00:00 - 23:59)
 * 
 * Reálná data se ukládají pouze pro dnešek.
 * Pro zítřek jsou pouze predikce.
 */
class SolarChartDataProvider
{
private:
    static const char* NAMESPACE;
    
    // Data pro 2 dny - alokováno v PSRAM
    SolarChartDataItem_t* chartData;
    
    // Akumulátory pro aktuální čtvrthodinu
    float pvAccumulator = 0;
    float loadAccumulator = 0;
    int socAccumulator = 0;
    int currentSampleCount = 0;
    int lastRecordedQuarter = -1;
    int lastRecordedDay = -1;
    int lastSavedQuarter = -1;  // Pro ukládání jen po změně čtvrthodiny
    
    // Příznak dostupnosti zítřejších predikcí
    bool hasTomorrowPredictions = false;
    
    /**
     * Získá aktuální čtvrthodinu (0-95)
     */
    int getCurrentQuarter() const {
        time_t now = time(nullptr);
        struct tm* timeinfo = localtime(&now);
        return (timeinfo->tm_hour * 60 + timeinfo->tm_min) / 15;
    }
    
    /**
     * Získá aktuální den v roce
     */
    int getCurrentDayOfYear() const {
        time_t now = time(nullptr);
        struct tm* timeinfo = localtime(&now);
        return timeinfo->tm_yday;
    }
    
    /**
     * Resetuje data pro nový den - posune zítřejší predikce na dnešek
     */
    void resetForNewDay() {
        // Posun zítřejších predikcí na dnešek
        for (int i = 0; i < CHART_QUARTERS_PER_DAY; i++) {
            chartData[i] = chartData[i + CHART_QUARTERS_PER_DAY];
        }
        
        // Vymazání zítřejších dat
        for (int i = CHART_QUARTERS_PER_DAY; i < CHART_QUARTERS_TWO_DAYS; i++) {
            chartData[i] = SolarChartDataItem_t();
        }
        
        hasTomorrowPredictions = false;
        pvAccumulator = 0;
        loadAccumulator = 0;
        socAccumulator = 0;
        currentSampleCount = 0;
        lastRecordedQuarter = -1;
    }

public:
    SolarChartDataProvider() {
        // Alokace v PSRAM pro 2 dny
        chartData = (SolarChartDataItem_t*)heap_caps_malloc(
            CHART_QUARTERS_TWO_DAYS * sizeof(SolarChartDataItem_t), MALLOC_CAP_SPIRAM);
        
        if (!chartData) {
            LOGE("Failed to allocate SolarChartDataProvider in PSRAM!");
            chartData = (SolarChartDataItem_t*)malloc(CHART_QUARTERS_TWO_DAYS * sizeof(SolarChartDataItem_t));
        } else {
            LOGD("SolarChartDataProvider allocated in PSRAM (%d bytes)", 
                  CHART_QUARTERS_TWO_DAYS * sizeof(SolarChartDataItem_t));
        }
        
        // Inicializace
        if (chartData) {
            for (int i = 0; i < CHART_QUARTERS_TWO_DAYS; i++) {
                chartData[i] = SolarChartDataItem_t();
            }
        }
        lastRecordedDay = -1;
    }
    
    ~SolarChartDataProvider() {
        if (chartData) heap_caps_free(chartData);
    }
    
    /**
     * Přidá vzorek aktuálních hodnot (pouze pro dnešek, index 0-95)
     * @param timestampMS časové razítko (nepoužívá se přímo, zachováno pro kompatibilitu)
     * @param pvPowerW aktuální výkon FVE ve wattech
     * @param loadPowerW aktuální výkon spotřeby ve wattech
     * @param soc stav nabití baterie (%)
     */
    void addSample(long timestampMS, float pvPowerW, float loadPowerW, int soc) {
        int currentQuarter = getCurrentQuarter();
        int currentDay = getCurrentDayOfYear();
        
        // Detekce nového dne
        if (lastRecordedDay >= 0 && currentDay != lastRecordedDay) {
            resetForNewDay();
        }
        lastRecordedDay = currentDay;
        
        // Pokud se změnila čtvrthodina, uložíme průměr za předchozí čtvrthodinu
        if (lastRecordedQuarter != currentQuarter) {
            if (lastRecordedQuarter >= 0 && lastRecordedQuarter < CHART_QUARTERS_PER_DAY && currentSampleCount > 0) {
                // Průměrný výkon * 0.25h = Wh za čtvrthodinu
                chartData[lastRecordedQuarter].pvPowerWh = (pvAccumulator / currentSampleCount) * 0.25f;
                chartData[lastRecordedQuarter].loadPowerWh = (loadAccumulator / currentSampleCount) * 0.25f;
                chartData[lastRecordedQuarter].soc = socAccumulator / currentSampleCount;
                chartData[lastRecordedQuarter].samples = currentSampleCount;
                chartData[lastRecordedQuarter].isPrediction = false;
            }
            
            // Reset akumulátorů
            pvAccumulator = 0;
            loadAccumulator = 0;
            socAccumulator = 0;
            currentSampleCount = 0;
            lastRecordedQuarter = currentQuarter;
        }
        
        // Akumulace
        pvAccumulator += pvPowerW;
        loadAccumulator += loadPowerW;
        socAccumulator += soc;
        currentSampleCount++;
        
        // Aktualizace aktuální čtvrthodiny (průběžná hodnota)
        if (currentQuarter >= 0 && currentQuarter < CHART_QUARTERS_PER_DAY && currentSampleCount > 0) {
            chartData[currentQuarter].pvPowerWh = (pvAccumulator / currentSampleCount) * 0.25f;
            chartData[currentQuarter].loadPowerWh = (loadAccumulator / currentSampleCount) * 0.25f;
            chartData[currentQuarter].soc = socAccumulator / currentSampleCount;
            chartData[currentQuarter].samples = currentSampleCount;
            chartData[currentQuarter].isPrediction = false;
        }
    }
    
    /**
     * Nastaví predikci pro budoucí čtvrthodiny (dnešek i zítřek)
     * @param quarter čtvrthodina (0-191, kde 0-95 je dnešek, 96-191 je zítřek)
     * @param pvPowerWh predikovaná výroba v Wh
     * @param loadPowerWh predikovaná spotřeba v Wh
     * @param soc predikovaný SOC (%)
     */
    void setPrediction(int quarter, float pvPowerWh, float loadPowerWh, int soc) {
        if (quarter >= 0 && quarter < CHART_QUARTERS_TWO_DAYS && chartData) {
            // Nepřepisujeme reálná data
            if (chartData[quarter].samples == 0 || chartData[quarter].isPrediction) {
                chartData[quarter].pvPowerWh = pvPowerWh;
                chartData[quarter].loadPowerWh = loadPowerWh;
                chartData[quarter].soc = soc;
                chartData[quarter].samples = 1;
                chartData[quarter].isPrediction = true;
                
                // Označíme že máme zítřejší predikce
                if (quarter >= CHART_QUARTERS_PER_DAY) {
                    hasTomorrowPredictions = true;
                }
            }
        }
    }
    
    /**
     * Vymaže predikce (volá se před novým generováním)
     * @param includeTomorrow true = vymaže i zítřejší predikce
     */
    void clearPredictions(bool includeTomorrow = true) {
        if (!chartData) return;
        
        int maxQuarter = includeTomorrow ? CHART_QUARTERS_TWO_DAYS : CHART_QUARTERS_PER_DAY;
        for (int i = 0; i < maxQuarter; i++) {
            if (chartData[i].isPrediction) {
                chartData[i] = SolarChartDataItem_t();
            }
        }
        
        if (includeTomorrow) {
            hasTomorrowPredictions = false;
        }
    }
    
    /**
     * Vrací data pro danou čtvrthodinu (0-191)
     */
    const SolarChartDataItem_t& getQuarter(int quarter) const {
        if (quarter >= 0 && quarter < CHART_QUARTERS_TWO_DAYS && chartData) {
            return chartData[quarter];
        }
        static SolarChartDataItem_t empty;
        return empty;
    }
    
    /**
     * Vrací pointer na všechna data (2 dny)
     */
    SolarChartDataItem_t* getData() {
        return chartData;
    }
    
    /**
     * Vrací aktuální čtvrthodinu v rámci dneška (0-95)
     */
    int getCurrentQuarterIndex() const {
        return getCurrentQuarter();
    }
    
    /**
     * Vrací true pokud máme predikce na zítřek
     */
    bool hasTomorrowData() const {
        return hasTomorrowPredictions;
    }
    
    /**
     * Vrací celkový počet čtvrthodin k zobrazení
     */
    int getTotalQuarters() const {
        return hasTomorrowPredictions ? CHART_QUARTERS_TWO_DAYS : CHART_QUARTERS_PER_DAY;
    }
    
    /**
     * Vrací maximální výkon v datech (pro škálování grafu)
     * Vrací průměrný výkon W (Wh * 4)
     */
    float getMaxPower() const {
        float maxPower = 5000.0f;  // Minimum pro škálování
        if (!chartData) return maxPower;
        
        int maxQuarters = hasTomorrowPredictions ? CHART_QUARTERS_TWO_DAYS : CHART_QUARTERS_PER_DAY;
        for (int i = 0; i < maxQuarters; i++) {
            if (chartData[i].samples > 0) {
                // Převod Wh za čtvrthodinu na průměrný výkon W
                float pvPowerW = chartData[i].pvPowerWh * 4.0f;
                float loadPowerW = chartData[i].loadPowerWh * 4.0f;
                maxPower = max(maxPower, max(pvPowerW, loadPowerW));
            }
        }
        return maxPower;
    }
    
    /**
     * Uloží intraday data do NVS (volat jen po změně čtvrthodiny)
     * Ukládá pouze reálná data pro dnešek (ne predikce)
     */
    void saveToPreferences() {
        int currentQuarter = getCurrentQuarter();
        
        // Ukládáme jen když se změnila čtvrthodina
        if (currentQuarter == lastSavedQuarter) {
            return;
        }
        lastSavedQuarter = currentQuarter;
        
        FlashGuard guard;
        if (!guard.isLocked()) {
            LOGE("Failed to lock NVS mutex for saving solar chart data");
            return;
        }
        
        Preferences preferences;
        if (!preferences.begin(NAMESPACE, false)) {
            LOGE("Failed to open NVS for SolarChartData");
            return;
        }
        
        // Uložíme den v roce pro kontrolu při načítání
        preferences.putInt("day", getCurrentDayOfYear());
        
        // Komprimovaná data: uint16_t pro Wh hodnoty, uint8_t pro SOC
        // Ukládáme jen reálná data (ne predikce) pro dnešek
        uint16_t pvData[CHART_QUARTERS_PER_DAY];
        uint16_t loadData[CHART_QUARTERS_PER_DAY];
        uint8_t socData[CHART_QUARTERS_PER_DAY];
        uint8_t hasData[CHART_QUARTERS_PER_DAY];  // Bitová mapa - má slot data?
        
        for (int i = 0; i < CHART_QUARTERS_PER_DAY; i++) {
            if (chartData[i].samples > 0 && !chartData[i].isPrediction) {
                pvData[i] = (uint16_t)constrain(chartData[i].pvPowerWh, 0.0f, 65535.0f);
                loadData[i] = (uint16_t)constrain(chartData[i].loadPowerWh, 0.0f, 65535.0f);
                socData[i] = (uint8_t)constrain(chartData[i].soc, 0, 100);
                hasData[i] = 1;
            } else {
                pvData[i] = 0;
                loadData[i] = 0;
                socData[i] = 0;
                hasData[i] = 0;
            }
        }
        
        preferences.putBytes("pv", pvData, sizeof(pvData));
        preferences.putBytes("load", loadData, sizeof(loadData));
        preferences.putBytes("soc", socData, sizeof(socData));
        preferences.putBytes("has", hasData, sizeof(hasData));
        
        preferences.end();
        LOGD("SolarChartData saved for quarter %d", currentQuarter);
    }
    
    /**
     * Načte intraday data z NVS (volat při startu)
     * Načte pouze pokud je to stejný den
     */
    void loadFromPreferences() {
        FlashGuard guard;
        if (!guard.isLocked()) {
            LOGE("Failed to lock NVS mutex for loading solar chart data");
            return;
        }
        
        Preferences preferences;
        if (!preferences.begin(NAMESPACE, true)) {
            LOGD("No SolarChartData in NVS");
            return;
        }
        
        // Kontrola zda je to stejný den
        int savedDay = preferences.getInt("day", -1);
        int currentDay = getCurrentDayOfYear();
        
        if (savedDay != currentDay) {
            LOGD("SolarChartData from different day (saved: %d, current: %d), ignoring", savedDay, currentDay);
            preferences.end();
            return;
        }
        
        // Načtení dat
        uint16_t pvData[CHART_QUARTERS_PER_DAY];
        uint16_t loadData[CHART_QUARTERS_PER_DAY];
        uint8_t socData[CHART_QUARTERS_PER_DAY];
        uint8_t hasData[CHART_QUARTERS_PER_DAY];
        
        size_t pvLen = preferences.getBytes("pv", pvData, sizeof(pvData));
        size_t loadLen = preferences.getBytes("load", loadData, sizeof(loadData));
        size_t socLen = preferences.getBytes("soc", socData, sizeof(socData));
        size_t hasLen = preferences.getBytes("has", hasData, sizeof(hasData));
        
        preferences.end();
        
        if (pvLen == 0 || loadLen == 0 || socLen == 0 || hasLen == 0) {
            LOGD("SolarChartData incomplete in NVS");
            return;
        }
        
        // Obnovení dat
        int restoredCount = 0;
        for (int i = 0; i < CHART_QUARTERS_PER_DAY; i++) {
            if (hasData[i]) {
                chartData[i].pvPowerWh = (float)pvData[i];
                chartData[i].loadPowerWh = (float)loadData[i];
                chartData[i].soc = socData[i];
                chartData[i].samples = 1;  // Označíme jako platná data
                chartData[i].isPrediction = false;
                restoredCount++;
            }
        }
        
        lastRecordedDay = currentDay;
        LOGI("SolarChartData restored: %d quarters for day %d", restoredCount, currentDay);
    }
};

const char* SolarChartDataProvider::NAMESPACE = "chartdata";
