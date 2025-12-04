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
    static constexpr float BEST_SELL_TIME_THRESHOLD = 0.90f;   // 90% maxima pro "nejlepší čas prodeje"
    static constexpr float ARBITRAGE_PROFIT_THRESHOLD = 0.15f; // 15% zisk z buy price pro arbitráž
    static constexpr float LOW_BATTERY_SOC = 60.0f;            // SOC pod kterým je baterie "nízká"
    static constexpr float MIN_ENERGY_THRESHOLD = 0.01f;       // Minimální energie pro akci (kWh)
    static constexpr float CONSUMPTION_SAFETY_MARGIN = 1.2f;   // 20% rezerva na spotřebu (pro jistotu)
    static constexpr int LOCAL_WINDOW_QUARTERS = 48;           // Okno pro lokální min/max (12 hodin = 48 čtvrthodin)
    
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

public:
    IntelligenceSimulator(ConsumptionPredictor& consumption, ProductionPredictor& production)
        : consumptionPredictor(consumption), productionPredictor(production),
          batteryCapacityKwh(10.0f), minSocPercent(10), maxSocPercent(100),
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
                //
                // Používáme LOKÁLNÍ hodnoty (12h okno), protože:
                // - Nás zajímá nejbližší špička, ne špička za 24 hodin
                // - HOLD na příliš dlouhou dobu není efektivní
                //
                bool isCheapNow = buyPrice < localAvgBuyPrice;  // Teď je levněji než lokální průměr
                // Čistý zisk z HOLD = rozdíl cen (batteryCost se počítá jen při nabíjení, ne při vybíjení)
                float holdProfit = localMaxBuyPrice - buyPrice;
                bool holdIsProfitable = holdProfit > 0;
                
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
                //
                // Klíčová změna: HOLD jen pokud by se NEVYPLATILO nabíjet
                // Nabíjení se vyplatí když: jsme blízko minima A storageCost < localMaxBuyPrice
                bool canCharge = spaceInBattery > 0;
                float storageCostForHold = buyPrice + batteryCostPerKwh;
                bool isNearMinimumForHold = buyPrice <= localMinBuyPrice * CHEAP_TIME_THRESHOLD;
                bool chargingWouldBeWorthEconomically = storageCostForHold < localMaxBuyPrice;
                // Nabíjení má přednost jen pokud jsme blízko minima - jinak čekáme na levnější cenu
                bool shouldChargeInsteadOfHold = canCharge && isNearMinimumForHold && chargingWouldBeWorthEconomically;
                
                bool shouldHoldForLater = batteryBetterThanGrid && 
                                          isCheapNow &&
                                          holdIsProfitable && 
                                          willNeedBatteryLater &&
                                          availableFromBattery > 0 &&
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
            
            // 3a) NABÍJENÍ ZE SÍTĚ - levná elektřina
            // Nabíjíme pokud: je to výhodné pro pozdější spotřebu NEBO pro arbitráž
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
                
                // Arbitráž: koupit levně, prodat draze (potřebujeme zisk po započtení nákladů baterie)
                // Profit musí být alespoň X% z nákupní ceny
                // BatteryCost se počítá jen jednou za celý cyklus
                float arbitrageProfit = maxFutureSellPrice - buyPrice - batteryCostPerKwh;
                float minRequiredProfit = buyPrice * ARBITRAGE_PROFIT_THRESHOLD;
                bool worthArbitrage = arbitrageProfit > minRequiredProfit && arbitrageProfit > 0;
                
                // Debug: log charging decision factors
                if (i < 3 || (q >= 96 && q <= 100)) {
                    log_d("Q%d CHARGE? nearMin=%d (%.2f<=%.2f*1.1=%.2f), worthEcon=%d (%.2f<%.2f), need=%d (%.1f>%.1f), arb=%d",
                          q, isNearMinimum, buyPrice, localMinBuyPrice, localMinBuyPrice * CHEAP_TIME_THRESHOLD,
                          worthChargingEconomically, storageCost, localMaxBuyPrice,
                          willNeedLater, expectedConsumption, remainingProduction,
                          worthArbitrage);
                }
                
                // Nabíjíme pokud:
                // 1) Jsme blízko lokálního minima A vyplatí se nabíjet ekonomicky A budeme potřebovat energii
                // 2) Nebo se vyplatí arbitráž (ta má vlastní pravidla o ceně)
                // Klíčová změna: Musíme být u MINIMA, ne jen pod maximem!
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
            
            // 3b) PRODEJ DO SÍTĚ - drahá elektřina
            // Prodej má smysl pouze když:
            // 1) Prodejní cena je vyšší než náklad na vybití baterie
            // 2) Máme skutečný přebytek energie nad budoucí spotřebu
            // 3) Je to nejlepší čas k prodeji (v lokálním okně 12h)
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
                
                if (isBestSellTime && worthSelling && hasRealExcess) {
                    decision = INVERTER_MODE_DISCHARGE_TO_GRID;
                    float excess = currentBatteryKwh - minReserve;
                    float toSell = min(excess, min(availableFromBattery, maxDischargeQuarter));
                    
                    if (toSell > MIN_ENERGY_THRESHOLD) {
                        result.toGridKwh += toSell;
                        result.fromBatteryKwh += toSell;
                        currentBatteryKwh -= toSell;
                        result.costCzk -= toSell * sellPrice;  // Příjem z prodeje
                        // Opotřebení baterie se počítá jen při nabíjení (full cycle cost)
                        reason = String("Selling@") + String(sellPrice, 1) + 
                                 " > locAvgBuy@" + String(localAvgBuyPrice, 1);
                    }
                }
            }
            
            // 3c) HOLD - čekáme na lepší podmínky
            if (decision == INVERTER_MODE_SELF_USE && expectSolarSurplus && 
                batterySocStart < maxSocPercent && productionKwh < 0.1f) {
                // Ráno bez výroby, ale očekáváme solární přebytek
                decision = INVERTER_MODE_HOLD_BATTERY;
                reason = "Holding for solar charging later";
            }
            
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
        float batteryKwh = socToKwh(startingBatterySoc);
        
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
            }
            
            // === KROK 2: Přebytek výroby ===
            if (solarSurplus > 0) {
                // 2a) Nabíjíme baterii
                float toCharge = min(solarSurplus, min(spaceInBattery, maxChargeQuarter));
                batteryKwh += toCharge;
                totalCost += toCharge * batteryCostPerKwh;  // Náklad na opotřebení při nabíjení
                solarSurplus -= toCharge;
                
                // 2b) Zbytek prodáváme
                if (solarSurplus > 0) {
                    totalCost -= solarSurplus * sellPrice;  // Záporné = výdělek
                }
            }
            
            // Omezení baterie
            batteryKwh = constrain(batteryKwh, getMinBatteryKwh(), getMaxBatteryKwh());
        }
        
        outFinalBatteryKwh = batteryKwh;
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
