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
     * Získá počet dostupných čtvrthodin (96 pro dnešek, 192 pokud máme i zítřek)
     */
    int getAvailableQuarters(const ElectricityPriceTwoDays_t& prices) {
        return prices.hasTomorrowData ? QUARTERS_TWO_DAYS : QUARTERS_OF_DAY;
    }
    
    /**
     * Najde nejlevnější hodinu pro nákup v daném časovém okně
     */
    int findCheapestHour(const ElectricityPriceTwoDays_t& prices, int fromQuarter, int toQuarter, const IntelligenceSettings_t& settings) {
        float minPrice = FLT_MAX;
        int cheapestQuarter = fromQuarter;
        int maxQuarter = min(toQuarter, getAvailableQuarters(prices));
        
        for (int q = fromQuarter; q < maxQuarter; q++) {
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
    int findMostExpensiveHour(const ElectricityPriceTwoDays_t& prices, int fromQuarter, int toQuarter, const IntelligenceSettings_t& settings) {
        float maxPrice = -FLT_MAX;
        int expensiveQuarter = fromQuarter;
        int maxQuarter = min(toQuarter, getAvailableQuarters(prices));
        
        for (int q = fromQuarter; q < maxQuarter; q++) {
            float sellPrice = IntelligenceSettingsStorage::calculateSellPrice(prices.prices[q].electricityPrice, settings);
            if (sellPrice > maxPrice) {
                maxPrice = sellPrice;
                expensiveQuarter = q;
            }
        }
        
        return expensiveQuarter / 4;
    }
    
    /**
     * Vypočítá průměrnou nákupní cenu pro budoucí čtvrthodiny
     */
    float calculateAverageBuyPrice(const ElectricityPriceTwoDays_t& prices, int fromQuarter, const IntelligenceSettings_t& settings) {
        float sum = 0;
        int count = 0;
        int maxQuarter = getAvailableQuarters(prices);
        
        for (int q = fromQuarter; q < maxQuarter; q++) {
            sum += IntelligenceSettingsStorage::calculateBuyPrice(prices.prices[q].electricityPrice, settings);
            count++;
        }
        
        return count > 0 ? sum / count : 0;
    }
    
    /**
     * Vypočítá minimální nákupní cenu pro budoucí čtvrthodiny
     */
    float findMinBuyPrice(const ElectricityPriceTwoDays_t& prices, int fromQuarter, const IntelligenceSettings_t& settings) {
        float minPrice = FLT_MAX;
        int maxQuarter = getAvailableQuarters(prices);
        
        for (int q = fromQuarter; q < maxQuarter; q++) {
            float price = IntelligenceSettingsStorage::calculateBuyPrice(prices.prices[q].electricityPrice, settings);
            if (price < minPrice) {
                minPrice = price;
            }
        }
        
        return minPrice;
    }
    
    /**
     * Vypočítá maximální prodejní cenu pro budoucí čtvrthodiny
     */
    float findMaxSellPrice(const ElectricityPriceTwoDays_t& prices, int fromQuarter, const IntelligenceSettings_t& settings) {
        float maxPrice = -FLT_MAX;
        int maxQuarter = getAvailableQuarters(prices);
        
        for (int q = fromQuarter; q < maxQuarter; q++) {
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
        const ElectricityPriceTwoDays_t& prices,
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
        int priceQuarter;    // Index do pole cen (0-191)
        int dayQuarter;      // Čtvrthodina v rámci dne (0-95) pro prediktory
        
        // forQuarter může být 0-191 (pro 2 dny)
        if (forQuarter >= 0 && forQuarter < QUARTERS_TWO_DAYS) {
            priceQuarter = forQuarter;
            dayQuarter = forQuarter % QUARTERS_OF_DAY;  // 0-95 v rámci dne
            currentHour = dayQuarter / 4;  // Hodina v rámci dne (0-23)
        } else {
            time_t now = time(nullptr);
            struct tm* timeinfo = localtime(&now);
            currentHour = timeinfo->tm_hour;
            priceQuarter = (timeinfo->tm_hour * 60 + timeinfo->tm_min) / 15;
            dayQuarter = priceQuarter;  // Pro dnešek jsou stejné
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
        
        // Predikce na zbytek dne - používáme čtvrthodinu v rámci dne (0-95)
        float remainingConsumption = consumptionPredictor.predictRemainingDayConsumption(dayQuarter);
        float remainingProduction = productionPredictor.predictRemainingDayProduction(dayQuarter);
        float netEnergy = remainingProduction - remainingConsumption;
        
        // Aktuální ceny - používáme index do pole cen (0-191)
        float currentSpotPrice = prices.prices[priceQuarter].electricityPrice;
        float currentBuyPrice = IntelligenceSettingsStorage::calculateBuyPrice(currentSpotPrice, settings);
        float currentSellPrice = IntelligenceSettingsStorage::calculateSellPrice(currentSpotPrice, settings);
        float batteryCost = settings.batteryCostPerKwh;
        
        // Budoucí ceny
        float minFutureBuyPrice = findMinBuyPrice(prices, priceQuarter + 1, settings);
        float maxFutureSellPrice = findMaxSellPrice(prices, priceQuarter + 1, settings);
        float avgFutureBuyPrice = calculateAverageBuyPrice(prices, priceQuarter + 1, settings);
        
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
        
        // 3. KLÍČOVÁ LOGIKA: Je levnější koupit ze sítě než použít baterii?
        // Pokud aktuální nákupní cena < cena energie z baterie, držíme baterii a jedeme ze sítě
        // Toto je případ kdy např. spot je 6.70 Kč a baterie stojí 23 Kč/kWh
        if (currentBuyPrice < batteryCost) {
            log_d("Grid cheaper than battery: buy=%.2f < battery=%.2f", currentBuyPrice, batteryCost);
            result.command = INVERTER_MODE_HOLD_BATTERY;
            result.expectedSavings = (batteryCost - currentBuyPrice) * remainingConsumption;
            result.reason = String("Grid cheaper (") + String(currentBuyPrice, 1) + ") than battery (" + String(batteryCost, 1) + ")";
            return result;
        }
        
        // 4. Analýza: Je výhodnější NABÍJET ZE SÍTĚ teď?
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
        
        // 5. Analýza: Je výhodnější VYBÍJET DO SÍTĚ teď?
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
        
        // 6. Analýza: Držet baterii pro pozdější špičku?
        // Pokud přijde dražší hodina a máme energii, držíme ji
        if (maxFutureSellPrice > currentSellPrice + batteryCost) {
            // Je výhodnější počkat a prodat později
            if (currentEnergyKwh > 0 && netEnergy > -currentEnergyKwh) {
                result.command = INVERTER_MODE_HOLD_BATTERY;
                result.reason = "Holding for better sell price later";
                return result;
            }
        }
        
        // 7. Pokud přijde levnější hodina pro nákup, použijeme baterii teď a koupíme později
        if (minFutureBuyPrice + batteryCost < currentBuyPrice) {
            // Bude levnější koupit později
            if (currentEnergyKwh > 0) {
                // Použijeme baterii místo nákupu teď
                result.command = INVERTER_MODE_SELF_USE;
                result.reason = "Using battery, will buy cheaper later";
                return result;
            }
        }
        
        // 8. Výchozí stav - pokud máme energii v baterii a je levnější než síť, použijeme ji
        if (currentEnergyKwh > 0 && batteryCost < currentBuyPrice) {
            result.command = INVERTER_MODE_SELF_USE;
            result.reason = "Battery cheaper than grid";
            return result;
        }
        
        // 9. Výchozí stav - normální self-use provoz
        result.command = INVERTER_MODE_SELF_USE;
        result.reason = "Normal self-consumption mode";
        return result;
    }
    
    /**
     * Vytvoří přehled ekonomické analýzy pro UI
     */
    String getEconomicSummary(
        const InverterData_t& inverterData,
        const ElectricityPriceTwoDays_t& prices,
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
