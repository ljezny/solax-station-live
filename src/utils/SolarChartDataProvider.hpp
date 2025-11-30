#pragma once

#include <Arduino.h>
#include <esp_heap_caps.h>
#include "../Spot/ElectricityPriceResult.hpp"  // Pro QUARTERS_OF_DAY

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
#define CHART_SAMPLES_PER_DAY CHART_QUARTERS_PER_DAY  // Pro zpětnou kompatibilitu

/**
 * Provider dat pro solar chart
 * 
 * Ukládá data od 00:00 do 23:59 aktuálního dne.
 * Pro čtvrthodiny, které ještě nenastaly, může obsahovat predikci.
 */
class SolarChartDataProvider
{
private:
    // Data pro aktuální den - alokováno v PSRAM
    SolarChartDataItem_t* todayData;
    
    // Akumulátory pro aktuální čtvrthodinu
    float pvAccumulator = 0;
    float loadAccumulator = 0;
    int socAccumulator = 0;
    int currentSampleCount = 0;
    int lastRecordedQuarter = -1;
    int lastRecordedDay = -1;
    
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
     * Resetuje data pro nový den
     */
    void resetForNewDay() {
        for (int i = 0; i < CHART_QUARTERS_PER_DAY; i++) {
            todayData[i] = SolarChartDataItem_t();
        }
        pvAccumulator = 0;
        loadAccumulator = 0;
        socAccumulator = 0;
        currentSampleCount = 0;
        lastRecordedQuarter = -1;
    }

public:
    SolarChartDataProvider() {
        // Alokace v PSRAM
        todayData = (SolarChartDataItem_t*)heap_caps_malloc(
            CHART_QUARTERS_PER_DAY * sizeof(SolarChartDataItem_t), MALLOC_CAP_SPIRAM);
        
        if (!todayData) {
            log_e("Failed to allocate SolarChartDataProvider in PSRAM!");
            todayData = (SolarChartDataItem_t*)malloc(CHART_QUARTERS_PER_DAY * sizeof(SolarChartDataItem_t));
        } else {
            log_d("SolarChartDataProvider allocated in PSRAM (%d bytes)", 
                  CHART_QUARTERS_PER_DAY * sizeof(SolarChartDataItem_t));
        }
        
        // Inicializace
        if (todayData) {
            for (int i = 0; i < CHART_QUARTERS_PER_DAY; i++) {
                todayData[i] = SolarChartDataItem_t();
            }
        }
        lastRecordedDay = -1;
    }
    
    ~SolarChartDataProvider() {
        if (todayData) heap_caps_free(todayData);
    }
    
    /**
     * Přidá vzorek aktuálních hodnot
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
                todayData[lastRecordedQuarter].pvPowerWh = (pvAccumulator / currentSampleCount) * 0.25f;
                todayData[lastRecordedQuarter].loadPowerWh = (loadAccumulator / currentSampleCount) * 0.25f;
                todayData[lastRecordedQuarter].soc = socAccumulator / currentSampleCount;
                todayData[lastRecordedQuarter].samples = currentSampleCount;
                todayData[lastRecordedQuarter].isPrediction = false;
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
            todayData[currentQuarter].pvPowerWh = (pvAccumulator / currentSampleCount) * 0.25f;
            todayData[currentQuarter].loadPowerWh = (loadAccumulator / currentSampleCount) * 0.25f;
            todayData[currentQuarter].soc = socAccumulator / currentSampleCount;
            todayData[currentQuarter].samples = currentSampleCount;
            todayData[currentQuarter].isPrediction = false;
        }
    }
    
    /**
     * Nastaví predikci pro budoucí čtvrthodiny
     * @param quarter čtvrthodina (0-95)
     * @param pvPowerWh predikovaná výroba v Wh
     * @param loadPowerWh predikovaná spotřeba v Wh
     * @param soc predikovaný SOC (%)
     */
    void setPrediction(int quarter, float pvPowerWh, float loadPowerWh, int soc) {
        if (quarter >= 0 && quarter < CHART_QUARTERS_PER_DAY && todayData) {
            // Nepřepisujeme reálná data
            if (todayData[quarter].samples == 0 || todayData[quarter].isPrediction) {
                todayData[quarter].pvPowerWh = pvPowerWh;
                todayData[quarter].loadPowerWh = loadPowerWh;
                todayData[quarter].soc = soc;
                todayData[quarter].samples = 1;
                todayData[quarter].isPrediction = true;
            }
        }
    }
    
    /**
     * Vymaže predikce (volá se před novým generováním)
     */
    void clearPredictions() {
        if (!todayData) return;
        for (int i = 0; i < CHART_QUARTERS_PER_DAY; i++) {
            if (todayData[i].isPrediction) {
                todayData[i] = SolarChartDataItem_t();
            }
        }
    }
    
    /**
     * Vrací data pro danou čtvrthodinu
     */
    const SolarChartDataItem_t& getQuarter(int quarter) const {
        if (quarter >= 0 && quarter < CHART_QUARTERS_PER_DAY && todayData) {
            return todayData[quarter];
        }
        static SolarChartDataItem_t empty;
        return empty;
    }
    
    /**
     * Vrací pointer na všechna data
     */
    SolarChartDataItem_t* getData() {
        return todayData;
    }
    
    /**
     * Vrací aktuální čtvrthodinu
     */
    int getCurrentQuarterIndex() const {
        return getCurrentQuarter();
    }
    
    /**
     * Vrací maximální výkon v datech (pro škálování grafu)
     * Vrací průměrný výkon W (Wh * 4)
     */
    float getMaxPower() const {
        float maxPower = 5000.0f;  // Minimum pro škálování
        if (!todayData) return maxPower;
        
        for (int i = 0; i < CHART_QUARTERS_PER_DAY; i++) {
            if (todayData[i].samples > 0) {
                // Převod Wh za čtvrthodinu na průměrný výkon W
                float pvPowerW = todayData[i].pvPowerWh * 4.0f;
                float loadPowerW = todayData[i].loadPowerWh * 4.0f;
                maxPower = max(maxPower, max(pvPowerW, loadPowerW));
            }
        }
        return maxPower;
    }
};
