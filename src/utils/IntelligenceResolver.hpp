#pragma once

#include <Arduino.h>
#include <cfloat>
#include "IntelligenceSettings.hpp"
#include "ConsumptionPredictor.hpp"
#include "ProductionPredictor.hpp"
#include "../Inverters/InverterResult.hpp"
#include "../Spot/ElectricityPriceResult.hpp"

/**
 * Výstup resolveru
 */
typedef struct IntelligenceResult {
    InverterMode_t command;            // Příkaz pro střídač (používá InverterMode_t z InverterResult.hpp)
    int targetSocPercent;              // Cílový SOC při nabíjení
    int maxDischargePowerW;            // Maximální výkon vybíjení do sítě
    float expectedSavings;             // Očekávaná úspora/zisk za toto rozhodnutí
    String reason;                     // Důvod rozhodnutí (pro debug/UI)
    
    static IntelligenceResult createDefault() {
        IntelligenceResult result;
        result.command = INVERTER_MODE_SELF_USE;
        result.targetSocPercent = 100;
        result.maxDischargePowerW = 0;
        result.expectedSavings = 0;
        result.reason = "Default";
        return result;
    }
} IntelligenceResult_t;

/**
 * Struktura pro hodinovou ekonomickou analýzu
 */
struct HourlyEconomics {
    int hour;
    float spotPrice;
    float buyPrice;           // Skutečná nákupní cena
    float sellPrice;          // Skutečná prodejní cena
    float predictedConsumption;  // kWh
    float predictedProduction;   // kWh
    float netEnergy;          // production - consumption (kladné = přebytek)
};

/**
 * Inteligentní resolver pro řízení střídače
 * 
 * Rozhoduje na základě:
 * - Aktuálních a budoucích spotových cen
 * - Predikce spotřeby a výroby
 * - Ekonomických parametrů (cena baterie, koeficienty nákupu/prodeje)
 * - Aktuálního stavu baterie
 */
class IntelligenceResolver {
private:
    ConsumptionPredictor& consumptionPredictor;
    ProductionPredictor& productionPredictor;
    
    /**
     * Najde nejlevnější hodinu pro nákup v daném časovém okně
     */
    int findCheapestHour(const ElectricityPriceResult_t& prices, int fromQuarter, int toQuarter, const IntelligenceSettings_t& settings) {
        float minPrice = FLT_MAX;
        int cheapestQuarter = fromQuarter;
        
        for (int q = fromQuarter; q < toQuarter && q < QUARTERS_OF_DAY; q++) {
            float buyPrice = IntelligenceSettingsStorage::calculateBuyPrice(prices.prices[q].electricityPrice, settings);
            if (buyPrice < minPrice) {
                minPrice = buyPrice;
                cheapestQuarter = q;
            }
        }
        
        return cheapestQuarter / 4;  // Převod na hodinu
    }
    
    /**
     * Najde nejdražší hodinu pro prodej v daném časovém okně
     */
    int findMostExpensiveHour(const ElectricityPriceResult_t& prices, int fromQuarter, int toQuarter, const IntelligenceSettings_t& settings) {
        float maxPrice = -FLT_MAX;
        int expensiveQuarter = fromQuarter;
        
        for (int q = fromQuarter; q < toQuarter && q < QUARTERS_OF_DAY; q++) {
            float sellPrice = IntelligenceSettingsStorage::calculateSellPrice(prices.prices[q].electricityPrice, settings);
            if (sellPrice > maxPrice) {
                maxPrice = sellPrice;
                expensiveQuarter = q;
            }
        }
        
        return expensiveQuarter / 4;
    }
    
    /**
     * Vypočítá průměrnou nákupní cenu pro zbytek dne
     */
    float calculateAverageBuyPrice(const ElectricityPriceResult_t& prices, int fromQuarter, const IntelligenceSettings_t& settings) {
        float sum = 0;
        int count = 0;
        
        for (int q = fromQuarter; q < QUARTERS_OF_DAY; q++) {
            sum += IntelligenceSettingsStorage::calculateBuyPrice(prices.prices[q].electricityPrice, settings);
            count++;
        }
        
        return count > 0 ? sum / count : 0;
    }
    
    /**
     * Vypočítá minimální nákupní cenu pro zbytek dne
     */
    float findMinBuyPrice(const ElectricityPriceResult_t& prices, int fromQuarter, const IntelligenceSettings_t& settings) {
        float minPrice = FLT_MAX;
        
        for (int q = fromQuarter; q < QUARTERS_OF_DAY; q++) {
            float price = IntelligenceSettingsStorage::calculateBuyPrice(prices.prices[q].electricityPrice, settings);
            if (price < minPrice) {
                minPrice = price;
            }
        }
        
        return minPrice;
    }
    
    /**
     * Vypočítá maximální prodejní cenu pro zbytek dne
     */
    float findMaxSellPrice(const ElectricityPriceResult_t& prices, int fromQuarter, const IntelligenceSettings_t& settings) {
        float maxPrice = -FLT_MAX;
        
        for (int q = fromQuarter; q < QUARTERS_OF_DAY; q++) {
            float price = IntelligenceSettingsStorage::calculateSellPrice(prices.prices[q].electricityPrice, settings);
            if (price > maxPrice) {
                maxPrice = price;
            }
        }
        
        return maxPrice;
    }
    
public:
    IntelligenceResolver(ConsumptionPredictor& consumption, ProductionPredictor& production)
        : consumptionPredictor(consumption), productionPredictor(production) {}
    
    /**
     * Hlavní rozhodovací funkce
     * 
     * @param inverterData aktuální stav střídače
     * @param prices spotové ceny (aktuální den)
     * @param settings nastavení inteligence
     * @param forQuarter čtvrthodina pro kterou chceme rozhodnutí (-1 = aktuální čas)
     * @return doporučený příkaz pro střídač
     */
    IntelligenceResult_t resolve(
        const InverterData_t& inverterData,
        const ElectricityPriceResult_t& prices,
        const IntelligenceSettings_t& settings,
        int forQuarter = -1
    ) {
        IntelligenceResult_t result = IntelligenceResult_t::createDefault();
        
        // Kontrola, zda je inteligence povolena
        if (!settings.enabled) {
            result.reason = "Intelligence disabled";
            return result;
        }
        
        // Kontrola platnosti dat
        if (prices.updated == 0) {
            result.reason = "No price data available";
            return result;
        }
        
        // Získání času - buď aktuálního nebo zadaného quarteru
        int currentHour;
        int currentQuarter;
        
        if (forQuarter >= 0 && forQuarter < QUARTERS_OF_DAY) {
            currentQuarter = forQuarter;
            currentHour = forQuarter / 4;
        } else {
            time_t now = time(nullptr);
            struct tm* timeinfo = localtime(&now);
            currentHour = timeinfo->tm_hour;
            currentQuarter = (timeinfo->tm_hour * 60 + timeinfo->tm_min) / 15;
        }
        
        int currentSoc = inverterData.soc;
        
        // Kapacita baterie v kWh
        float batteryCapacityKwh = inverterData.batteryCapacityWh > 0 
            ? inverterData.batteryCapacityWh / 1000.0f 
            : 10.0f;  // Výchozí 10kWh pokud neznáme
        
        // Použitelná kapacita baterie (mezi min a max SOC)
        float usableCapacityKwh = batteryCapacityKwh * (settings.maxSocPercent - settings.minSocPercent) / 100.0f;
        
        // Aktuální energie v baterii nad minimem
        float currentEnergyKwh = batteryCapacityKwh * (currentSoc - settings.minSocPercent) / 100.0f;
        if (currentEnergyKwh < 0) currentEnergyKwh = 0;
        
        // Predikce na zbytek dne - používáme čtvrthodiny
        float remainingConsumption = consumptionPredictor.predictRemainingDayConsumption(currentQuarter);
        float remainingProduction = productionPredictor.predictRemainingDayProduction(currentQuarter);
        float netEnergy = remainingProduction - remainingConsumption;
        
        // Aktuální ceny
        float currentSpotPrice = prices.prices[currentQuarter].electricityPrice;
        float currentBuyPrice = IntelligenceSettingsStorage::calculateBuyPrice(currentSpotPrice, settings);
        float currentSellPrice = IntelligenceSettingsStorage::calculateSellPrice(currentSpotPrice, settings);
        float batteryCost = settings.batteryCostPerKwh;
        
        // Budoucí ceny
        float minFutureBuyPrice = findMinBuyPrice(prices, currentQuarter + 1, settings);
        float maxFutureSellPrice = findMaxSellPrice(prices, currentQuarter + 1, settings);
        float avgFutureBuyPrice = calculateAverageBuyPrice(prices, currentQuarter + 1, settings);
        
        log_d("Intelligence analysis: SOC=%d%%, consumption=%.1fkWh, production=%.1fkWh, net=%.1fkWh",
              currentSoc, remainingConsumption, remainingProduction, netEnergy);
        log_d("Prices: buy=%.2f, sell=%.2f, battery=%.2f, minFutureBuy=%.2f, maxFutureSell=%.2f",
              currentBuyPrice, currentSellPrice, batteryCost, minFutureBuyPrice, maxFutureSellPrice);
        
        // === ROZHODOVACÍ LOGIKA ===
        
        // 1. Ochrana baterie - pokud je SOC pod minimem, nenabízíme vybíjení
        if (currentSoc <= settings.minSocPercent) {
            result.command = INVERTER_MODE_HOLD_BATTERY;
            result.reason = "SOC at minimum, protecting battery";
            
            // Pokud je aktuální cena výhodná, můžeme nabíjet
            if (currentBuyPrice < minFutureBuyPrice && currentSoc < settings.maxSocPercent) {
                result.command = INVERTER_MODE_CHARGE_FROM_GRID;
                result.targetSocPercent = settings.maxSocPercent;
                result.reason = "SOC low & cheap price - charging";
            }
            return result;
        }
        
        // 2. Baterie plná a máme přebytek výroby
        if (currentSoc >= settings.maxSocPercent && remainingProduction > remainingConsumption) {
            result.command = INVERTER_MODE_SELF_USE;
            result.reason = "Battery full, using self-consumption";
            return result;
        }
        
        // 3. Analýza: Je výhodnější NABÍJET ZE SÍTĚ teď?
        // Nabíjíme pokud: aktuální nákupní cena + náklady baterie < budoucí průměrná cena
        // A zároveň: vyplatí se to (nejlevnější nákup)
        if (currentSoc < settings.maxSocPercent) {
            bool isCheapestTime = currentBuyPrice <= minFutureBuyPrice;
            bool worthCharging = currentBuyPrice + batteryCost < avgFutureBuyPrice;
            
            // Budeme potřebovat energii? (spotřeba > výroba + baterie)
            bool willNeedEnergy = remainingConsumption > remainingProduction + currentEnergyKwh;
            
            if (isCheapestTime && worthCharging && willNeedEnergy) {
                result.command = INVERTER_MODE_CHARGE_FROM_GRID;
                result.targetSocPercent = settings.maxSocPercent;
                result.expectedSavings = (avgFutureBuyPrice - currentBuyPrice - batteryCost) * usableCapacityKwh;
                result.reason = String("Cheapest time to charge, saving ") + String(result.expectedSavings, 1) + " " + prices.currency;
                return result;
            }
        }
        
        // 4. Analýza: Je výhodnější VYBÍJET DO SÍTĚ teď?
        // Vybíjíme pokud: aktuální prodejní cena > náklady baterie + budoucí nákupní cena
        // (vyplatí se prodat teď a koupit později)
        if (currentSoc > settings.minSocPercent) {
            bool isExpensiveTime = currentSellPrice >= maxFutureSellPrice;
            bool worthSelling = currentSellPrice > batteryCost + minFutureBuyPrice;
            
            // Máme přebytek energie? (baterie + výroba > spotřeba)
            bool hasExcessEnergy = currentEnergyKwh + remainingProduction > remainingConsumption;
            
            if (isExpensiveTime && worthSelling && hasExcessEnergy) {
                result.command = INVERTER_MODE_DISCHARGE_TO_GRID;
                result.maxDischargePowerW = 5000;  // Omezíme výkon, lze parametrizovat
                result.expectedSavings = (currentSellPrice - batteryCost - minFutureBuyPrice) * currentEnergyKwh;
                result.reason = String("Best time to sell, profit ") + String(result.expectedSavings, 1) + " " + prices.currency;
                return result;
            }
        }
        
        // 5. Analýza: Držet baterii pro pozdější špičku?
        // Pokud přijde dražší hodina a máme energii, držíme ji
        if (maxFutureSellPrice > currentSellPrice + batteryCost) {
            // Je výhodnější počkat a prodat později
            if (currentEnergyKwh > 0 && netEnergy > -currentEnergyKwh) {
                result.command = INVERTER_MODE_HOLD_BATTERY;
                result.reason = "Holding for better sell price later";
                return result;
            }
        }
        
        // 6. Pokud přijde levnější hodina a budeme potřebovat energii, také držíme
        if (minFutureBuyPrice + batteryCost < currentBuyPrice) {
            // Bude levnější koupit později
            if (currentEnergyKwh > 0) {
                // Použijeme baterii místo nákupu teď
                result.command = INVERTER_MODE_SELF_USE;
                result.reason = "Using battery, will buy cheaper later";
                return result;
            }
        }
        
        // 7. Výchozí stav - normální self-use provoz
        result.command = INVERTER_MODE_SELF_USE;
        result.reason = "Normal self-consumption mode";
        return result;
    }
    
    /**
     * Vytvoří přehled ekonomické analýzy pro UI
     */
    String getEconomicSummary(
        const InverterData_t& inverterData,
        const ElectricityPriceResult_t& prices,
        const IntelligenceSettings_t& settings
    ) {
        time_t now = time(nullptr);
        struct tm* timeinfo = localtime(&now);
        int currentHour = timeinfo->tm_hour;
        int currentQuarter = (timeinfo->tm_hour * 60 + timeinfo->tm_min) / 15;
        
        float currentSpotPrice = prices.prices[currentQuarter].electricityPrice;
        float currentBuyPrice = IntelligenceSettingsStorage::calculateBuyPrice(currentSpotPrice, settings);
        float currentSellPrice = IntelligenceSettingsStorage::calculateSellPrice(currentSpotPrice, settings);
        
        float remainingConsumption = consumptionPredictor.predictRemainingDayConsumption(currentQuarter);
        float remainingProduction = productionPredictor.predictRemainingDayProduction(currentQuarter);
        
        String summary = "";
        summary += "Current buy: " + String(currentBuyPrice, 2) + " " + prices.currency + "/kWh\n";
        summary += "Current sell: " + String(currentSellPrice, 2) + " " + prices.currency + "/kWh\n";
        summary += "Battery cost: " + String(settings.batteryCostPerKwh, 2) + " " + prices.currency + "/kWh\n";
        summary += "Pred. consumption: " + String(remainingConsumption, 1) + " kWh\n";
        summary += "Pred. production: " + String(remainingProduction, 1) + " kWh\n";
        summary += "SOC: " + String(inverterData.soc) + "%";
        
        return summary;
    }
    
    /**
     * Vrátí textový popis příkazu
     */
    static String commandToString(InverterMode_t command) {
        switch (command) {
            case INVERTER_MODE_SELF_USE:
                return "Self-Use";
            case INVERTER_MODE_CHARGE_FROM_GRID:
                return "Charge from Grid";
            case INVERTER_MODE_DISCHARGE_TO_GRID:
                return "Discharge to Grid";
            case INVERTER_MODE_HOLD_BATTERY:
                return "Hold Battery";
            default:
                return "Unknown";
        }
    }
};
