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
typedef struct IntelligenceResult
{
    InverterMode_t command; // Příkaz pro střídač (používá InverterMode_t z InverterResult.hpp)
    int targetSocPercent;   // Cílový SOC při nabíjení
    int maxDischargePowerW; // Maximální výkon vybíjení do sítě
    float expectedSavings;  // Očekávaná úspora/zisk za toto rozhodnutí
    String reason;          // Důvod rozhodnutí (pro debug/UI)

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
 * Struktura pro hodinovou ekonomickou analýzu
 */
struct HourlyEconomics
{
    int hour;
    float spotPrice;
    float buyPrice;             // Skutečná nákupní cena
    float sellPrice;            // Skutečná prodejní cena
    float predictedConsumption; // kWh
    float predictedProduction;  // kWh
    float netEnergy;            // production - consumption (kladné = přebytek)
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
class IntelligenceResolver
{
private:
    ConsumptionPredictor &consumptionPredictor;
    ProductionPredictor &productionPredictor;

    /**
     * Získá počet dostupných čtvrthodin (96 pro dnešek, 192 pokud máme i zítřek)
     */
    int getAvailableQuarters(const ElectricityPriceTwoDays_t &prices)
    {
        return prices.hasTomorrowData ? QUARTERS_TWO_DAYS : QUARTERS_OF_DAY;
    }

    /**
     * Najde nejlevnější hodinu pro nákup v daném časovém okně
     */
    int findCheapestHour(const ElectricityPriceTwoDays_t &prices, int fromQuarter, int toQuarter, const IntelligenceSettings_t &settings)
    {
        float minPrice = FLT_MAX;
        int cheapestQuarter = fromQuarter;
        int maxQuarter = min(toQuarter, getAvailableQuarters(prices));

        for (int q = fromQuarter; q < maxQuarter; q++)
        {
            float buyPrice = IntelligenceSettingsStorage::calculateBuyPrice(prices.prices[q].electricityPrice, settings);
            if (buyPrice < minPrice)
            {
                minPrice = buyPrice;
                cheapestQuarter = q;
            }
        }

        return cheapestQuarter / 4; // Převod na hodinu
    }

    /**
     * Najde nejdražší hodinu pro prodej v daném časovém okně
     */
    int findMostExpensiveHour(const ElectricityPriceTwoDays_t &prices, int fromQuarter, int toQuarter, const IntelligenceSettings_t &settings)
    {
        float maxPrice = -FLT_MAX;
        int expensiveQuarter = fromQuarter;
        int maxQuarter = min(toQuarter, getAvailableQuarters(prices));

        for (int q = fromQuarter; q < maxQuarter; q++)
        {
            float sellPrice = IntelligenceSettingsStorage::calculateSellPrice(prices.prices[q].electricityPrice, settings);
            if (sellPrice > maxPrice)
            {
                maxPrice = sellPrice;
                expensiveQuarter = q;
            }
        }

        return expensiveQuarter / 4;
    }

    /**
     * Vypočítá průměrnou nákupní cenu pro budoucí čtvrthodiny
     */
    float calculateAverageBuyPrice(const ElectricityPriceTwoDays_t &prices, int fromQuarter, const IntelligenceSettings_t &settings)
    {
        float sum = 0;
        int count = 0;
        int maxQuarter = getAvailableQuarters(prices);

        for (int q = fromQuarter; q < maxQuarter; q++)
        {
            sum += IntelligenceSettingsStorage::calculateBuyPrice(prices.prices[q].electricityPrice, settings);
            count++;
        }

        return count > 0 ? sum / count : 0;
    }

    /**
     * Vypočítá minimální nákupní cenu pro budoucí čtvrthodiny
     */
    float findMinBuyPrice(const ElectricityPriceTwoDays_t &prices, int fromQuarter, const IntelligenceSettings_t &settings)
    {
        float minPrice = FLT_MAX;
        int maxQuarter = getAvailableQuarters(prices);

        for (int q = fromQuarter; q < maxQuarter; q++)
        {
            float price = IntelligenceSettingsStorage::calculateBuyPrice(prices.prices[q].electricityPrice, settings);
            if (price < minPrice)
            {
                minPrice = price;
            }
        }

        return minPrice;
    }

    /**
     * Vypočítá maximální nákupní cenu pro budoucí čtvrthodiny
     */
    float findMaxBuyPrice(const ElectricityPriceTwoDays_t &prices, int fromQuarter, const IntelligenceSettings_t &settings)
    {
        float maxPrice = -FLT_MAX;
        int maxQuarter = getAvailableQuarters(prices);

        for (int q = fromQuarter; q < maxQuarter; q++)
        {
            float price = IntelligenceSettingsStorage::calculateBuyPrice(prices.prices[q].electricityPrice, settings);
            if (price > maxPrice)
            {
                maxPrice = price;
            }
        }

        return maxPrice;
    }

    /**
     * Vypočítá maximální prodejní cenu pro budoucí čtvrthodiny
     */
    float findMaxSellPrice(const ElectricityPriceTwoDays_t &prices, int fromQuarter, const IntelligenceSettings_t &settings)
    {
        float maxPrice = -FLT_MAX;
        int maxQuarter = getAvailableQuarters(prices);

        for (int q = fromQuarter; q < maxQuarter; q++)
        {
            float price = IntelligenceSettingsStorage::calculateSellPrice(prices.prices[q].electricityPrice, settings);
            if (price > maxPrice)
            {
                maxPrice = price;
            }
        }

        return maxPrice;
    }

public:
    IntelligenceResolver(ConsumptionPredictor &consumption, ProductionPredictor &production)
        : consumptionPredictor(consumption), productionPredictor(production) {}

    /**
     * Hlavní rozhodovací funkce
     *
     * @param inverterData aktuální stav střídače
     * @param prices spotové ceny (aktuální den)
     * @param settings nastavení inteligence
     * @param forQuarter čtvrthodina pro kterou chceme rozhodnutí (-1 = aktuální čas)
     * @param verbose zda logovat detailně (true jen pro aktuální čtvrthodinu)
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

        // Získání času - buď aktuálního nebo zadaného quarteru
        int currentHour;
        int priceQuarter; // Index do pole cen (0-191)
        int dayQuarter;   // Čtvrthodina v rámci dne (0-95) pro prediktory

        // forQuarter může být 0-191 (pro 2 dny)
        if (forQuarter >= 0 && forQuarter < QUARTERS_TWO_DAYS)
        {
            priceQuarter = forQuarter;
            dayQuarter = forQuarter % QUARTERS_OF_DAY; // 0-95 v rámci dne
            currentHour = dayQuarter / 4;              // Hodina v rámci dne (0-23)
        }
        else
        {
            time_t now = time(nullptr);
            struct tm *timeinfo = localtime(&now);
            currentHour = timeinfo->tm_hour;
            priceQuarter = (timeinfo->tm_hour * 60 + timeinfo->tm_min) / 15;
            dayQuarter = priceQuarter; // Pro dnešek jsou stejné
        }

        int currentSoc = inverterData.soc;

        // Kapacita baterie v kWh
        float batteryCapacityKwh = inverterData.batteryCapacityWh > 0
                                       ? inverterData.batteryCapacityWh / 1000.0f
                                       : 10.0f; // Výchozí 10kWh pokud neznáme

        // Použitelná kapacita baterie (mezi min a max SOC)
        float usableCapacityKwh = batteryCapacityKwh * (settings.maxSocPercent - settings.minSocPercent) / 100.0f;

        // Aktuální energie v baterii nad minimem
        float currentEnergyKwh = batteryCapacityKwh * (currentSoc - settings.minSocPercent) / 100.0f;
        if (currentEnergyKwh < 0)
            currentEnergyKwh = 0;

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
        float maxFutureBuyPrice = findMaxBuyPrice(prices, priceQuarter + 1, settings);
        float maxFutureSellPrice = findMaxSellPrice(prices, priceQuarter + 1, settings);
        float avgFutureBuyPrice = calculateAverageBuyPrice(prices, priceQuarter + 1, settings);

        // Predikce budoucí výroby (pro rozhodnutí o odložení nabíjení)
        float futureProduction = remainingProduction; // Už máme z prediktorů
        bool expectSolarSurplus = futureProduction > remainingConsumption;

        // Logování pouze pro aktuální čtvrthodinu (verbose mode)
        log_i("=== Intelligence Q%d: SOC=%d%%, buy=%.2f, sell=%.2f ===",
              priceQuarter, currentSoc, currentBuyPrice, currentSellPrice);

        // === ROZHODOVACÍ LOGIKA ===
        // Priorita: 1) Ochrana baterie, 2) Speciální příležitosti, 3) Ekonomická optimalizace, 4) Self-use

        // ═══════════════════════════════════════════════════════════════════
        // 1. OCHRANA BATERIE - SOC pod minimem
        // ═══════════════════════════════════════════════════════════════════
        if (currentSoc <= settings.minSocPercent)
        {
            log_d("Rule 1: SOC at minimum (%d%% <= %d%%)", currentSoc, settings.minSocPercent);
            // Baterie je prázdná - musíme buď nabíjet nebo držet
            if (currentBuyPrice <= minFutureBuyPrice && currentSoc < settings.maxSocPercent)
            {
                result.command = INVERTER_MODE_CHARGE_FROM_GRID;
                result.targetSocPercent = settings.maxSocPercent;
                result.reason = "SOC low & cheapest time - charging";
                log_d("-> CHARGE: cheapest time (%.2f <= %.2f)", currentBuyPrice, minFutureBuyPrice);
                return result;
            }
            result.command = INVERTER_MODE_HOLD_BATTERY;
            result.reason = "SOC at minimum, protecting battery";
            log_d("-> HOLD: protecting battery");
            return result;
        }

        // ═══════════════════════════════════════════════════════════════════
        // 2. SPECIÁLNÍ PŘÍLEŽITOST: Síť je levnější než baterie
        // ═══════════════════════════════════════════════════════════════════
        // Příklad: noční tarif, záporné ceny, velmi levná energie
        // HOLD baterii a kupuj ze sítě, ALE jen pokud budeme baterii potřebovat později
        if (currentBuyPrice < batteryCost && maxFutureBuyPrice > batteryCost)
        {
            log_d("Rule 2: Grid cheaper than battery (%.2f < %.2f) and will need later (%.2f > %.2f)",
                  currentBuyPrice, batteryCost, maxFutureBuyPrice, batteryCost);
            result.command = INVERTER_MODE_HOLD_BATTERY;
            result.expectedSavings = (batteryCost - currentBuyPrice) * remainingConsumption;
            result.reason = String("Grid (") + String(currentBuyPrice, 1) + ") cheaper than battery (" + String(batteryCost, 1) + ")";
            log_d("-> HOLD: save battery, buy from grid, savings=%.1f", result.expectedSavings);
            return result;
        }

        // ═══════════════════════════════════════════════════════════════════
        // 3. SPECIÁLNÍ PŘÍLEŽITOST: Odložení nabíjení - čekáme na solární výrobu
        // ═══════════════════════════════════════════════════════════════════
        // Ráno: místo vybíjení baterie (self-use) → HOLD a kup ze sítě
        // Baterie se nabije zadarmo ze slunce
        // Podmínky: očekáváme přebytek výroby A cena teď je vyšší než později
        if (expectSolarSurplus && currentSoc < settings.maxSocPercent)
        {
            log_d("Rule 3: Solar surplus expected (prod=%.1f > cons=%.1f), SOC=%d%% < max=%d%%",
                  remainingProduction, remainingConsumption, currentSoc, settings.maxSocPercent);
            // Baterie se nabije ze slunce - nemá smysl ji teď vybíjet
            // HOLD jen pokud je aktuální cena rozumná (ne extrémně drahá)
            if (currentBuyPrice < avgFutureBuyPrice * 1.5)
            {
                result.command = INVERTER_MODE_HOLD_BATTERY;
                result.reason = "Holding - solar will charge battery later";
                log_d("-> HOLD: waiting for solar (buy=%.2f < avgFuture*1.5=%.2f)",
                      currentBuyPrice, avgFutureBuyPrice * 1.5);
                return result;
            }
            log_d("-> SKIP: grid too expensive (%.2f >= %.2f)", currentBuyPrice, avgFutureBuyPrice * 1.5);
        }

        // ═══════════════════════════════════════════════════════════════════
        // 4. NABÍJENÍ ZE SÍTĚ - nejlevnější čas pro nákup
        // ═══════════════════════════════════════════════════════════════════
        if (currentSoc < settings.maxSocPercent)
        {
            bool isCheapestTime = currentBuyPrice <= minFutureBuyPrice;
            bool worthCharging = currentBuyPrice + batteryCost < avgFutureBuyPrice;
            bool willNeedEnergy = remainingConsumption > remainingProduction + currentEnergyKwh;

            log_d("Rule 4: Charge check - cheapest=%s (%.2f<=%.2f), worth=%s (%.2f+%.2f<%.2f), need=%s (%.1f>%.1f+%.1f)",
                  isCheapestTime ? "YES" : "NO", currentBuyPrice, minFutureBuyPrice,
                  worthCharging ? "YES" : "NO", currentBuyPrice, batteryCost, avgFutureBuyPrice,
                  willNeedEnergy ? "YES" : "NO", remainingConsumption, remainingProduction, currentEnergyKwh);

            if (isCheapestTime && worthCharging && willNeedEnergy)
            {
                result.command = INVERTER_MODE_CHARGE_FROM_GRID;
                result.targetSocPercent = settings.maxSocPercent;
                result.expectedSavings = (avgFutureBuyPrice - currentBuyPrice - batteryCost) * usableCapacityKwh;
                result.reason = String("Cheapest time to charge, saving ") + String(result.expectedSavings, 1) + " " + prices.currency;
                log_d("-> CHARGE: for consumption, savings=%.1f", result.expectedSavings);
                return result;
            }
        }

        // ═══════════════════════════════════════════════════════════════════
        // 4b. ARBITRÁŽ: Koupit levně teď, prodat draze později
        // ═══════════════════════════════════════════════════════════════════
        // Nabíjíme nad rámec vlastní spotřeby pokud se vyplatí arbitráž
        // Podmínka: prodejní cena později > nákupní cena teď + 2x náklady baterie
        // (2x protože energie projde baterií dvakrát - dovnitř a ven)
        if (currentSoc < settings.maxSocPercent)
        {
            float arbitrageProfit = maxFutureSellPrice - currentBuyPrice - (2 * batteryCost);
            bool worthArbitrage = arbitrageProfit > 0;
            bool isCheapTime = currentBuyPrice <= minFutureBuyPrice * 1.1; // Tolerance 10%

            // Kolik energie můžeme koupit navíc (nad vlastní spotřebu)?
            float excessCapacity = usableCapacityKwh - (remainingConsumption - remainingProduction);
            if (excessCapacity < 0)
                excessCapacity = 0;

            log_d("Rule 4b: Arbitrage - profit=%.2f (%.2f-%.2f-2*%.2f), worth=%s, cheapTime=%s, excessCap=%.1f",
                  arbitrageProfit, maxFutureSellPrice, currentBuyPrice, batteryCost,
                  worthArbitrage ? "YES" : "NO", isCheapTime ? "YES" : "NO", excessCapacity);

            if (worthArbitrage && isCheapTime && excessCapacity > 0)
            {
                result.command = INVERTER_MODE_CHARGE_FROM_GRID;
                result.targetSocPercent = settings.maxSocPercent;
                result.expectedSavings = arbitrageProfit * excessCapacity;
                result.reason = String("Arbitrage: buy ") + String(currentBuyPrice, 1) + ", sell " + String(maxFutureSellPrice, 1) + ", profit " + String(result.expectedSavings, 1) + " " + prices.currency;
                log_d("-> CHARGE: arbitrage, profit=%.1f", result.expectedSavings);
                return result;
            }
        }

        // ═══════════════════════════════════════════════════════════════════
        // 5. PRODEJ DO SÍTĚ - nejdražší čas pro prodej
        // ═══════════════════════════════════════════════════════════════════
        // Jen pokud máme přebytek (výroba > spotřeba) a je to výhodné
        if (netEnergy > 0 && currentSoc > settings.minSocPercent)
        {
            bool isExpensiveTime = currentSellPrice >= maxFutureSellPrice;
            bool worthSelling = currentSellPrice > batteryCost + minFutureBuyPrice;

            log_d("Rule 5: Sell check - netEnergy=%.1f>0, expensive=%s (%.2f>=%.2f), worth=%s (%.2f>%.2f+%.2f)",
                  netEnergy, isExpensiveTime ? "YES" : "NO", currentSellPrice, maxFutureSellPrice,
                  worthSelling ? "YES" : "NO", currentSellPrice, batteryCost, minFutureBuyPrice);

            if (isExpensiveTime && worthSelling)
            {
                result.command = INVERTER_MODE_DISCHARGE_TO_GRID;
                result.maxDischargePowerW = 5000;
                result.expectedSavings = (currentSellPrice - batteryCost - minFutureBuyPrice) * currentEnergyKwh;
                result.reason = String("Best time to sell, profit ") + String(result.expectedSavings, 1) + " " + prices.currency;
                log_d("-> DISCHARGE: best sell time, profit=%.1f", result.expectedSavings);
                return result;
            }
        }

        // ═══════════════════════════════════════════════════════════════════
        // 5b. PRODEJ Z ARBITRÁŽE - máme přebytek nad vlastní spotřebu
        // ═══════════════════════════════════════════════════════════════════
        // Pokud máme více energie v baterii než potřebujeme a je dobrá cena
        if (currentSoc > settings.minSocPercent)
        {
            // Kolik energie máme navíc nad budoucí spotřebu?
            float excessEnergy = currentEnergyKwh - (remainingConsumption - remainingProduction);
            if (excessEnergy < 0)
                excessEnergy = 0;

            bool isExpensiveTime = currentSellPrice >= maxFutureSellPrice * 0.95; // Tolerance 5%
            bool worthSelling = currentSellPrice > batteryCost + minFutureBuyPrice;

            log_d("Rule 5b: Sell excess - excessEnergy=%.1f, expensive=%s (%.2f>=%.2f*0.95), worth=%s",
                  excessEnergy, isExpensiveTime ? "YES" : "NO", currentSellPrice, maxFutureSellPrice,
                  worthSelling ? "YES" : "NO");

            if (excessEnergy > 0 && isExpensiveTime && worthSelling)
            {
                result.command = INVERTER_MODE_DISCHARGE_TO_GRID;
                result.maxDischargePowerW = 5000;
                result.expectedSavings = (currentSellPrice - batteryCost) * excessEnergy;
                result.reason = String("Selling excess: ") + String(excessEnergy, 1) + "kWh @ " + String(currentSellPrice, 1) + " " + prices.currency;
                log_d("-> DISCHARGE: selling excess %.1fkWh, profit=%.1f", excessEnergy, result.expectedSavings);
                return result;
            }
        }

        // ═══════════════════════════════════════════════════════════════════
        // 6. HOLD PRO POZDĚJŠÍ PRODEJ
        // ═══════════════════════════════════════════════════════════════════
        // Máme přebytek energie a později bude lepší cena
        // POZOR: Jen pokud máme přebytek! Jinak bychom kupovali ze sítě
        if (netEnergy > 0 && maxFutureSellPrice > currentSellPrice + batteryCost)
        {
            log_d("Rule 6: Hold for later - netEnergy=%.1f>0, futurePrice=%.2f > current=%.2f + bat=%.2f",
                  netEnergy, maxFutureSellPrice, currentSellPrice, batteryCost);
            result.command = INVERTER_MODE_HOLD_BATTERY;
            result.reason = String("Holding for better price later (") + String(maxFutureSellPrice, 1) + " vs " + String(currentSellPrice, 1) + ")";
            log_d("-> HOLD: waiting for better sell price");
            return result;
        }

        // ═══════════════════════════════════════════════════════════════════
        // 7. SELF-USE - standardní režim
        // ═══════════════════════════════════════════════════════════════════
        // Máme spotřebu a baterii - použij baterii (je levnější než síť)
        if (currentEnergyKwh > 0 && batteryCost <= currentBuyPrice)
        {
            log_d("Rule 7: Self-use - battery=%.1fkWh>0, batteryCost=%.2f <= gridBuy=%.2f",
                  currentEnergyKwh, batteryCost, currentBuyPrice);
            result.command = INVERTER_MODE_SELF_USE;
            result.expectedSavings = (currentBuyPrice - batteryCost) * min(remainingConsumption, currentEnergyKwh);
            result.reason = String("Self-use: battery (") + String(batteryCost, 1) + ") cheaper than grid (" + String(currentBuyPrice, 1) + ")";
            log_d("-> SELF_USE: using battery, savings=%.1f", result.expectedSavings);
            return result;
        }

        // ═══════════════════════════════════════════════════════════════════
        // 8. VÝCHOZÍ STAV - normální self-use
        // ═══════════════════════════════════════════════════════════════════
        log_d("Rule 8: Default self-use mode");
        result.command = INVERTER_MODE_SELF_USE;
        result.reason = "Normal self-consumption mode";
        log_d("-> SELF_USE: default mode");
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
        time_t now = time(nullptr);
        struct tm *timeinfo = localtime(&now);
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
    static String commandToString(InverterMode_t command)
    {
        switch (command)
        {
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
