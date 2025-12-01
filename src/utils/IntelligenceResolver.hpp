#pragma once

#include <Arduino.h>
#include <cfloat>
#include <vector>
#include "IntelligenceSettings.hpp"
#include "IntelligenceSimulator.hpp"
#include "ConsumptionPredictor.hpp"
#include "ProductionPredictor.hpp"
#include "../Inverters/InverterResult.hpp"
#include "../Spot/ElectricityPriceResult.hpp"

/**
 * Výstup resolveru (zpětná kompatibilita)
 */
typedef struct IntelligenceResult
{
    InverterMode_t command;
    int targetSocPercent;
    int maxDischargePowerW;
    float expectedSavings;
    String reason;

    static IntelligenceResult createDefault()
    {
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
 * Inteligentní resolver pro řízení střídače
 * 
 * Používá IntelligenceSimulator pro simulaci energetických toků
 * a vrací optimální rozhodnutí pro střídač.
 */
class IntelligenceResolver
{
private:
    ConsumptionPredictor &consumptionPredictor;
    ProductionPredictor &productionPredictor;
    IntelligenceSimulator simulator;
    
    // Cached výsledky simulace
    std::vector<QuarterSimulationResult> lastSimulationResults;
    SimulationSummary lastSummary;
    unsigned long lastSimulationTime = 0;
    static const unsigned long SIMULATION_CACHE_MS = 60000;  // 1 minuta

public:
    IntelligenceResolver(ConsumptionPredictor &consumption, ProductionPredictor &production)
        : consumptionPredictor(consumption), productionPredictor(production),
          simulator(consumption, production) {}

    /**
     * Spustí simulaci a vrátí výsledky
     */
    const std::vector<QuarterSimulationResult>& runSimulation(
        const InverterData_t &inverterData,
        const ElectricityPriceTwoDays_t &prices,
        const IntelligenceSettings_t &settings,
        bool forceRefresh = false)
    {
        unsigned long now = millis();
        
        // Použij cache pokud je platná
        if (!forceRefresh && 
            lastSimulationTime > 0 && 
            (now - lastSimulationTime) < SIMULATION_CACHE_MS &&
            !lastSimulationResults.empty()) {
            return lastSimulationResults;
        }
        
        // Spuštění nové simulace
        lastSimulationResults = simulator.simulate(inverterData, prices, settings);
        // Předej aktuální SOC pro výpočet baseline
        lastSummary = simulator.getSummary(lastSimulationResults, (float)inverterData.soc);
        lastSimulationTime = now;
        
        return lastSimulationResults;
    }
    
    /**
     * Vrátí poslední souhrn simulace
     */
    const SimulationSummary& getLastSummary() const {
        return lastSummary;
    }
    
    /**
     * Vrátí poslední výsledky simulace
     */
    const std::vector<QuarterSimulationResult>& getLastResults() const {
        return lastSimulationResults;
    }

    /**
     * Hlavní rozhodovací funkce
     * 
     * @param inverterData aktuální stav střídače
     * @param prices spotové ceny
     * @param settings nastavení inteligence
     * @param forQuarter čtvrthodina pro kterou chceme rozhodnutí (-1 = aktuální)
     * @return doporučený příkaz pro střídač
     */
    IntelligenceResult_t resolve(
        const InverterData_t &inverterData,
        const ElectricityPriceTwoDays_t &prices,
        const IntelligenceSettings_t &settings,
        int forQuarter = -1)
    {
        IntelligenceResult_t result = IntelligenceResult_t::createDefault();

        // Kontrola, zda je inteligence povolena
        if (!settings.enabled)
        {
            result.reason = "Intelligence disabled";
            return result;
        }

        // Kontrola platnosti dat
        if (prices.updated == 0)
        {
            result.reason = "No price data available";
            return result;
        }

        // Spustíme simulaci
        const auto& simResults = runSimulation(inverterData, prices, settings);
        
        if (simResults.empty()) {
            result.reason = "Simulation failed";
            return result;
        }
        
        // Získání aktuální čtvrthodiny
        time_t now = time(nullptr);
        struct tm *timeinfo = localtime(&now);
        int currentQuarter = (timeinfo->tm_hour * 60 + timeinfo->tm_min) / 15;
        
        // Najdeme výsledek pro požadovanou čtvrthodinu
        int targetIndex = 0;
        if (forQuarter >= 0) {
            // Hledáme konkrétní čtvrthodinu
            for (size_t i = 0; i < simResults.size(); i++) {
                if (simResults[i].quarter == forQuarter) {
                    targetIndex = i;
                    break;
                }
            }
        }
        
        if (targetIndex >= 0 && targetIndex < (int)simResults.size()) {
            const auto& simResult = simResults[targetIndex];
            result.command = simResult.decision;
            result.reason = simResult.reason;
            result.expectedSavings = simResult.savingsVsGridCzk;
            result.targetSocPercent = settings.maxSocPercent;
            result.maxDischargePowerW = 5000;
        }

        return result;
    }

    /**
     * Vytvoří přehled ekonomické analýzy pro UI
     */
    String getEconomicSummary(
        const InverterData_t &inverterData,
        const ElectricityPriceTwoDays_t &prices,
        const IntelligenceSettings_t &settings)
    {
        // Spustíme simulaci pokud nemáme výsledky
        const auto& results = runSimulation(inverterData, prices, settings);
        
        if (results.empty()) {
            return "No simulation data";
        }
        
        const auto& summary = lastSummary;
        const auto& current = results[0];
        
        String text = "";
        text += "Buy: " + String(current.buyPrice, 1) + " " + prices.currency + "/kWh\n";
        text += "Sell: " + String(current.sellPrice, 1) + " " + prices.currency + "/kWh\n";
        text += "Pred. cons: " + String(summary.totalConsumptionKwh, 1) + " kWh\n";
        text += "Pred. prod: " + String(summary.totalProductionKwh, 1) + " kWh\n";
        text += "Est. savings: " + String(summary.totalSavingsCzk, 1) + " " + prices.currency;
        
        return text;
    }

    /**
     * Vrátí textový popis příkazu
     */
    static String commandToString(InverterMode_t command)
    {
        return IntelligenceSimulator::decisionToString(command);
    }
};
