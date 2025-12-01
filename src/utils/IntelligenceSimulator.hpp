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
    float totalSavingsCzk;
    float finalBatterySoc;
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
    static constexpr float BEST_SELL_TIME_THRESHOLD = 0.95f;   // 95% maxima pro "nejlepší čas prodeje"
    static constexpr float ARBITRAGE_PROFIT_THRESHOLD = 0.05f; // 5% zisk z buy price pro arbitráž
    static constexpr float LOW_BATTERY_SOC = 60.0f;            // SOC pod kterým je baterie "nízká"
    static constexpr float MIN_ENERGY_THRESHOLD = 0.01f;       // Minimální energie pro akci (kWh)
    static constexpr float CONSUMPTION_SAFETY_MARGIN = 1.2f;   // 20% rezerva na spotřebu (pro jistotu)
    
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
     */
    void configure(const InverterData_t& inverterData, const IntelligenceSettings_t& settings) {
        batteryCapacityKwh = inverterData.batteryCapacityWh > 0 
            ? inverterData.batteryCapacityWh / 1000.0f 
            : 10.0f;
        minSocPercent = settings.minSocPercent;
        maxSocPercent = settings.maxSocPercent;
        batteryCostPerKwh = settings.batteryCostPerKwh;
        // Use values from inverter if available, otherwise default to 5kW
        maxChargePowerKw = inverterData.maxChargePowerW > 0 
            ? inverterData.maxChargePowerW / 1000.0f 
            : 5.0f;
        maxDischargePowerKw = inverterData.maxDischargePowerW > 0 
            ? inverterData.maxDischargePowerW / 1000.0f 
            : 5.0f;
        
        // Inicializace stavu baterie
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
            
            // 1b) Zbývá spotřeba - rozhodnutí baterie vs síť
            if (remainingConsumptionNow > 0) {
                // Máme spotřebu k pokrytí
                bool batteryBetterThanGrid = batteryCostPerKwh < buyPrice;
                bool gridCheaperThanBattery = buyPrice < batteryCostPerKwh;
                bool shouldHoldForLater = maxFutureBuyPrice > buyPrice + batteryCostPerKwh;
                
                if (gridCheaperThanBattery && shouldHoldForLater && availableFromBattery > 0) {
                    // Síť je levnější než baterie a později bude dražší → HOLD baterii
                    decision = INVERTER_MODE_HOLD_BATTERY;
                    reason = String("Grid (") + String(buyPrice, 1) + ") < battery (" + 
                             String(batteryCostPerKwh, 1) + "), hold for later";
                    
                    // Kupujeme ze sítě
                    result.fromGridKwh = remainingConsumptionNow;
                    result.costCzk = remainingConsumptionNow * buyPrice;
                } else if (availableFromBattery >= remainingConsumptionNow && batteryBetterThanGrid) {
                    // Baterie je levnější → použij baterii
                    result.fromBatteryKwh = remainingConsumptionNow;
                    currentBatteryKwh -= remainingConsumptionNow;
                    result.costCzk = remainingConsumptionNow * batteryCostPerKwh;
                    reason = String("Battery (") + String(batteryCostPerKwh, 1) + 
                             ") < grid (" + String(buyPrice, 1) + ")";
                } else if (availableFromBattery > 0 && batteryBetterThanGrid) {
                    // Částečně z baterie, zbytek ze sítě
                    result.fromBatteryKwh = min(availableFromBattery, remainingConsumptionNow);
                    currentBatteryKwh -= result.fromBatteryKwh;
                    result.fromGridKwh = remainingConsumptionNow - result.fromBatteryKwh;
                    result.costCzk = result.fromBatteryKwh * batteryCostPerKwh + 
                                     result.fromGridKwh * buyPrice;
                    reason = "Partial battery + grid";
                } else {
                    // Vše ze sítě
                    result.fromGridKwh = remainingConsumptionNow;
                    result.costCzk = remainingConsumptionNow * buyPrice;
                    reason = "Grid only";
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
                // Podmínky pro nabíjení
                bool isCheapTime = buyPrice <= minFutureBuyPrice * CHEAP_TIME_THRESHOLD;
                bool worthCharging = buyPrice + batteryCostPerKwh < avgFutureBuyPrice;
                // Spotřeba s bezpečnostní rezervou (pro jistotu nakoupíme víc)
                float expectedConsumption = remainingConsumption * CONSUMPTION_SAFETY_MARGIN;
                bool willNeedLater = expectedConsumption > remainingProduction;
                bool batteryLow = batterySocStart < LOW_BATTERY_SOC;
                
                // Arbitráž: koupit levně, prodat draze (potřebujeme zisk po započtení nákladů baterie)
                // Profit musí být alespoň X% z nákupní ceny
                float arbitrageProfit = maxFutureSellPrice - buyPrice - (2 * batteryCostPerKwh);
                float minRequiredProfit = buyPrice * ARBITRAGE_PROFIT_THRESHOLD;
                bool worthArbitrage = arbitrageProfit > minRequiredProfit && arbitrageProfit > 0;
                
                // Nabíjíme pokud:
                // 1) Je levný čas, vyplatí se nabíjet a budeme potřebovat energii
                // 2) Nebo se vyplatí arbitráž
                // 3) Nebo je baterie hodně prázdná a je levnější než průměr
                bool shouldCharge = (isCheapTime && worthCharging && willNeedLater) || 
                                    worthArbitrage ||
                                    (batteryLow && isCheapTime && willNeedLater);
                
                if (shouldCharge) {
                    decision = INVERTER_MODE_CHARGE_FROM_GRID;
                    float toCharge = min(spaceInBattery, maxChargeQuarter);
                    result.toBatteryKwh += toCharge;
                    result.fromGridKwh += toCharge;
                    currentBatteryKwh += toCharge;
                    result.costCzk += toCharge * buyPrice;
                    
                    if (worthArbitrage) {
                        reason = String("Arbitrage: buy@") + String(buyPrice, 1) + 
                                 " sell@" + String(maxFutureSellPrice, 1) +
                                 " profit:" + String(arbitrageProfit / buyPrice * 100, 0) + "%";
                    } else {
                        reason = String("Charging: cheap time @") + String(buyPrice, 1) + 
                                 " vs avg " + String(avgFutureBuyPrice, 1);
                    }
                }
            }
            
            // 3b) PRODEJ DO SÍTĚ - drahá elektřina
            if (availableFromBattery > 0 && solarSurplus <= 0) {
                bool isBestSellTime = sellPrice >= maxFutureSellPrice * BEST_SELL_TIME_THRESHOLD;
                bool worthSelling = sellPrice > batteryCostPerKwh + minFutureBuyPrice;
                bool hasExcessEnergy = currentBatteryKwh > getMinBatteryKwh() + 
                                       (remainingConsumption - remainingProduction);
                
                if (isBestSellTime && worthSelling && hasExcessEnergy) {
                    decision = INVERTER_MODE_DISCHARGE_TO_GRID;
                    float toSell = min(availableFromBattery, maxDischargeQuarter);
                    // Omezit na přebytek nad spotřebu
                    float excess = currentBatteryKwh - getMinBatteryKwh() - 
                                   max(0.0f, remainingConsumption - remainingProduction);
                    toSell = min(toSell, max(0.0f, excess));
                    
                    if (toSell > MIN_ENERGY_THRESHOLD) {
                        result.toGridKwh += toSell;
                        result.fromBatteryKwh += toSell;
                        currentBatteryKwh -= toSell;
                        result.costCzk -= toSell * sellPrice;
                        result.costCzk += toSell * batteryCostPerKwh;  // Náklad baterie
                        reason = String("Selling: best time @") + String(sellPrice, 1);
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
            float savingsFromBattery = result.fromBatteryKwh * (buyPrice - batteryCostPerKwh);  // Levnější než síť
            float savingsFromGrid = 0;  // Žádná úspora
            float earningsFromSale = result.toGridKwh * sellPrice;  // Zisk z prodeje
            
            result.savingsVsGridCzk = savingsFromSolar + savingsFromBattery + earningsFromSale;
            
            result.decision = decision;
            result.reason = reason;
            
            // Omezení baterie
            currentBatteryKwh = constrain(currentBatteryKwh, getMinBatteryKwh(), getMaxBatteryKwh());
            
            results.push_back(result);
        }
        
        return results;
    }
    
    /**
     * Vrátí souhrn simulace
     */
    SimulationSummary getSummary(const std::vector<QuarterSimulationResult>& results) {
        SimulationSummary summary;
        summary.totalProductionKwh = 0;
        summary.totalConsumptionKwh = 0;
        summary.totalFromGridKwh = 0;
        summary.totalToGridKwh = 0;
        summary.totalCostCzk = 0;
        summary.totalSavingsCzk = 0;
        summary.quartersSimulated = results.size();
        
        for (const auto& r : results) {
            summary.totalProductionKwh += r.productionKwh;
            summary.totalConsumptionKwh += r.consumptionKwh;
            summary.totalFromGridKwh += r.fromGridKwh;
            summary.totalToGridKwh += r.toGridKwh;
            summary.totalCostCzk += r.costCzk;
            summary.totalSavingsCzk += r.savingsVsGridCzk;
        }
        
        if (!results.empty()) {
            summary.finalBatterySoc = results.back().batterySoc;
        } else {
            summary.finalBatterySoc = 0;
        }
        
        return summary;
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
