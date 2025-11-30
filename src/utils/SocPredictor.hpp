#pragma once

#include <Arduino.h>
#include "ConsumptionPredictor.hpp"
#include "ProductionPredictor.hpp"
#include "../Spot/ElectricityPriceResult.hpp"

/**
 * Predikce SOC baterie na základě predikce výroby a spotřeby
 * 
 * Počítá predikovaný SOC pro budoucí čtvrthodiny na základě:
 * - Aktuálního SOC
 * - Predikce výroby (ProductionPredictor)
 * - Predikce spotřeby (ConsumptionPredictor)
 * - Kapacity baterie
 */
class SocPredictor {
private:
    ConsumptionPredictor& consumptionPredictor;
    ProductionPredictor& productionPredictor;
    
public:
    SocPredictor(ConsumptionPredictor& consumption, ProductionPredictor& production)
        : consumptionPredictor(consumption), productionPredictor(production) {}
    
    /**
     * Vypočítá predikci SOC pro všechny čtvrthodiny od aktuální
     * 
     * @param currentSoc aktuální SOC baterie (%)
     * @param batteryCapacityWh kapacita baterie v Wh
     * @param minSoc minimální povolený SOC (%)
     * @param maxSoc maximální povolený SOC (%)
     * @param outSoc pole pro výstup predikovaných SOC hodnot [QUARTERS_OF_DAY]
     * @param isTomorrow true pokud predikujeme pro zítřek (všechny čtvrthodiny jsou "budoucí")
     * @param dayOverride den v týdnu pro predikci spotřeby (-1 = aktuální)
     * @param monthOverride měsíc pro predikci výroby (-1 = aktuální)
     */
    void predictDailySoc(int currentSoc, int batteryCapacityWh, int minSoc, int maxSoc, int* outSoc,
                         bool isTomorrow = false, int dayOverride = -1, int monthOverride = -1) {
        if (!outSoc) return;
        
        // Pokud neznáme kapacitu, použijeme default 10kWh
        if (batteryCapacityWh <= 0) {
            batteryCapacityWh = 10000;
        }
        
        // Aktuální čas
        time_t now = time(nullptr);
        struct tm* timeinfo = localtime(&now);
        int currentQuarter = isTomorrow ? -1 : (timeinfo->tm_hour * 60 + timeinfo->tm_min) / 15;
        int currentDay = (dayOverride >= 0) ? dayOverride : timeinfo->tm_wday;
        int currentMonth = (monthOverride >= 0) ? monthOverride : timeinfo->tm_mon;
        
        // Aktuální energie v baterii
        float batteryEnergyWh = (float)currentSoc / 100.0f * batteryCapacityWh;
        
        // Projdeme všechny čtvrthodiny
        for (int q = 0; q < QUARTERS_OF_DAY; q++) {
            if (q < currentQuarter) {
                // Minulé čtvrthodiny - necháme prázdné (0)
                outSoc[q] = 0;
            } else if (q == currentQuarter) {
                // Aktuální čtvrthodina - aktuální SOC
                outSoc[q] = currentSoc;
            } else {
                // Budoucí čtvrthodiny - predikce
                float productionWh = productionPredictor.predictQuarterlyProduction(currentMonth, q);
                float consumptionWh = consumptionPredictor.predictQuarterlyConsumption(currentDay, q);
                
                // Bilance: výroba - spotřeba
                float energyBalanceWh = productionWh - consumptionWh;
                
                // Přidáme/odebereme energii z baterie
                batteryEnergyWh += energyBalanceWh;
                
                // Omezení na min/max kapacitu
                float minEnergyWh = (float)minSoc / 100.0f * batteryCapacityWh;
                float maxEnergyWh = (float)maxSoc / 100.0f * batteryCapacityWh;
                
                if (batteryEnergyWh < minEnergyWh) {
                    batteryEnergyWh = minEnergyWh;  // Dobíjení ze sítě
                }
                if (batteryEnergyWh > maxEnergyWh) {
                    batteryEnergyWh = maxEnergyWh;  // Prodej přebytku
                }
                
                // Převod na SOC %
                outSoc[q] = (int)(batteryEnergyWh / batteryCapacityWh * 100.0f);
                outSoc[q] = constrain(outSoc[q], 0, 100);
            }
        }
    }
    
    /**
     * Predikuje SOC pro konkrétní čtvrthodinu
     * 
     * @param targetQuarter cílová čtvrthodina
     * @param currentSoc aktuální SOC baterie (%)
     * @param batteryCapacityWh kapacita baterie v Wh
     * @param minSoc minimální povolený SOC (%)
     * @param maxSoc maximální povolený SOC (%)
     * @return predikovaný SOC (%)
     */
    int predictSocForQuarter(int targetQuarter, int currentSoc, int batteryCapacityWh, int minSoc, int maxSoc) {
        if (batteryCapacityWh <= 0) batteryCapacityWh = 10000;
        
        time_t now = time(nullptr);
        struct tm* timeinfo = localtime(&now);
        int currentQuarter = (timeinfo->tm_hour * 60 + timeinfo->tm_min) / 15;
        int currentDay = timeinfo->tm_wday;
        int currentMonth = timeinfo->tm_mon;
        
        if (targetQuarter <= currentQuarter) {
            return currentSoc;
        }
        
        float batteryEnergyWh = (float)currentSoc / 100.0f * batteryCapacityWh;
        float minEnergyWh = (float)minSoc / 100.0f * batteryCapacityWh;
        float maxEnergyWh = (float)maxSoc / 100.0f * batteryCapacityWh;
        
        for (int q = currentQuarter + 1; q <= targetQuarter; q++) {
            float productionWh = productionPredictor.predictQuarterlyProduction(currentMonth, q);
            float consumptionWh = consumptionPredictor.predictQuarterlyConsumption(currentDay, q);
            
            batteryEnergyWh += (productionWh - consumptionWh);
            batteryEnergyWh = constrain(batteryEnergyWh, minEnergyWh, maxEnergyWh);
        }
        
        int soc = (int)(batteryEnergyWh / batteryCapacityWh * 100.0f);
        return constrain(soc, 0, 100);
    }
};
