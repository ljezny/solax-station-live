#pragma once

#include <Arduino.h>
#include <cfloat>
#include <vector>
#include "IntelligenceSettings.hpp"
#include "ConsumptionPredictor.hpp"
#include "ProductionPredictor.hpp"
#include "../Inverters/InverterResult.hpp"
#include "../Spot/ElectricityPriceResult.hpp"

/**
 * Výsledek simulace pro jednu čtvrthodinu
 */
struct QuarterSimulationResult {
    int quarter;                    // Index čtvrthodiny (0-191)
    int hour;                       // Hodina (0-23)
    int minute;                     // Minuta (0, 15, 30, 45)
    bool isTomorrow;               // Zda je to zítřejší čtvrthodina
    
    // Energie v kWh za čtvrthodinu
    float productionKwh;           // Predikovaná výroba
    float consumptionKwh;          // Predikovaná spotřeba
    float batteryKwh;              // Energie v baterii (na začátku čtvrthodiny)
    float batterySoc;              // SOC baterie v %
    
    // Toky energie (kladné = do systému, záporné = ze systému)
    float fromSolarKwh;            // Pokrytí ze solární výroby
    float fromBatteryKwh;          // Pokrytí z baterie
    float fromGridKwh;             // Nákup ze sítě
    float toGridKwh;               // Prodej do sítě
    float toBatteryKwh;            // Nabíjení baterie
    
    // Ceny
    float spotPrice;               // Spotová cena
    float buyPrice;                // Nákupní cena
    float sellPrice;               // Prodejní cena
    
    // Ekonomika
    float costCzk;                 // Náklady za tuto čtvrthodinu (kladné = platíme, záporné = vyděláváme)
    float savingsVsGridCzk;        // Úspora oproti čistému nákupu ze sítě
    
    // Rozhodnutí
    InverterMode_t decision;       // Rozhodnutí pro střídač
    String reason;                 // Důvod rozhodnutí
};

/**
 * Celkový výsledek simulace
 */
struct SimulationSummary {
    float totalProductionKwh;
    float totalConsumptionKwh;
    float totalFromGridKwh;
    float totalToGridKwh;
    float totalCostCzk;
    float totalSavingsCzk;        // Úspora oproti hloupému Self-Use (včetně hodnoty baterie)
    float baselineCostCzk;        // Náklady při hloupém Self-Use
    float batteryValueAdjustment; // Hodnota energie navíc v baterii oproti baseline
    float finalBatterySoc;        // Finální SOC inteligentní simulace
    float baselineFinalSoc;       // Finální SOC baseline simulace
    float chargedFromGridKwh;     // Kolik energie bylo nabito ze sítě (pro arbitráž)
    float chargedFromGridCost;    // Kolik to stálo
    float maxBuyPrice;            // Maximální nákupní cena (pro ocenění baterie)
    int quartersSimulated;
};

/**
 * Simulátor inteligentního řízení střídače
 * 
 * Simuluje energetické toky čtvrthodinu po čtvrthodině a rozhoduje
 * o optimálním režimu střídače na základě:
 * - Predikce výroby a spotřeby
 * - Spotových cen elektřiny
 * - Ceny baterie (opotřebení)
 * - Stavu baterie
 * 
 * Podporuje:
 * - Arbitráž (koupit levně, prodat draze)
 * - Odložení nabíjení (čekat na solární výrobu)
 * - Podržení baterie (síť je levnější než baterie)
 * - Optimální čas pro nabíjení/vybíjení
 */
class IntelligenceSimulator {
private:
    // === RELATIVNÍ PRAHY (fungují pro CZK, EUR i jiné měny) ===
    static constexpr float CHEAP_TIME_THRESHOLD = 1.10f;       // 10% tolerance nad minimum pro "levný čas"
    static constexpr float BEST_SELL_TIME_THRESHOLD = 0.80f;   // 80% maxima pro "nejlepší čas prodeje"
    static constexpr float ARBITRAGE_PROFIT_THRESHOLD = 0.15f; // 15% zisk z buy price pro arbitráž
    static constexpr float LOW_BATTERY_SOC = 60.0f;            // SOC pod kterým je baterie "nízká"
    static constexpr float MIN_ENERGY_THRESHOLD = 0.01f;       // Minimální energie pro akci (kWh)
    static constexpr float CONSUMPTION_SAFETY_MARGIN = 1.2f;   // 20% rezerva na spotřebu (pro jistotu)
    static constexpr int LOCAL_WINDOW_QUARTERS = 48;           // Okno pro lokální min/max (12 hodin = 48 čtvrthodin)
    static constexpr float HOLD_MIN_PROFIT_RATIO = 0.15f;      // Minimální profit pro HOLD = 15% z buyPrice (relativní)
    static constexpr float HOLD_PRICE_HYSTERESIS = 0.15f;      // 15% hystereze pro Hold vs Self-Use přepínání
    
    ConsumptionPredictor& consumptionPredictor;
    ProductionPredictor& productionPredictor;
    
    // Parametry baterie
    float batteryCapacityKwh;
    float minSocPercent;
    float maxSocPercent;
    float batteryCostPerKwh;      // Cena za cyklus baterie (opotřebení)
    float maxChargePowerKw;       // Maximální nabíjecí výkon
    float maxDischargePowerKw;    // Maximální vybíjecí výkon
    
    // Aktuální simulovaný stav
    float currentBatteryKwh;
    
    /**
     * Vrátí použitelnou kapacitu baterie
     */
    float getUsableCapacityKwh() const {
        return batteryCapacityKwh * (maxSocPercent - minSocPercent) / 100.0f;
    }
    
    /**
     * Vrátí minimální energii v baterii
     */
    float getMinBatteryKwh() const {
        return batteryCapacityKwh * minSocPercent / 100.0f;
    }
    
    /**
     * Vrátí maximální energii v baterii
     */
    float getMaxBatteryKwh() const {
        return batteryCapacityKwh * maxSocPercent / 100.0f;
    }
    
    /**
     * Převede kWh na SOC%
     */
    float kwhToSoc(float kwh) const {
        return (kwh / batteryCapacityKwh) * 100.0f;
    }
    
    /**
     * Převede SOC% na kWh
     */
    float socToKwh(float soc) const {
        return (soc / 100.0f) * batteryCapacityKwh;
    }
    
    /**
     * Najde nejlevnější nákupní cenu v budoucnosti
     */
    float findMinFutureBuyPrice(const ElectricityPriceTwoDays_t& prices, 
                                 int fromQuarter, int toQuarter,
                                 const IntelligenceSettings_t& settings) {
        float minPrice = FLT_MAX;
        for (int q = fromQuarter; q < toQuarter; q++) {
            float price = IntelligenceSettingsStorage::calculateBuyPrice(
                prices.prices[q].electricityPrice, settings);
            if (price < minPrice) minPrice = price;
        }
        return minPrice;
    }
    
    /**
     * Najde lokální minimum nákupní ceny v okně LOCAL_WINDOW_QUARTERS čtvrthodin
     * Lokální minimum je relevantní pro rozhodování "je teď levný čas?"
     */
    float findLocalMinBuyPrice(const ElectricityPriceTwoDays_t& prices, 
                                int fromQuarter, int toQuarter,
                                const IntelligenceSettings_t& settings) {
        int windowEnd = min(fromQuarter + LOCAL_WINDOW_QUARTERS, toQuarter);
        return findMinFutureBuyPrice(prices, fromQuarter, windowEnd, settings);
    }
    
    /**
     * Najde lokální maximum nákupní ceny v okně LOCAL_WINDOW_QUARTERS čtvrthodin
     * Lokální maximum je relevantní pro rozhodování "vyplatí se nabíjet?"
     */
    float findLocalMaxBuyPrice(const ElectricityPriceTwoDays_t& prices, 
                                int fromQuarter, int toQuarter,
                                const IntelligenceSettings_t& settings) {
        int windowEnd = min(fromQuarter + LOCAL_WINDOW_QUARTERS, toQuarter);
        return findMaxFutureBuyPrice(prices, fromQuarter, windowEnd, settings);
    }
    
    /**
     * Najde nejvyšší nákupní cenu v budoucnosti
     */
    float findMaxFutureBuyPrice(const ElectricityPriceTwoDays_t& prices, 
                                 int fromQuarter, int toQuarter,
                                 const IntelligenceSettings_t& settings) {
        float maxPrice = -FLT_MAX;
        for (int q = fromQuarter; q < toQuarter; q++) {
            float price = IntelligenceSettingsStorage::calculateBuyPrice(
                prices.prices[q].electricityPrice, settings);
            if (price > maxPrice) maxPrice = price;
        }
        return maxPrice;
    }
    
    /**
     * Najde nejvyšší prodejní cenu v budoucnosti
     */
    float findMaxFutureSellPrice(const ElectricityPriceTwoDays_t& prices,
                                  int fromQuarter, int toQuarter,
                                  const IntelligenceSettings_t& settings) {
        float maxPrice = -FLT_MAX;
        for (int q = fromQuarter; q < toQuarter; q++) {
            float price = IntelligenceSettingsStorage::calculateSellPrice(
                prices.prices[q].electricityPrice, settings);
            if (price > maxPrice) maxPrice = price;
        }
        return maxPrice;
    }
    
    /**
     * Najde lokální maximum prodejní ceny v okně LOCAL_WINDOW_QUARTERS čtvrthodin
     */
    float findLocalMaxSellPrice(const ElectricityPriceTwoDays_t& prices,
                                 int fromQuarter, int toQuarter,
                                 const IntelligenceSettings_t& settings) {
        int windowEnd = min(fromQuarter + LOCAL_WINDOW_QUARTERS, toQuarter);
        return findMaxFutureSellPrice(prices, fromQuarter, windowEnd, settings);
    }
    
    /**
     * Spočítá průměrnou budoucí nákupní cenu
     */
    float calculateAvgFutureBuyPrice(const ElectricityPriceTwoDays_t& prices,
                                      int fromQuarter, int toQuarter,
                                      const IntelligenceSettings_t& settings) {
        float sum = 0;
        int count = 0;
        for (int q = fromQuarter; q < toQuarter; q++) {
            sum += IntelligenceSettingsStorage::calculateBuyPrice(
                prices.prices[q].electricityPrice, settings);
            count++;
        }
        return count > 0 ? sum / count : 0;
    }
    
    /**
     * Spočítá lokální průměrnou budoucí nákupní cenu v okně LOCAL_WINDOW_QUARTERS
     */
    float calculateLocalAvgBuyPrice(const ElectricityPriceTwoDays_t& prices,
                                     int fromQuarter, int toQuarter,
                                     const IntelligenceSettings_t& settings) {
        int windowEnd = min(fromQuarter + LOCAL_WINDOW_QUARTERS, toQuarter);
        return calculateAvgFutureBuyPrice(prices, fromQuarter, windowEnd, settings);
    }
    
    /**
     * Spočítá zbytkovou výrobu od dané čtvrthodiny do konce horizontu
     */
    float calculateRemainingProduction(int fromQuarter, int toQuarter, 
                                        int currentDay, int currentMonth) {
        float total = 0;
        for (int q = fromQuarter; q < toQuarter; q++) {
            int dayQ = q % QUARTERS_OF_DAY;
            // Pro zítřek použijeme stejný měsíc (zjednodušení)
            total += productionPredictor.predictQuarterlyProduction(currentMonth, dayQ) / 1000.0f;
        }
        return total;
    }
    
    /**
     * Spočítá zbytkovou spotřebu od dané čtvrthodiny do konce horizontu
     */
    float calculateRemainingConsumption(int fromQuarter, int toQuarter,
                                         int currentDay) {
        float total = 0;
        for (int q = fromQuarter; q < toQuarter; q++) {
            int dayQ = q % QUARTERS_OF_DAY;
            bool isTomorrow = (q >= QUARTERS_OF_DAY);
            int day = isTomorrow ? (currentDay + 1) % 7 : currentDay;
            total += consumptionPredictor.predictQuarterlyConsumption(day, dayQ) / 1000.0f;
        }
        return total;
    }
    
    /**
     * Struktura pro informace o období solární výroby
     */
    struct SolarProductionPeriod {
        int startQuarter;           // První čtvrthodina s výrobou
        int endQuarter;             // Poslední čtvrthodina s výrobou
        int firstSurplusQuarter;    // První čtvrthodina s přebytkem (výroba > spotřeba)
        float totalProductionKwh;   // Celková výroba v období
        float totalConsumptionKwh;  // Celková spotřeba v období
        float surplusKwh;           // Očekávaný přebytek (production - consumption)
        float deficitUntilSurplus;      // Net deficit (consumption - production) do začátku přebytků
        float avgSellPrice;         // Průměrná prodejní cena v období výroby (simple avg)
        float weightedSellPrice;    // Vážený průměr prodejní ceny podle přebytku
        float minSellPrice;         // Minimální prodejní cena v období s přebytkem
        bool isValid;               // Zda bylo nalezeno období výroby
    };
    
    /**
     * Analyzuje budoucí období solární výroby
     * Najde čtvrthodiny s nenulovou výrobou a spočítá očekávaný přebytek
     */
    SolarProductionPeriod analyzeSolarProductionPeriod(
        int fromQuarter, int toQuarter,
        int currentDay, int currentMonth,
        const ElectricityPriceTwoDays_t& prices,
        const IntelligenceSettings_t& settings) {
        
        SolarProductionPeriod result = {0, 0, -1, 0, 0, 0, 0, 0, 0, 999.0f, false};
        
        float sumSellPrice = 0;
        float weightedSumSellPrice = 0;  // Součet (cena * přebytek)
        float totalSurplusWeight = 0;    // Součet vah (přebytků)
        int countWithProduction = 0;
        bool foundStart = false;
        bool foundSustainedSurplus = false;  // Našli jsme trvalý přebytek?
        float cumulativeNet = 0;             // Kumulativní (production - consumption)
        float deficitBeforeSurplus = 0;      // Čistý deficit do začátku přebytků
        
        for (int q = fromQuarter; q < toQuarter; q++) {
            int dayQ = q % QUARTERS_OF_DAY;
            bool isTomorrow = (q >= QUARTERS_OF_DAY);
            int day = isTomorrow ? (currentDay + 1) % 7 : currentDay;
            
            float productionKwh = productionPredictor.predictQuarterlyProduction(currentMonth, dayQ) / 1000.0f;
            float consumptionKwh = consumptionPredictor.predictQuarterlyConsumption(day, dayQ) / 1000.0f;
            
            // Počítáme kumulativní net (production - consumption)
            // Dokud je kumulativní < 0, stále máme deficit a potřebujeme energii v baterii
            // Počítáme pro VŠECHNY čtvrthodiny, ne jen ty s produkcí!
            if (!foundSustainedSurplus) {
                float netThisQuarter = productionKwh - consumptionKwh;
                cumulativeNet += netThisQuarter;
                
                // Deficit je MAXIMUM záporné hodnoty kumulativního net
                // Uložíme nejvyšší deficit, protože to je kolik energie potřebujeme v baterii
                if (cumulativeNet < 0 && -cumulativeNet > deficitBeforeSurplus) {
                    deficitBeforeSurplus = -cumulativeNet;  // Záporné -> kladné
                }
                
                // Debug: loguj deficit každých 20 čtvrthodin
                if (q % 20 == 0 && fromQuarter > 60) {  // Jen pro večerní analýzu
                    log_d("DEFICIT Q%d: prod=%.2f cons=%.2f net=%.2f cumNet=%.2f deficit=%.2f", 
                          q, productionKwh, consumptionKwh, netThisQuarter, cumulativeNet, deficitBeforeSurplus);
                }
            }
            
            // Minimální práh pro "významnou" výrobu (např. 50W = 12.5Wh za čtvrthodinu)
            const float MIN_PRODUCTION_THRESHOLD = 0.0125f;
            
            if (productionKwh > MIN_PRODUCTION_THRESHOLD) {
                if (!foundStart) {
                    result.startQuarter = q;
                    foundStart = true;
                }
                result.endQuarter = q;
                result.totalProductionKwh += productionKwh;
                result.totalConsumptionKwh += consumptionKwh;
                
                float sellPrice = IntelligenceSettingsStorage::calculateSellPrice(
                    prices.prices[q].electricityPrice, settings);
                sumSellPrice += sellPrice;
                countWithProduction++;
                
                // Vážený průměr - váhou je přebytek v dané čtvrthodině
                float quarterSurplus = productionKwh - consumptionKwh;
                if (quarterSurplus > 0) {
                    // Trvalý přebytek = kumulativní net je kladný
                    // To znamená, že od teď solár pokryje spotřebu a ještě zbyde
                    if (!foundSustainedSurplus && cumulativeNet > 0) {
                        result.firstSurplusQuarter = q;
                        result.deficitUntilSurplus = deficitBeforeSurplus;
                        foundSustainedSurplus = true;
                    }
                    
                    weightedSumSellPrice += sellPrice * quarterSurplus;
                    totalSurplusWeight += quarterSurplus;
                    
                    // Sleduj minimální cenu v době přebytku
                    if (sellPrice < result.minSellPrice) {
                        result.minSellPrice = sellPrice;
                    }
                }
            }
        }
        
        if (foundStart && countWithProduction > 0) {
            result.surplusKwh = result.totalProductionKwh - result.totalConsumptionKwh;
            result.avgSellPrice = sumSellPrice / countWithProduction;
            
            // Vážený průměr: ceny vážené podle přebytku v každé čtvrthodině
            // Tím se více váží období s největším přebytkem (obvykle poledne = nízké ceny)
            if (totalSurplusWeight > 0.01f) {
                result.weightedSellPrice = weightedSumSellPrice / totalSurplusWeight;
            } else {
                result.weightedSellPrice = result.avgSellPrice;
            }
            
            // Pokud jsme nenašli žádný přebytek, použij průměr
            if (result.minSellPrice > 900.0f) {
                result.minSellPrice = result.avgSellPrice;
            }
            
            // Pokud jsme nenašli trvalý přebytek, nastav firstSurplusQuarter na konec výroby
            if (!foundSustainedSurplus) {
                result.firstSurplusQuarter = result.endQuarter;
                result.deficitUntilSurplus = deficitBeforeSurplus;
            }
            
            result.isValid = true;
        } else {
            // Žádná výroba - nastavíme bezpečné hodnoty
            result.avgSellPrice = 0.0f;
            result.weightedSellPrice = 0.0f;
            result.minSellPrice = 0.0f;
            result.surplusKwh = 0.0f;
            result.deficitUntilSurplus = 0.0f;
            result.isValid = false;
        }
        
        return result;
    }
    
    /**
     * Spočítá průměrnou prodejní cenu v daném rozsahu čtvrthodin
     */
    float calculateAvgSellPrice(const ElectricityPriceTwoDays_t& prices,
                                int fromQuarter, int toQuarter,
                                const IntelligenceSettings_t& settings) {
        float sum = 0;
        int count = 0;
        for (int q = fromQuarter; q < toQuarter; q++) {
            sum += IntelligenceSettingsStorage::calculateSellPrice(
                prices.prices[q].electricityPrice, settings);
            count++;
        }
        return count > 0 ? sum / count : 0;
    }

public:
    IntelligenceSimulator(ConsumptionPredictor& consumption, ProductionPredictor& production)
        : consumptionPredictor(consumption), productionPredictor(production),
          batteryCapacityKwh(10.0f), minSocPercent(30), maxSocPercent(85),
          batteryCostPerKwh(1.0f), maxChargePowerKw(5.0f), maxDischargePowerKw(5.0f),
          currentBatteryKwh(5.0f) {}
    
    /**
     * Nastaví parametry simulace
     * Hodnoty baterie se berou vždy z nastavení (kam se automaticky aktualizují ze střídače)
     */
    void configure(const InverterData_t& inverterData, const IntelligenceSettings_t& settings,
                   float initialBatteryPrice = -1.0f) {
        // Všechny parametry baterie se berou z nastavení
        // (hodnoty ze střídače se automaticky aktualizují do nastavení při načtení dat)
        batteryCapacityKwh = settings.batteryCapacityKwh;
        minSocPercent = settings.minSocPercent;
        maxSocPercent = settings.maxSocPercent;
        batteryCostPerKwh = settings.batteryCostPerKwh;
        maxChargePowerKw = settings.maxChargePowerKw;
        maxDischargePowerKw = settings.maxDischargePowerKw;
        
        // Inicializace stavu baterie (aktuální SOC ze střídače)
        currentBatteryKwh = socToKwh(inverterData.soc);
    }
    
    /**
     * Hlavní simulační funkce
     * 
     * Simuluje energetické toky pro všechny dostupné čtvrthodiny
     * a vrací pole výsledků.
     * 
     * @param inverterData aktuální stav střídače
     * @param prices spotové ceny
     * @param settings nastavení inteligence
     * @return vektor výsledků simulace
     */
    std::vector<QuarterSimulationResult> simulate(
        const InverterData_t& inverterData,
        const ElectricityPriceTwoDays_t& prices,
        const IntelligenceSettings_t& settings) {
        
        std::vector<QuarterSimulationResult> results;
        
        // Konfigurace
        configure(inverterData, settings);
        
        // Získání aktuálního času
        time_t now = time(nullptr);
        struct tm* timeinfo = localtime(&now);
        int currentQuarter = (timeinfo->tm_hour * 60 + timeinfo->tm_min) / 15;
        int currentDay = timeinfo->tm_wday;
        int currentMonth = timeinfo->tm_mon;
        
        // Počet čtvrthodin k simulaci
        int maxQuarters = prices.hasTomorrowData ? QUARTERS_TWO_DAYS : QUARTERS_OF_DAY;
        int quartersToSimulate = maxQuarters - currentQuarter;
        
        // Reset baterie na aktuální stav
        currentBatteryKwh = socToKwh(inverterData.soc);
        
        // Simulace čtvrthodinu po čtvrthodině
        for (int i = 0; i < quartersToSimulate; i++) {
            int q = currentQuarter + i;  // Absolutní index (0-191)
            int dayQ = q % QUARTERS_OF_DAY;  // Index v rámci dne (0-95)
            bool isTomorrow = (q >= QUARTERS_OF_DAY);
            int simDay = isTomorrow ? (currentDay + 1) % 7 : currentDay;
            int simMonth = currentMonth;  // Zjednodušení
            
            // Predikce pro tuto čtvrthodinu
            float productionWh = productionPredictor.predictQuarterlyProduction(simMonth, dayQ);
            float consumptionWh = consumptionPredictor.predictQuarterlyConsumption(simDay, dayQ);
            float productionKwh = productionWh / 1000.0f;
            float consumptionKwh = consumptionWh / 1000.0f;
            
            // Ceny
            float spotPrice = prices.prices[q].electricityPrice;
            float buyPrice = IntelligenceSettingsStorage::calculateBuyPrice(spotPrice, settings);
            float sellPrice = IntelligenceSettingsStorage::calculateSellPrice(spotPrice, settings);
            
            // Budoucí ceny (pro rozhodování)
            // Globální min/max pro celý horizont
            float minFutureBuyPrice = (q + 1 < maxQuarters) 
                ? findMinFutureBuyPrice(prices, q + 1, maxQuarters, settings) 
                : buyPrice;
            float maxFutureBuyPrice = (q + 1 < maxQuarters)
                ? findMaxFutureBuyPrice(prices, q + 1, maxQuarters, settings)
                : buyPrice;
            float maxFutureSellPrice = (q + 1 < maxQuarters)
                ? findMaxFutureSellPrice(prices, q + 1, maxQuarters, settings)
                : sellPrice;
            float avgFutureBuyPrice = (q + 1 < maxQuarters)
                ? calculateAvgFutureBuyPrice(prices, q + 1, maxQuarters, settings)
                : buyPrice;
            
            // Lokální min/max v okně 12 hodin - pro rozhodování o nabíjení/vybíjení
            // Používáme lokální hodnoty, protože nás zajímá blízká budoucnost
            float localMinBuyPrice = (q + 1 < maxQuarters)
                ? findLocalMinBuyPrice(prices, q + 1, maxQuarters, settings)
                : buyPrice;
            float localMaxBuyPrice = (q + 1 < maxQuarters)
                ? findLocalMaxBuyPrice(prices, q + 1, maxQuarters, settings)
                : buyPrice;
            float localMaxSellPrice = (q + 1 < maxQuarters)
                ? findLocalMaxSellPrice(prices, q + 1, maxQuarters, settings)
                : sellPrice;
            float localAvgBuyPrice = (q + 1 < maxQuarters)
                ? calculateLocalAvgBuyPrice(prices, q + 1, maxQuarters, settings)
                : buyPrice;
            
            // Budoucí energie
            float remainingProduction = (q + 1 < maxQuarters)
                ? calculateRemainingProduction(q + 1, maxQuarters, currentDay, currentMonth)
                : 0;
            float remainingConsumption = (q + 1 < maxQuarters)
                ? calculateRemainingConsumption(q + 1, maxQuarters, currentDay)
                : 0;
            bool expectSolarSurplus = remainingProduction > remainingConsumption;
            
            // Aktuální stav baterie
            float batteryKwhStart = currentBatteryKwh;
            float batterySocStart = kwhToSoc(batteryKwhStart);
            float availableFromBattery = max(0.0f, batteryKwhStart - getMinBatteryKwh());
            float spaceInBattery = max(0.0f, getMaxBatteryKwh() - batteryKwhStart);
            
            // Maximální energie za čtvrthodinu (výkon * 0.25h)
            float maxChargeQuarter = maxChargePowerKw * 0.25f;
            float maxDischargeQuarter = maxDischargePowerKw * 0.25f;
            
            // === ROZHODOVACÍ LOGIKA ===
            QuarterSimulationResult result;
            result.quarter = q;
            result.hour = dayQ / 4;
            result.minute = (dayQ % 4) * 15;
            result.isTomorrow = isTomorrow;
            result.productionKwh = productionKwh;
            result.consumptionKwh = consumptionKwh;
            result.batteryKwh = batteryKwhStart;
            result.batterySoc = batterySocStart;
            result.spotPrice = spotPrice;
            result.buyPrice = buyPrice;
            result.sellPrice = sellPrice;
            
            // Inicializace toků
            result.fromSolarKwh = 0;
            result.fromBatteryKwh = 0;
            result.fromGridKwh = 0;
            result.toGridKwh = 0;
            result.toBatteryKwh = 0;
            result.costCzk = 0;
            result.savingsVsGridCzk = 0;
            
            // Net energie (výroba - spotřeba)
            float netEnergy = productionKwh - consumptionKwh;
            
            // === KROK 1: Pokrytí spotřeby ===
            // Priorita: 1) Solární výroba, 2) Baterie (pokud výhodná), 3) Síť
            
            float remainingConsumptionNow = consumptionKwh;
            
            // 1a) Pokrytí ze solární výroby (zdarma)
            float solarForConsumption = min(productionKwh, remainingConsumptionNow);
            result.fromSolarKwh = solarForConsumption;
            remainingConsumptionNow -= solarForConsumption;
            float solarSurplus = productionKwh - solarForConsumption;
            
            // === ROZHODNUTÍ O REŽIMU ===
            InverterMode_t decision = INVERTER_MODE_SELF_USE;
            String reason = "Self-use";
            
            // Náklad na vybití baterie = 0 (opotřebení se počítá při nabíjení jako full cycle)
            // Tím se zajistí, že batteryCost se počítá jen jednou za cyklus
            float dischargeCost = 0;
            
            // 1b) Zbývá spotřeba - rozhodnutí baterie vs síť
            if (remainingConsumptionNow > 0) {
                // Máme spotřebu k pokrytí
                // Vybíjení baterie se vyplatí když je levnější než síť
                bool batteryBetterThanGrid = dischargeCost < buyPrice;
                
                // HOLD baterii se vyplatí POUZE když splníme VŠECHNY podmínky:
                //
                // 1) Baterie je teď levnější než síť (jinak kupujeme ze sítě beztak)
                // 2) Aktuální cena je NÍZKÁ - pod lokálním průměrem budoucích cen (12h okno)
                //    (v drahé hodině nemá smysl kupovat ze sítě)
                // 3) HOLD se skutečně VYPLATÍ ekonomicky:
                //    - Teď zaplatíme: buyPrice (nákup ze sítě)
                //    - Později ušetříme: localMaxBuyPrice - batteryCost (baterie místo sítě)
                //    - HOLD se vyplatí když: localMaxBuyPrice - batteryCost > buyPrice
                //    - Čili: localMaxBuyPrice > buyPrice + batteryCost
                // 4) Budeme mít reálnou spotřebu v době vysoké ceny (ne jen teoreticky)
                // 5) NOVÝ: Profit musí být dostatečný (> HOLD_MIN_PROFIT_CZK) aby se vyplatilo přepínat
                //
                // Používáme LOKÁLNÍ hodnoty (12h okno), protože:
                // - Nás zajímá nejbližší špička, ne špička za 24 hodin
                // - HOLD na příliš dlouhou dobu není efektivní
                //
                
                // Používáme hysterezi: pro HOLD potřebujeme výrazně nižší cenu než průměr
                bool isCheapNow = buyPrice < localAvgBuyPrice * (1.0f - HOLD_PRICE_HYSTERESIS);
                // Čistý zisk z HOLD = rozdíl cen (batteryCost se počítá jen při nabíjení, ne při vybíjení)
                float holdProfit = localMaxBuyPrice - buyPrice;
                // HOLD se vyplatí jen když profit je dostatečný - relativně k buyPrice (ne jen pár haléřů)
                float minRequiredHoldProfit = buyPrice * HOLD_MIN_PROFIT_RATIO;
                bool holdIsProfitable = holdProfit > minRequiredHoldProfit;
                
                // Kolik energie potřebujeme do budoucna (bez solární výroby)?
                float energyDeficit = max(0.0f, remainingConsumption - remainingProduction);
                bool willNeedBatteryLater = energyDeficit > 0;
                
                // HOLD pouze pokud:
                // - Baterie je teď výhodnější než síť (dischargeCost < buyPrice)
                // - Teď NENÍ vhodná hodina pro nabíjení (jinak bychom měli nabíjet, ne držet)
                // - HOLD je ziskový (ušetříme víc než zaplatíme)
                // - Budeme potřebovat baterii později
                // - Máme co držet
                // - Nemáme místo pro nabíjení NEBO se nevyplatí nabíjet
                // - NOVÝ: Lokální maximum je výrazně vyšší než průměr (skutečná špička)
                //
                // Klíčová změna: HOLD jen pokud by se NEVYPLATILO nabíjet
                // Nabíjení se vyplatí když: jsme blízko minima A storageCost < localMaxBuyPrice
                // DŮLEŽITÉ: Nikdy nenakupujeme ze sítě pokud očekáváme solární nabíjení!
                bool canCharge = spaceInBattery > 0;
                float storageCostForHold = buyPrice + batteryCostPerKwh;
                bool isNearMinimumForHold = buyPrice <= localMinBuyPrice * CHEAP_TIME_THRESHOLD;
                bool chargingWouldBeWorthEconomically = storageCostForHold < localMaxBuyPrice;
                // Nabíjení má přednost jen pokud jsme blízko minima - jinak čekáme na levnější cenu
                // ALE: Nikdy nenakupujeme ze sítě pokud očekáváme solární přebytky
                // Očekáváme solární nabíjení pokud je očekávaný přebytek > 50% místa v baterii
                float expectedSolarSurplus = max(0.0f, remainingProduction - remainingConsumption);
                bool willHaveSolarCharging = expectedSolarSurplus > spaceInBattery * 0.5f;
                bool shouldChargeInsteadOfHold = canCharge && isNearMinimumForHold && 
                                                 chargingWouldBeWorthEconomically && !willHaveSolarCharging;
                
                // Lokální maximum musí být výrazně vyšší než průměr (skutečná cenová špička)
                bool hasRealPeakAhead = localMaxBuyPrice > localAvgBuyPrice * (1.0f + HOLD_PRICE_HYSTERESIS);
                
                bool shouldHoldForLater = batteryBetterThanGrid && 
                                          isCheapNow &&
                                          holdIsProfitable && 
                                          willNeedBatteryLater &&
                                          availableFromBattery > 0 &&
                                          hasRealPeakAhead &&          // Musí být skutečná špička
                                          !shouldChargeInsteadOfHold;  // Nepřeskakuj nabíjení!
                
                if (shouldHoldForLater) {
                    // HOLD baterii a koupit ze sítě - ušetříme baterii na dražší čas
                    decision = INVERTER_MODE_HOLD_BATTERY;
                    reason = String("Hold: buy@") + String(buyPrice, 1) + 
                             " now, use batt@" + String(localMaxBuyPrice, 1) + 
                             " profit:" + String(holdProfit, 1);
                    
                    // Kupujeme ze sítě
                    result.fromGridKwh = remainingConsumptionNow;
                    result.costCzk = remainingConsumptionNow * buyPrice;
                } else if (shouldChargeInsteadOfHold) {
                    // Teď je dobrá doba na nabíjení - nekryjeme spotřebu z baterie, ale ze sítě
                    // a baterii nabijeme v KROK 3
                    result.fromGridKwh = remainingConsumptionNow;
                    result.costCzk = remainingConsumptionNow * buyPrice;
                    reason = String("Grid (charging time): buy@") + String(buyPrice, 1);
                } else if (availableFromBattery >= remainingConsumptionNow && batteryBetterThanGrid) {
                    // Baterie je levnější → použij baterii
                    result.fromBatteryKwh = remainingConsumptionNow;
                    currentBatteryKwh -= remainingConsumptionNow;
                    // Náklad = 0 (opotřebení už bylo započítáno při nabíjení)
                    result.costCzk = 0;
                    reason = String("Battery@") + String(dischargeCost, 1) + 
                             " < grid@" + String(buyPrice, 1);
                } else if (availableFromBattery > 0 && batteryBetterThanGrid) {
                    // Částečně z baterie, zbytek ze sítě
                    result.fromBatteryKwh = min(availableFromBattery, remainingConsumptionNow);
                    currentBatteryKwh -= result.fromBatteryKwh;
                    result.fromGridKwh = remainingConsumptionNow - result.fromBatteryKwh;
                    // Náklad baterie = 0 (opotřebení už bylo započítáno při nabíjení)
                    result.costCzk = result.fromGridKwh * buyPrice;
                    reason = "Partial battery + grid";
                } else {
                    // Vše ze sítě (baterie je dražší nebo prázdná)
                    result.fromGridKwh = remainingConsumptionNow;
                    result.costCzk = remainingConsumptionNow * buyPrice;
                    if (availableFromBattery > 0) {
                        decision = INVERTER_MODE_HOLD_BATTERY;
                        reason = String("Hold: batt@") + String(dischargeCost, 1) + 
                                 " > grid@" + String(buyPrice, 1);
                    } else {
                        reason = "Grid only (battery empty)";
                    }
                }
            }
            
            // === KROK 2: Solární přebytek ===
            if (solarSurplus > 0) {
                // Máme přebytek výroby
                bool canChargeBattery = spaceInBattery > 0;
                bool worthCharging = sellPrice < batteryCostPerKwh + minFutureBuyPrice;
                bool worthSelling = sellPrice > batteryCostPerKwh;
                bool isBestSellTime = sellPrice >= maxFutureSellPrice * BEST_SELL_TIME_THRESHOLD;
                
                if (canChargeBattery && worthCharging) {
                    // Nabíjíme baterii přebytkem
                    float toCharge = min(solarSurplus, min(spaceInBattery, maxChargeQuarter));
                    result.toBatteryKwh = toCharge;
                    currentBatteryKwh += toCharge;
                    result.costCzk += toCharge * batteryCostPerKwh;  // Náklad na opotřebení při nabíjení
                    solarSurplus -= toCharge;
                    reason = "Charging from solar surplus";
                    
                    // Pokud ještě zbývá přebytek, prodáváme
                    if (solarSurplus > 0) {
                        result.toGridKwh = solarSurplus;
                        result.costCzk -= solarSurplus * sellPrice;  // Záporné = výdělek
                    }
                } else if (worthSelling || isBestSellTime) {
                    // Prodáváme do sítě
                    result.toGridKwh = solarSurplus;
                    result.costCzk -= solarSurplus * sellPrice;
                    reason = String("Selling surplus @ ") + String(sellPrice, 1);
                } else {
                    // Nabíjíme baterii (default)
                    float toCharge = min(solarSurplus, min(spaceInBattery, maxChargeQuarter));
                    result.toBatteryKwh = toCharge;
                    currentBatteryKwh += toCharge;
                    result.costCzk += toCharge * batteryCostPerKwh;  // Náklad na opotřebení při nabíjení
                    solarSurplus -= toCharge;
                    if (solarSurplus > 0) {
                        result.toGridKwh = solarSurplus;
                        result.costCzk -= solarSurplus * sellPrice;
                    }
                }
            }
            
            // === KROK 3: Speciální rozhodnutí ===
            
            // 3a) PRE-EMPTIVE DISCHARGE - vybít baterii PŘED solární výrobou
            // Cíl: Uvolnit místo pro solární energii, kterou bychom jinak museli prodat levně
            // Vyplatí se když:
            // 1) Baterie je téměř plná (málo místa pro solární)
            // 2) Očekáváme solární přebytek (více výroby než spotřeby)
            // 3) Aktuální prodejní cena > průměrná cena v době solární výroby + batteryCost
            // 4) Máme dost energie v baterii na pokrytí spotřeby do prvního přebytku
            // Podmínka productionKwh < consumptionKwh zajistí, že vybíjíme dokud výroba nepokryje spotřebu
            if (availableFromBattery > 0 && decision == INVERTER_MODE_SELF_USE && productionKwh < consumptionKwh) {
                // Analyzuj budoucí solární období
                // DŮLEŽITÉ: Pro výpočet deficitu vždy začínáme od konce AKTUÁLNÍ produkce (západu slunce)
                // Pokud jsme večer (produkce > 0), najdi konec produkce a počítej deficit od něj
                // Pokud jsme v noci/ráno (produkce = 0), počítej od aktuálního kvartálu
                
                int deficitStartQuarter = q;
                
                // Najdi konec aktuální produkce (západ slunce)
                // Práh pro "významnou" produkci - pod tímto je tma
                const float SUNSET_THRESHOLD = 0.05f;  // 50Wh za čtvrthodinu = 200W
                
                if (productionKwh > SUNSET_THRESHOLD) {
                    // Jsme ve dne - najdi kdy skončí produkce (západ slunce)
                    for (int searchQ = q; searchQ < maxQuarters; searchQ++) {
                        int dayQ = searchQ % QUARTERS_OF_DAY;
                        float futureProduction = productionPredictor.predictQuarterlyProduction(currentMonth, dayQ) / 1000.0f;
                        if (futureProduction < SUNSET_THRESHOLD) {
                            deficitStartQuarter = searchQ;
                            break;
                        }
                    }
                }
                
                SolarProductionPeriod solarPeriod = analyzeSolarProductionPeriod(
                    deficitStartQuarter, maxQuarters, currentDay, currentMonth, prices, settings);
                
                if (solarPeriod.isValid && solarPeriod.surplusKwh > 0) {
                    // Kolik místa máme v baterii?
                    float currentSpace = spaceInBattery;
                    
                    // Kolik přebytku bychom mohli nabít do baterie?
                    float potentialCharge = min(solarPeriod.surplusKwh, getUsableCapacityKwh());
                    
                    // Kolik přebytku bychom MUSELI prodat, protože nemáme místo?
                    float forcedSell = max(0.0f, potentialCharge - currentSpace);
                    
                    // Pre-emptive discharge se vyplatí když:
                    // 1) Máme přebytek který bychom museli prodat
                    // 2) Teď dostaneme lepší cenu než bychom dostali za solární přebytek
                    // 3) Zisk po odečtení opotřebení baterie je kladný
                    //
                    // Výpočet profitu:
                    // - Teď prodáme za sellPrice
                    // - Později bychom prodali za weightedSellPrice (vážený průměr podle přebytku)
                    //   Tím se více váží období s největším přebytkem (obvykle poledne = nízké ceny)
                    // - BatteryCost NEPOČÍTÁME - nabíjení ze soláru je zdarma, 
                    //   baterie by se stejně nabila, jen prodáme za lepší cenu
                    float preemptiveProfit = sellPrice - solarPeriod.weightedSellPrice;
                    
                    // Minimální profit pro pre-emptive discharge (15% z prodejní ceny)
                    float minPreemptiveProfit = sellPrice * ARBITRAGE_PROFIT_THRESHOLD;
                    
                    // Kolik energie minimálně potřebujeme v baterii na pokrytí deficitu do začátku přebytků?
                    // deficitUntilSurplus = kumulativní (consumption - production) dokud solár nezačne pokrývat
                    // 
                    // DŮLEŽITÉ: Pokud jsme večer (ještě před západem slunce), musíme přičíst
                    // spotřebu od TEĎ do západu slunce, protože analyzeSolarProductionPeriod
                    // počítá deficit až od deficitStartQuarter (západu slunce)
                    float additionalDeficit = 0;
                    if (deficitStartQuarter > q) {
                        // Spočítej spotřebu od teď do západu slunce
                        for (int addQ = q; addQ < deficitStartQuarter; addQ++) {
                            int addDayQ = addQ % QUARTERS_OF_DAY;
                            bool addIsTomorrow = (addQ >= QUARTERS_OF_DAY);
                            int addDay = addIsTomorrow ? (currentDay + 1) % 7 : currentDay;
                            float addProd = productionPredictor.predictQuarterlyProduction(currentMonth, addDayQ) / 1000.0f;
                            float addCons = consumptionPredictor.predictQuarterlyConsumption(addDay, addDayQ) / 1000.0f;
                            // Přidáváme jen deficit (když spotřeba > produkce)
                            if (addCons > addProd) {
                                additionalDeficit += (addCons - addProd);
                            }
                        }
                    }
                    
                    // Celkový deficit = noční deficit + večerní deficit do západu slunce
                    float totalDeficit = solarPeriod.deficitUntilSurplus + additionalDeficit;
                    
                    // Přidáme 10% rezervu pro jistotu
                    float minEnergyNeeded = totalDeficit * 1.1f;
                    float minSocNeeded = (minEnergyNeeded / batteryCapacityKwh) * 100.0f + minSocPercent;
                    
                    // Aktuální SOC
                    float currentSoc = (currentBatteryKwh / batteryCapacityKwh) * 100.0f;
                    
                    // Můžeme vybíjet jen pokud máme dost energie na pokrytí spotřeby
                    bool hasEnoughForConsumption = currentSoc > minSocNeeded;
                    
                    bool worthPreemptive = preemptiveProfit > minPreemptiveProfit && 
                                           preemptiveProfit > 0 &&
                                           forcedSell > 0.5f &&  // Alespoň 0.5 kWh přebytku
                                           hasEnoughForConsumption;  // Dost energie na spotřebu
                    
                    // Debug log
                    if (availableFromBattery > 0.5f) {
                        log_d("Q%d PRE_DISCHARGE? sell=%.2f wghtSolar=%.2f profit=%.2f surplus=%.1f forced=%.1f deficit=%.1f+%.1f=%.1f minSoc=%.0f curSoc=%.0f worth=%d",
                              q, sellPrice, solarPeriod.weightedSellPrice,
                              preemptiveProfit, solarPeriod.surplusKwh, forcedSell, 
                              solarPeriod.deficitUntilSurplus, additionalDeficit, totalDeficit,
                              minSocNeeded, currentSoc, worthPreemptive);
                    }
                    
                    if (worthPreemptive) {
                        decision = INVERTER_MODE_DISCHARGE_TO_GRID;
                        
                        // Vybijeme tolik, abychom uvolnili místo pro solární přebytek
                        // Ale maximálně tolik, kolik máme k dispozici nad minimální potřebnou úroveň
                        float maxDischargeable = max(0.0f, currentBatteryKwh - (minSocNeeded / 100.0f * batteryCapacityKwh));
                        float toDischarge = min(forcedSell, min(maxDischargeable, maxDischargeQuarter));
                        
                        if (toDischarge > MIN_ENERGY_THRESHOLD) {
                            result.toGridKwh += toDischarge;
                            result.fromBatteryKwh += toDischarge;
                            currentBatteryKwh -= toDischarge;
                            result.costCzk -= toDischarge * sellPrice;  // Příjem z prodeje
                            result.costCzk += toDischarge * batteryCostPerKwh;  // Náklad na opotřebení
                            
                            reason = String("PreDisch: sell@") + String(sellPrice, 1) + 
                                     " vs solar@" + String(solarPeriod.avgSellPrice, 1) +
                                     " surplus:" + String(solarPeriod.surplusKwh, 1);
                        }
                    }
                }
            }
            
            // 3b) NABÍJENÍ ZE SÍTĚ - levná elektřina
            // Nabíjíme pokud: je to výhodné pro pozdější spotřebu NEBO pro arbitráž
            // ALE NIKDY nenakupujeme ze sítě pokud očekáváme solární přebytky!
            if (spaceInBattery > 0) {
                // Podmínky pro nabíjení - používáme LOKÁLNÍ min/max (12h okno)
                
                // Je aktuální cena blízko lokálního minima? (do 10% nad minimum)
                // Toto zajistí, že nabíjíme jen v nejlevnějších hodinách
                bool isNearMinimum = buyPrice <= localMinBuyPrice * CHEAP_TIME_THRESHOLD;
                
                // Cena uložení energie = nákup + opotřebení baterie (full cycle cost se počítá jen jednou)
                float storageCost = buyPrice + batteryCostPerKwh;
                
                // Nabíjení se vyplatí ekonomicky když uložená energie bude levnější než LOKÁLNÍ MAXIMUM
                // (v následujících 12 hodinách nastane špička kde energii využijeme)
                bool worthChargingEconomically = storageCost < localMaxBuyPrice;
                
                // Spotřeba s bezpečnostní rezervou (pro jistotu nakoupíme víc)
                float expectedConsumption = remainingConsumption * CONSUMPTION_SAFETY_MARGIN;
                bool willNeedLater = expectedConsumption > remainingProduction;
                
                // Zjisti jestli očekáváme solární přebytky které by nabily baterii zdarma
                // Pokud ano, NENAKUPUJEME ze sítě - počkáme na solární nabíjení
                bool expectSolarCharging = false;
                if (remainingProduction > remainingConsumption) {
                    float expectedSolarSurplus = remainingProduction - remainingConsumption;
                    // Pokud přebytek pokryje alespoň 50% místa v baterii, čekáme na slunce
                    expectSolarCharging = expectedSolarSurplus > spaceInBattery * 0.5f;
                }
                
                // Arbitráž: koupit levně, prodat draze (potřebujeme zisk po započtení nákladů baterie)
                // Profit musí být alespoň X% z nákupní ceny
                // BatteryCost se počítá jen jednou za celý cyklus
                // DŮLEŽITÉ: Arbitráž má smysl JEN když:
                // 1) Neočekáváme solární přebytky (jinak nabijeme zdarma)
                // 2) Jsme blízko cenového minima
                float arbitrageProfit = maxFutureSellPrice - buyPrice - batteryCostPerKwh;
                float minRequiredProfit = buyPrice * ARBITRAGE_PROFIT_THRESHOLD;
                bool worthArbitrage = !expectSolarCharging && 
                                      isNearMinimum &&
                                      arbitrageProfit > minRequiredProfit && 
                                      arbitrageProfit > 0;
                
                // Debug: log charging decision factors
                if (i < 3 || (q >= 96 && q <= 100)) {
                    log_d("Q%d CHARGE? nearMin=%d expectSolar=%d arb=%d (%.2f<=%.2f*1.1)",
                          q, isNearMinimum, expectSolarCharging, worthArbitrage,
                          buyPrice, localMinBuyPrice);
                }
                
                // Nabíjíme pokud:
                // 1) Jsme blízko lokálního minima A vyplatí se nabíjet ekonomicky A budeme potřebovat energii
                // 2) Nebo se vyplatí arbitráž (ta teď vyžaduje: nearMin + !expectSolar + profit)
                bool shouldCharge = (isNearMinimum && worthChargingEconomically && willNeedLater) || worthArbitrage;
                
                if (shouldCharge) {
                    decision = INVERTER_MODE_CHARGE_FROM_GRID;
                    float toCharge = min(spaceInBattery, maxChargeQuarter);
                    result.toBatteryKwh += toCharge;
                    result.fromGridKwh += toCharge;
                    currentBatteryKwh += toCharge;
                    result.costCzk += toCharge * buyPrice;  // Platba za elektřinu
                    result.costCzk += toCharge * batteryCostPerKwh;  // Náklad na opotřebení při nabíjení
                    
                    if (worthArbitrage) {
                        reason = String("Arbitrage: buy@") + String(buyPrice, 1) + 
                                 " sell@" + String(maxFutureSellPrice, 1) +
                                 " profit:" + String(arbitrageProfit / buyPrice * 100, 0) + "%";
                    } else {
                        reason = String("Charging: store@") + String(storageCost, 1) + 
                                 " < maxBuy@" + String(maxFutureBuyPrice, 1);
                    }
                }
            }
            
            // 3c) PRODEJ DO SÍTĚ - drahá elektřina (včetně sell arbitráže)
            // Prodej má smysl když:
            // A) Máme skutečný přebytek energie nad budoucí spotřebu (původní logika)
            // B) SELL ARBITRÁŽ: Teď je drahá elektřina a později bude levná
            //    → Prodáme teď draze, nakoupíme později levně
            //    → Funguje i v noci bez produkce!
            if (availableFromBattery > 0 && solarSurplus <= 0 && decision == INVERTER_MODE_SELF_USE) {
                // Používáme LOKÁLNÍ maximum - je to nejlepší čas v následujících 12 hodinách?
                bool isBestSellTime = sellPrice >= localMaxSellPrice * BEST_SELL_TIME_THRESHOLD;
                // Prodej se vyplatí když sellPrice > lokální avgBuyPrice (jinak bychom později koupili dráž)
                // a zároveň sellPrice > batteryCost (jinak proděláváme na opotřebení)
                bool worthSelling = sellPrice > localAvgBuyPrice && sellPrice > batteryCostPerKwh;
                // Máme přebytek energie - baterie je plná a nepotřebujeme ji pro budoucí spotřebu
                float energyNeededForFuture = max(0.0f, remainingConsumption - remainingProduction);
                float minReserve = getMinBatteryKwh() + energyNeededForFuture;
                bool hasRealExcess = currentBatteryKwh > minReserve * 1.2f;  // 20% rezerva navíc
                
                // === NOVÉ: SELL ARBITRÁŽ ===
                // Prodat teď draze, nakoupit později levně
                // Profit = sellPrice - minFutureBuyPrice - batteryCostPerKwh
                // (batteryCost = půl cyklu při vybití, další půl cyklu bude při nabití)
                // Zjednodušení: počítáme celý cyklus při prodeji (konzervativnější)
                float sellArbitrageProfit = sellPrice - minFutureBuyPrice - batteryCostPerKwh;
                float minRequiredSellProfit = sellPrice * ARBITRAGE_PROFIT_THRESHOLD;
                // Sell arbitráž se vyplatí když:
                // 1) Zisk je dostatečný (> 15% z prodejní ceny)
                // 2) Máme kam později nabít (spaceInBattery po vybití)
                // 3) Je to blízko lokálního maxima prodejní ceny
                // 4) V budoucnu je výrazně levnější elektřina
                bool isSellPeak = sellPrice >= localMaxSellPrice * BEST_SELL_TIME_THRESHOLD;
                bool hasCheapFuture = minFutureBuyPrice < sellPrice * (1.0f - ARBITRAGE_PROFIT_THRESHOLD);
                bool worthSellArbitrage = sellArbitrageProfit > minRequiredSellProfit && 
                                          sellArbitrageProfit > 0 &&
                                          isSellPeak &&
                                          hasCheapFuture;
                
                // Debug log pro sell arbitráž
                if (availableFromBattery > 0.5f) {
                    log_d("Q%d SELL_ARB? sell=%.2f minBuy=%.2f batt=%.2f profit=%.2f (min=%.2f) peak=%d cheap=%d worth=%d",
                          q, sellPrice, minFutureBuyPrice, batteryCostPerKwh,
                          sellArbitrageProfit, minRequiredSellProfit,
                          isSellPeak, hasCheapFuture, worthSellArbitrage);
                }
                
                // Prodáváme pokud:
                // 1) Máme skutečný přebytek (původní logika) - prodáme přebytek
                // 2) NEBO se vyplatí sell arbitráž - prodáme i z "potřebné" baterie
                bool shouldSellExcess = isBestSellTime && worthSelling && hasRealExcess;
                bool shouldSellArbitrage = worthSellArbitrage;
                
                if (shouldSellExcess || shouldSellArbitrage) {
                    decision = INVERTER_MODE_DISCHARGE_TO_GRID;
                    
                    float toSell;
                    if (shouldSellArbitrage && !shouldSellExcess) {
                        // Sell arbitráž - prodáváme více, ale necháme rezervu na spotřebu do příštího nabíjení
                        // Kolik spotřebujeme do doby než bude levná elektřina? (zhruba 4-8 hodin)
                        float consumptionUntilCheap = consumptionKwh * 20;  // ~5 hodin spotřeby jako rezerva
                        float arbitrageReserve = getMinBatteryKwh() + consumptionUntilCheap;
                        float arbitrageExcess = max(0.0f, currentBatteryKwh - arbitrageReserve);
                        toSell = min(arbitrageExcess, min(availableFromBattery, maxDischargeQuarter));
                        reason = String("SellArb: sell@") + String(sellPrice, 1) + 
                                 " buy@" + String(minFutureBuyPrice, 1) +
                                 " profit:" + String(sellArbitrageProfit, 1);
                    } else {
                        // Přebytek - prodáváme jen to, co nepotřebujeme
                        float excess = currentBatteryKwh - minReserve;
                        toSell = min(excess, min(availableFromBattery, maxDischargeQuarter));
                        reason = String("Selling@") + String(sellPrice, 1) + 
                                 " > locAvgBuy@" + String(localAvgBuyPrice, 1);
                    }
                    
                    if (toSell > MIN_ENERGY_THRESHOLD) {
                        result.toGridKwh += toSell;
                        result.fromBatteryKwh += toSell;
                        currentBatteryKwh -= toSell;
                        result.costCzk -= toSell * sellPrice;  // Příjem z prodeje
                        // Opotřebení baterie - pro arbitráž počítáme celý cyklus při prodeji
                        if (shouldSellArbitrage && !shouldSellExcess) {
                            result.costCzk += toSell * batteryCostPerKwh;  // Náklad na opotřebení
                        }
                    } else {
                        // Není co prodat, vrátíme rozhodnutí na Self-Use
                        decision = INVERTER_MODE_SELF_USE;
                        reason = "Self-use";
                    }
                }
            }
            
            // 3c) HOLD - úplně odstraněn
            // HOLD blokoval vybíjení baterie a spotřeba šla ze sítě
            // Při přebytku energie za den není důvod HOLD používat
            // Self-Use je vždy lepší - baterie pokryje spotřebu
            
            // === KROK 4: Výpočet úspor ===
            // Úspora = kolik jsme ušetřili oproti nákupu ze sítě
            // - Ze solární výroby: ušetřili jsme celou nákupní cenu (neplatíme síť)
            // - Z baterie: ušetřili jsme rozdíl (buyPrice - batteryCost)
            // - Ze sítě: úspora = 0 (platíme plnou cenu)
            // - Prodej do sítě: přičítáme zisk z prodeje
            
            float savingsFromSolar = result.fromSolarKwh * buyPrice;  // Zdarma místo nákupu
            // Úspora z baterie = celá nákupní cena (batteryCost už byl započítán při nabíjení)
            float savingsFromBattery = result.fromBatteryKwh * buyPrice;
            float savingsFromGrid = 0;  // Žádná úspora
            float earningsFromSale = result.toGridKwh * sellPrice;  // Zisk z prodeje
            
            result.savingsVsGridCzk = savingsFromSolar + savingsFromBattery + earningsFromSale;
            
            result.decision = decision;
            result.reason = reason;
            
            // === DIAGNOSTICKÉ LOGOVÁNÍ PRO KAŽDÉ Q ===
            log_d("Q%d %02d:%02d | SOC:%.0f%% | prod:%.2f cons:%.2f | spot:%.2f buy:%.2f sell:%.2f | locMin:%.2f locMax:%.2f | dec:%s | %s",
                  q, result.hour, result.minute,
                  batterySocStart,
                  productionKwh, consumptionKwh,
                  spotPrice, buyPrice, sellPrice,
                  localMinBuyPrice, localMaxBuyPrice,
                  decisionToString(decision).c_str(),
                  reason.c_str());
            
            // Omezení baterie
            currentBatteryKwh = constrain(currentBatteryKwh, getMinBatteryKwh(), getMaxBatteryKwh());
            
            results.push_back(result);
        }
        
        return results;
    }
    
    /**
     * Simuluje "hloupé" Self-Use chování (bez inteligence)
     * 
     * Pravidla:
     * 1. Spotřeba se pokrývá: výroba → baterie → síť
     * 2. Přebytek výroby: baterie → prodej
     * 3. Nikdy nenabíjí ze sítě
     * 4. Nikdy aktivně neprodává z baterie
     * 
     * @param intelligentResults Výsledky inteligentní simulace (pro ceny a predikce)
     * @param startingBatterySoc Počáteční SOC baterie
     * @param outFinalBatteryKwh Výstupní parametr - finální stav baterie v kWh
     * @return Celkové náklady za simulované období
     */
    float simulateBaseline(const std::vector<QuarterSimulationResult>& intelligentResults,
                           float startingBatterySoc,
                           float& outFinalBatteryKwh) {
        float totalCost = 0;
        // Baseline začíná maximálně na maxSoc - hloupý systém by nikdy nenabil více
        float batteryKwh = min(socToKwh(startingBatterySoc), getMaxBatteryKwh());
        
        // Debug counters
        float totalSold = 0;
        float totalBought = 0;
        float totalCharged = 0;
        
        for (const auto& r : intelligentResults) {
            float productionKwh = r.productionKwh;
            float consumptionKwh = r.consumptionKwh;
            float buyPrice = r.buyPrice;
            float sellPrice = r.sellPrice;
            
            // Aktuální stav baterie
            float availableFromBattery = max(0.0f, batteryKwh - getMinBatteryKwh());
            float spaceInBattery = max(0.0f, getMaxBatteryKwh() - batteryKwh);
            float maxChargeQuarter = maxChargePowerKw * 0.25f;
            
            // === KROK 1: Pokrytí spotřeby ===
            float remainingConsumption = consumptionKwh;
            
            // 1a) Ze solární výroby (zdarma)
            float solarForConsumption = min(productionKwh, remainingConsumption);
            remainingConsumption -= solarForConsumption;
            float solarSurplus = productionKwh - solarForConsumption;
            
            // 1b) Z baterie (náklad = 0, opotřebení se počítá při nabíjení)
            float fromBattery = 0;
            if (remainingConsumption > 0 && availableFromBattery > 0) {
                fromBattery = min(availableFromBattery, remainingConsumption);
                batteryKwh -= fromBattery;
                remainingConsumption -= fromBattery;
                // Náklad = 0 (opotřebení už bylo započítáno při nabíjení)
            }
            
            // 1c) Ze sítě (platíme plnou cenu)
            if (remainingConsumption > 0) {
                totalCost += remainingConsumption * buyPrice;
                totalBought += remainingConsumption;
            }
            
            // === KROK 2: Přebytek výroby ===
            if (solarSurplus > 0) {
                // 2a) Nabíjíme baterii
                float toCharge = min(solarSurplus, min(spaceInBattery, maxChargeQuarter));
                batteryKwh += toCharge;
                totalCost += toCharge * batteryCostPerKwh;  // Náklad na opotřebení při nabíjení
                totalCharged += toCharge;
                solarSurplus -= toCharge;
                
                // 2b) Zbytek prodáváme
                if (solarSurplus > 0) {
                    totalCost -= solarSurplus * sellPrice;  // Záporné = výdělek
                    totalSold += solarSurplus;
                }
            }
            
            // Omezení baterie
            batteryKwh = constrain(batteryKwh, getMinBatteryKwh(), getMaxBatteryKwh());
        }
        
        outFinalBatteryKwh = batteryKwh;
        log_d("BASELINE: cost=%.1f sold=%.1f bought=%.1f charged=%.1f finalBat=%.1f kWh (%.0f%%)", 
              totalCost, totalSold, totalBought, totalCharged, batteryKwh, kwhToSoc(batteryKwh));
        return totalCost;
    }
    
    /**
     * Vrátí souhrn simulace včetně porovnání s baseline
     * 
     * Úspora se počítá jako rozdíl nákladů PLUS hodnota energie navíc v baterii.
     * Pokud inteligence nabije baterii více než baseline, tato energie
     * nahradí budoucí nákup ze sítě - oceňujeme ji MAXIMÁLNÍ nákupní cenou,
     * protože inteligence drží energii právě pro drahé hodiny.
     */
    SimulationSummary getSummary(const std::vector<QuarterSimulationResult>& results, float startingBatterySoc) {
        SimulationSummary summary;
        summary.totalProductionKwh = 0;
        summary.totalConsumptionKwh = 0;
        summary.totalFromGridKwh = 0;
        summary.totalToGridKwh = 0;
        summary.totalCostCzk = 0;
        summary.totalSavingsCzk = 0;
        summary.baselineCostCzk = 0;
        summary.batteryValueAdjustment = 0;
        summary.baselineFinalSoc = startingBatterySoc;
        summary.chargedFromGridKwh = 0;
        summary.chargedFromGridCost = 0;
        summary.maxBuyPrice = 0;
        summary.quartersSimulated = results.size();
        
        // Spočítej průměrnou a maximální nákupní cenu (pro ocenění energie v baterii)
        float avgBuyPrice = 0;
        float maxBuyPrice = 0;
        for (const auto& r : results) {
            summary.totalProductionKwh += r.productionKwh;
            summary.totalConsumptionKwh += r.consumptionKwh;
            summary.totalFromGridKwh += r.fromGridKwh;
            summary.totalToGridKwh += r.toGridKwh;
            summary.totalCostCzk += r.costCzk;
            avgBuyPrice += r.buyPrice;
            if (r.buyPrice > maxBuyPrice) maxBuyPrice = r.buyPrice;
            
            // Počítej nabíjení ze sítě (pro arbitráž)
            if (r.decision == INVERTER_MODE_CHARGE_FROM_GRID && r.toBatteryKwh > 0) {
                summary.chargedFromGridKwh += r.toBatteryKwh;
                // Náklady na nabíjení = cena elektřiny + opotřebení baterie
                summary.chargedFromGridCost += r.toBatteryKwh * (r.buyPrice + batteryCostPerKwh);
            }
        }
        if (!results.empty()) {
            avgBuyPrice /= results.size();
        }
        summary.maxBuyPrice = maxBuyPrice;
        
        // Spočítej baseline (hloupé Self-Use) a získej finální stav baterie
        float baselineFinalBatteryKwh = 0;
        summary.baselineCostCzk = simulateBaseline(results, startingBatterySoc, baselineFinalBatteryKwh);
        summary.baselineFinalSoc = kwhToSoc(baselineFinalBatteryKwh);
        
        // Finální SOC inteligentní simulace
        // Musíme spočítat stav baterie NA KONCI poslední čtvrthodiny
        // (batterySoc je stav na ZAČÁTKU čtvrthodiny)
        float intelligentFinalBatteryKwh = 0;
        if (!results.empty()) {
            const auto& lastResult = results.back();
            // Finální stav = stav na začátku + nabito - vybito
            intelligentFinalBatteryKwh = lastResult.batteryKwh + lastResult.toBatteryKwh - lastResult.fromBatteryKwh;
            // Omezení na platný rozsah
            intelligentFinalBatteryKwh = constrain(intelligentFinalBatteryKwh, getMinBatteryKwh(), getMaxBatteryKwh());
            summary.finalBatterySoc = kwhToSoc(intelligentFinalBatteryKwh);
        } else {
            summary.finalBatterySoc = startingBatterySoc;
            intelligentFinalBatteryKwh = socToKwh(startingBatterySoc);
        }
        
        // Rozdíl energie v baterii na konci (inteligence - baseline)
        // Kladná hodnota = inteligence má víc energie v baterii
        float batteryDifferenceKwh = intelligentFinalBatteryKwh - baselineFinalBatteryKwh;
        
        // Hodnota této energie = kolik ušetříme tím, že nemusíme kupovat ze sítě
        // Oceňujeme MAXIMÁLNÍ nákupní cenou, protože:
        // - Inteligence drží energii v baterii právě pro drahé hodiny
        // - Tuto energii použijeme místo nákupu ze sítě ve špičce
        // - BatteryCost už byl započítán při nabíjení (full cycle cost)
        // - Při vybíjení už žádné další náklady nejsou
        summary.batteryValueAdjustment = batteryDifferenceKwh * maxBuyPrice;
        
        // Úspora = (baseline náklady - inteligentní náklady) + hodnota energie navíc v baterii
        summary.totalSavingsCzk = (summary.baselineCostCzk - summary.totalCostCzk) + summary.batteryValueAdjustment;
        
        return summary;
    }
    
    /**
     * Vrátí souhrn simulace (zpětně kompatibilní verze)
     */
    SimulationSummary getSummary(const std::vector<QuarterSimulationResult>& results) {
        // Použij první SOC z výsledků jako výchozí
        float startingSoc = results.empty() ? 50.0f : results[0].batterySoc;
        return getSummary(results, startingSoc);
    }
    
    /**
     * Vrátí výsledek pro aktuální čtvrthodinu
     */
    QuarterSimulationResult getCurrentQuarterResult(const std::vector<QuarterSimulationResult>& results) {
        if (results.empty()) {
            QuarterSimulationResult empty;
            empty.decision = INVERTER_MODE_SELF_USE;
            empty.reason = "No simulation data";
            return empty;
        }
        return results[0];  // První výsledek je aktuální čtvrthodina
    }
    
    /**
     * Konverze rozhodnutí na text
     */
    static String decisionToString(InverterMode_t decision) {
        switch (decision) {
            case INVERTER_MODE_SELF_USE: return "Self-Use";
            case INVERTER_MODE_CHARGE_FROM_GRID: return "Charge";
            case INVERTER_MODE_DISCHARGE_TO_GRID: return "Discharge";
            case INVERTER_MODE_HOLD_BATTERY: return "Hold";
            default: return "Unknown";
        }
    }
};
