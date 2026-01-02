#pragma once

#include <Arduino.h>
#include <RemoteLogger.hpp>
#include <esp_heap_caps.h>
#include "ElectricityPriceResult.hpp"
#include "utils/FlashMutex.hpp"
#include "utils/Localization.hpp"

#include "Spot/Providers/EnergyChartsAPI.hpp"
#include "Spot/ExchangeRateLoader.hpp"

/**
 * Bidding zones supported by Energy Charts API
 * https://api.energy-charts.info/
 */
typedef enum
{
    BZN_NONE = 0,
    BZN_AT,           // Austria
    BZN_BE,           // Belgium
    BZN_BG,           // Bulgaria
    BZN_CH,           // Switzerland
    BZN_CZ,           // Czech Republic
    BZN_DE_LU,        // Germany, Luxembourg
    BZN_DE_AT_LU,     // Germany, Austria, Luxembourg (historical)
    BZN_DK1,          // Denmark 1
    BZN_DK2,          // Denmark 2
    BZN_EE,           // Estonia
    BZN_ES,           // Spain
    BZN_FI,           // Finland
    BZN_FR,           // France
    BZN_GR,           // Greece
    BZN_HR,           // Croatia
    BZN_HU,           // Hungary
    BZN_IT_NORTH,     // Italy North
    BZN_IT_CENTRE_NORTH, // Italy Centre North
    BZN_IT_CENTRE_SOUTH, // Italy Centre South
    BZN_IT_SOUTH,     // Italy South
    BZN_IT_SICILY,    // Italy Sicily
    BZN_IT_SARDINIA,  // Italy Sardinia
    BZN_LT,           // Lithuania
    BZN_LV,           // Latvia
    BZN_ME,           // Montenegro
    BZN_NL,           // Netherlands
    BZN_NO1,          // Norway 1
    BZN_NO2,          // Norway 2
    BZN_NO3,          // Norway 3
    BZN_NO4,          // Norway 4
    BZN_NO5,          // Norway 5
    BZN_PL,           // Poland
    BZN_PT,           // Portugal
    BZN_RO,           // Romania
    BZN_RS,           // Serbia
    BZN_SE1,          // Sweden 1
    BZN_SE2,          // Sweden 2
    BZN_SE3,          // Sweden 3
    BZN_SE4,          // Sweden 4
    BZN_SI,           // Slovenia
    BZN_SK,           // Slovakia
    BZN_COUNT
} ElectricityPriceProvider_t;

struct Timezone {
  const char* name;   // Caption shown to user
  const char* tz;     // TZ string for setenv()
};

const Timezone TIMEZONES[] = {
  {"UTC", "UTC0"},
  {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"},
  {"Europe/Paris", "CET-1CEST,M3.5.0/2,M10.5.0/3"},
  {"Europe/Moscow", "MSK-3"},
  {"Asia/Dubai", "GST-4"},
  {"Asia/Kolkata", "IST-5:30"},
  {"Asia/Bangkok", "ICT-7"},
  {"Asia/Singapore", "SGT-8"},
  {"Asia/Tokyo", "JST-9"},
  {"Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
  {"Pacific/Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
  {"America/St_Johns", "NST3:30NDT,M3.2.0,M11.1.0"},
  {"America/Halifax", "AST4ADT,M3.2.0,M11.1.0"},
  {"America/New_York", "EST5EDT,M3.2.0,M11.1.0"},
  {"America/Chicago", "CST6CDT,M3.2.0,M11.1.0"},
  {"America/Denver", "MST7MDT,M3.2.0,M11.1.0"},
  {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
  {"America/Anchorage", "AKST9AKDT,M3.2.0,M11.1.0"},
  {"Pacific/Honolulu", "HST10"},
  {"America/Sao_Paulo", "BRT3"},
  {"America/Argentina/Buenos_Aires", "ART3"},
  {"Africa/Cairo", "EET-2"},
  {"Africa/Johannesburg", "SAST-2"},
  {"Indian/Mauritius", "MUT-4"},
  {"Atlantic/Azores", "AZOT1AZOST,M3.5.0/0,M10.5.0/1"}
};

const int TIMEZONE_COUNT = sizeof(TIMEZONES) / sizeof(TIMEZONES[0]);

/**
 * Kód měny pro dynamické kurzy
 */
typedef enum {
    CURR_EUR = 0,  // Euro (bez konverze)
    CURR_CZK,      // Česká koruna
    CURR_PLN,      // Polský zlotý
    CURR_SEK,      // Švédská koruna
    CURR_NOK,      // Norská koruna
    CURR_DKK,      // Dánská koruna
    CURR_HUF,      // Maďarský forint
    CURR_BGN,      // Bulharský lev
    CURR_RON,      // Rumunský leu
    CURR_CHF,      // Švýcarský frank
} CurrencyCode_t;

/**
 * Struktura pro mapování bidding zone na API kód a metadata
 */
struct BiddingZoneInfo {
    const char* apiCode;      // Kód pro API (např. "CZ", "DE-LU")
    const char* caption;      // Popis pro UI
    const char* currency;     // Měna pro zobrazení (ct/kWh, Kč/kWh, gr/kWh)
    CurrencyCode_t currCode;  // Kód měny pro dynamický kurz
    float defaultRate;        // Záložní kurz (pokud API selže)
    float scaleMax;           // Doporučená max hodnota grafu
    int separatorStep;        // Krok mřížky grafu
};

// Mapování bidding zones na jejich vlastnosti
// defaultRate = multiplikátor pro konverzi EUR/kWh -> lokální jednotka/kWh
// Pro centy/øre/öre = kurz * 100, pro Kč/Ft/zł = kurz
const BiddingZoneInfo BIDDING_ZONES[] = {
    // BZN_NONE
    {"", "None", "ct", CURR_EUR, 100.0f, 30.0f, 5},
    // BZN_AT - Austria (EUR -> centy)
    {"AT", "Austria", "ct", CURR_EUR, 100.0f, 30.0f, 5},
    // BZN_BE - Belgium (EUR -> centy)
    {"BE", "Belgium", "ct", CURR_EUR, 100.0f, 30.0f, 5},
    // BZN_BG - Bulgaria (BGN -> stotinki, 1 BGN = 100 st, kurz ~1.96)
    {"BG", "Bulgaria", "st", CURR_BGN, 196.0f, 60.0f, 10},
    // BZN_CH - Switzerland (CHF -> Rappen, 1 CHF = 100 Rp, kurz ~0.95)
    {"CH", "Switzerland", "Rp", CURR_CHF, 95.0f, 30.0f, 5},
    // BZN_CZ - Czech Republic (CZK, kurz ~25)
    {"CZ", "Czech Republic", "Kč", CURR_CZK, 25.0f, 10.0f, 2},
    // BZN_DE_LU - Germany, Luxembourg (EUR -> centy)
    {"DE-LU", "Germany", "ct", CURR_EUR, 100.0f, 30.0f, 5},
    // BZN_DE_AT_LU - Germany, Austria, Luxembourg historical (EUR -> centy)
    {"DE-AT-LU", "Germany-Austria", "ct", CURR_EUR, 100.0f, 30.0f, 5},
    // BZN_DK1 - Denmark 1 (DKK -> øre, 1 DKK = 100 øre, kurz ~7.45)
    {"DK1", "Denmark West", "øre", CURR_DKK, 745.0f, 200.0f, 50},
    // BZN_DK2 - Denmark 2 (DKK -> øre)
    {"DK2", "Denmark East", "øre", CURR_DKK, 745.0f, 200.0f, 50},
    // BZN_EE - Estonia (EUR -> centy)
    {"EE", "Estonia", "ct", CURR_EUR, 100.0f, 30.0f, 5},
    // BZN_ES - Spain (EUR -> centy)
    {"ES", "Spain", "ct", CURR_EUR, 100.0f, 30.0f, 5},
    // BZN_FI - Finland (EUR -> centy)
    {"FI", "Finland", "ct", CURR_EUR, 100.0f, 30.0f, 5},
    // BZN_FR - France (EUR -> centy)
    {"FR", "France", "ct", CURR_EUR, 100.0f, 30.0f, 5},
    // BZN_GR - Greece (EUR -> centy)
    {"GR", "Greece", "ct", CURR_EUR, 100.0f, 30.0f, 5},
    // BZN_HR - Croatia (EUR from 2023 -> centy)
    {"HR", "Croatia", "ct", CURR_EUR, 100.0f, 30.0f, 5},
    // BZN_HU - Hungary (HUF, kurz ~400)
    {"HU", "Hungary", "Ft", CURR_HUF, 400.0f, 150.0f, 25},
    // BZN_IT_NORTH - Italy North (EUR -> centy)
    {"IT-North", "Italy North", "ct", CURR_EUR, 100.0f, 30.0f, 5},
    // BZN_IT_CENTRE_NORTH - Italy Centre North (EUR -> centy)
    {"IT-Centre-North", "Italy C-North", "ct", CURR_EUR, 100.0f, 30.0f, 5},
    // BZN_IT_CENTRE_SOUTH - Italy Centre South (EUR -> centy)
    {"IT-Centre-South", "Italy C-South", "ct", CURR_EUR, 100.0f, 30.0f, 5},
    // BZN_IT_SOUTH - Italy South (EUR -> centy)
    {"IT-South", "Italy South", "ct", CURR_EUR, 100.0f, 30.0f, 5},
    // BZN_IT_SICILY - Italy Sicily (EUR -> centy)
    {"IT-Sicily", "Italy Sicily", "ct", CURR_EUR, 100.0f, 30.0f, 5},
    // BZN_IT_SARDINIA - Italy Sardinia (EUR -> centy)
    {"IT-Sardinia", "Italy Sardinia", "ct", CURR_EUR, 100.0f, 30.0f, 5},
    // BZN_LT - Lithuania (EUR -> centy)
    {"LT", "Lithuania", "ct", CURR_EUR, 100.0f, 30.0f, 5},
    // BZN_LV - Latvia (EUR -> centy)
    {"LV", "Latvia", "ct", CURR_EUR, 100.0f, 30.0f, 5},
    // BZN_ME - Montenegro (EUR -> centy)
    {"ME", "Montenegro", "ct", CURR_EUR, 100.0f, 30.0f, 5},
    // BZN_NL - Netherlands (EUR -> centy)
    {"NL", "Netherlands", "ct", CURR_EUR, 100.0f, 30.0f, 5},
    // BZN_NO1 - Norway 1 (NOK -> øre, 1 NOK = 100 øre, kurz ~11.5)
    {"NO1", "Norway East", "øre", CURR_NOK, 1150.0f, 200.0f, 50},
    // BZN_NO2 - Norway 2 (NOK -> øre)
    {"NO2", "Norway South", "øre", CURR_NOK, 1150.0f, 200.0f, 50},
    // BZN_NO3 - Norway 3 (NOK -> øre)
    {"NO3", "Norway Mid", "øre", CURR_NOK, 1150.0f, 200.0f, 50},
    // BZN_NO4 - Norway 4 (NOK -> øre)
    {"NO4", "Norway North", "øre", CURR_NOK, 1150.0f, 200.0f, 50},
    // BZN_NO5 - Norway 5 (NOK -> øre)
    {"NO5", "Norway West", "øre", CURR_NOK, 1150.0f, 200.0f, 50},
    // BZN_PL - Poland (PLN -> grosze, 1 PLN = 100 gr, kurz ~4.3)
    {"PL", "Poland", "gr", CURR_PLN, 430.0f, 150.0f, 25},
    // BZN_PT - Portugal (EUR -> centy)
    {"PT", "Portugal", "ct", CURR_EUR, 100.0f, 30.0f, 5},
    // BZN_RO - Romania (RON -> bani, 1 RON = 100 bani, kurz ~5)
    {"RO", "Romania", "ban", CURR_RON, 500.0f, 150.0f, 25},
    // BZN_RS - Serbia (EUR -> centy)
    {"RS", "Serbia", "ct", CURR_EUR, 100.0f, 30.0f, 5},
    // BZN_SE1 - Sweden 1 (SEK -> öre, 1 SEK = 100 öre, kurz ~11.5)
    {"SE1", "Sweden North", "öre", CURR_SEK, 1150.0f, 200.0f, 50},
    // BZN_SE2 - Sweden 2 (SEK -> öre)
    {"SE2", "Sweden N-Mid", "öre", CURR_SEK, 1150.0f, 200.0f, 50},
    // BZN_SE3 - Sweden 3 (SEK -> öre)
    {"SE3", "Sweden S-Mid", "öre", CURR_SEK, 1150.0f, 200.0f, 50},
    // BZN_SE4 - Sweden 4 (SEK -> öre)
    {"SE4", "Sweden South", "öre", CURR_SEK, 1150.0f, 200.0f, 50},
    // BZN_SI - Slovenia (EUR -> centy)
    {"SI", "Slovenia", "ct", CURR_EUR, 100.0f, 30.0f, 5},
    // BZN_SK - Slovakia (EUR -> centy)
    {"SK", "Slovakia", "ct", CURR_EUR, 100.0f, 30.0f, 5},
};

class ElectricityPriceLoader
{
public:
    /**
     * Načte pouze dnešní spotové ceny do dvoudenní struktury
     * @param provider Poskytovatel cen (bidding zone)
     * @param outResult Pointer na dvoudenní strukturu (musí být předalokována v PSRAM)
     * @return true pokud se podařilo načíst data
     */
    bool loadTodayPrices(ElectricityPriceProvider_t provider, ElectricityPriceTwoDays_t* outResult)
    {
        if (!outResult) return false;
        
        outResult->updated = 0;
        outResult->hasTomorrowData = false;
        memset(outResult->currency, 0, CURRENCY_LENGTH);
        memset(outResult->energyUnit, 0, ENERGY_UNIT_LENGTH);
        
        // Načteme dnešní data přímo do výstupní struktury
        if (!loadPricesInternal(provider, false, outResult->prices, outResult->updated)) {
            return false;
        }
        
        // Nastavíme metadata
        const BiddingZoneInfo& zoneInfo = BIDDING_ZONES[provider];
        strncpy(outResult->currency, zoneInfo.currency, CURRENCY_LENGTH - 1);
        outResult->currency[CURRENCY_LENGTH - 1] = '\0';
        strcpy(outResult->energyUnit, "kWh");
        outResult->scaleMaxValue = zoneInfo.scaleMax;
        outResult->pricesHorizontalSeparatorStep = zoneInfo.separatorStep;
        
        return true;
    }
    
    /**
     * Načte zítřejší spotové ceny a přidá je do dvoudenní struktury
     * @param provider Poskytovatel cen (bidding zone)
     * @param outResult Pointer na dvoudenní strukturu (musí již obsahovat dnešní data)
     * @return true pokud se podařilo načíst zítřejší data
     */
    bool loadTomorrowPrices(ElectricityPriceProvider_t provider, ElectricityPriceTwoDays_t* outResult)
    {
        if (!outResult || outResult->updated == 0) return false;
        
        // Načteme zítřejší data přímo do pole na pozice 96-191
        time_t tomorrowUpdated = 0;
        if (!loadPricesInternal(provider, true, &outResult->prices[QUARTERS_OF_DAY], tomorrowUpdated)) {
            LOGD("Tomorrow electricity prices not yet available");
            return false;
        }
        
        outResult->hasTomorrowData = true;
        LOGD("Tomorrow electricity prices loaded successfully");
        
        return true;
    }

private:
    /**
     * Interní funkce pro načtení cen přímo do pole
     * @param provider Poskytovatel cen
     * @param tomorrow True pro zítřejší ceny
     * @param outPrices Pointer na pole cen (musí mít místo pro QUARTERS_OF_DAY položek)
     * @param outUpdated Reference na čas aktualizace
     * @return true pokud se podařilo načíst
     */
    bool loadPricesInternal(ElectricityPriceProvider_t provider, bool tomorrow, 
                           ElectricityPriceItem_t* outPrices, time_t& outUpdated)
    {
        if (provider == BZN_NONE || provider >= BZN_COUNT) {
            LOGD("No electricity price provider selected.");
            return false;
        }
        
        const BiddingZoneInfo& zoneInfo = BIDDING_ZONES[provider];
        LOGD("Loading electricity price data for %s (%s)", zoneInfo.caption, zoneInfo.apiCode);
        
        // Inicializace výstupního pole
        memset(outPrices, 0, sizeof(ElectricityPriceItem_t) * QUARTERS_OF_DAY);
        outUpdated = 0;
        
        // Načteme ceny z API - alokujeme dočasnou strukturu na heapu
        ElectricityPriceResult_t* tempResult = new ElectricityPriceResult_t();
        if (!tempResult) {
            LOGD("Failed to allocate temp result");
            return false;
        }
        
        tempResult->updated = 0;
        
        EnergyChartsAPI api;
        for (int r = 0; r < 5; r++)
        {
            if (api.reloadData(zoneInfo.apiCode, tomorrow, *tempResult))
            {
                break;
            }
            
            LOGD("Retry %d/5 for %s", r + 1, zoneInfo.apiCode);
            delay(5000 * (r + 1));
        }
        
        if (tempResult->updated == 0) {
            LOGD("Failed to load electricity prices for %s", zoneInfo.apiCode);
            delete tempResult;
            return false;
        }
        
        // Krátká pauza mezi HTTP voláními
        delay(100);
        
        // Aktualizujeme kurzy (pokud je potřeba)
        ExchangeRateLoader::getInstance()->updateRatesIfNeeded();
        
        // Získáme aktuální kurz
        float currencyRate = getCurrencyRate(zoneInfo.currCode, zoneInfo.defaultRate);
        LOGD("Using currency rate: %.4f for %s", currencyRate, zoneInfo.currency);
        
        // Aplikujeme konverzi EUR/MWh -> lokální měna/kWh
        // EUR/MWh / 1000 = EUR/kWh, pak * kurz = lokální měna/kWh
        for (int i = 0; i < QUARTERS_OF_DAY; i++)
        {
            outPrices[i].electricityPrice = (tempResult->prices[i].electricityPrice / 1000.0f) * currencyRate;
            outPrices[i].priceLevel = getPriceLevel(outPrices[i].electricityPrice, provider);
        }
        
        outUpdated = tempResult->updated;
        
        delete tempResult;
        return true;
    }

    /**
     * Získá multiplikátor pro konverzi EUR/kWh -> lokální jednotka/kWh
     * Pro měny s centy/øre/öre (1/100) vrací kurz * 100
     * Pro celé jednotky (Kč, Ft) vrací jen kurz
     */
    float getCurrencyRate(CurrencyCode_t currCode, float defaultRate)
    {
        ExchangeRateLoader* rateLoader = ExchangeRateLoader::getInstance();
        
        // Pokud nemáme platné kurzy, použijeme záložní hodnotu (již obsahuje správný multiplikátor)
        if (!rateLoader->hasValidRates()) {
            return defaultRate;
        }
        
        // Pro měny s podjednotkami (centy, øre, öre, grosze, bani, stotinki, Rappen)
        // násobíme kurz * 100
        switch (currCode) {
            case CURR_CZK: return rateLoader->getCZK();           // Kč - celé jednotky
            case CURR_HUF: return rateLoader->getHUF();           // Ft - celé jednotky
            case CURR_PLN: return rateLoader->getPLN() * 100.0f;  // gr = 1/100 zł
            case CURR_SEK: return rateLoader->getSEK() * 100.0f;  // öre = 1/100 SEK
            case CURR_NOK: return rateLoader->getNOK() * 100.0f;  // øre = 1/100 NOK
            case CURR_DKK: return rateLoader->getDKK() * 100.0f;  // øre = 1/100 DKK
            case CURR_BGN: return rateLoader->getBGN() * 100.0f;  // stotinki = 1/100 lev
            case CURR_RON: return rateLoader->getRON() * 100.0f;  // bani = 1/100 leu
            case CURR_CHF: return rateLoader->getCHF() * 100.0f;  // Rappen = 1/100 CHF
            case CURR_EUR:
            default: return 100.0f;  // centy = 1/100 EUR
        }
    }

public:
    ElectricityPriceProvider_t getStoredElectricityPriceProvider()
    {
        FlashGuard guard("SpotProvider:get");
        if (!guard.isLocked()) {
            return BZN_NONE;
        }
        
        Preferences preferences;
        preferences.begin("spot", true);
        int provider = preferences.getInt("provider", BZN_NONE);
        preferences.end();
        
        // Validace - pokud je mimo rozsah, vrátíme NONE
        if (provider < 0 || provider >= BZN_COUNT) {
            return BZN_NONE;
        }
        
        return (ElectricityPriceProvider_t)provider;
    }

    void storeElectricityPriceProvider(ElectricityPriceProvider_t provider)
    {
        FlashGuard guard("SpotProvider:set");
        if (!guard.isLocked()) {
            return;
        }
        
        Preferences preferences;
        preferences.begin("spot", false);
        preferences.putInt("provider", (int)provider);
        preferences.end();
    }

    String getStoredTimeZone()
    {
        FlashGuard guard("TimeZone:get");
        if (!guard.isLocked()) {
            return "CET-1CEST,M3.5.0/2,M10.5.0/3";
        }
        
        Preferences preferences;
        preferences.begin("spot", true);
        String timeZone = preferences.getString("tz", "CET-1CEST,M3.5.0/2,M10.5.0/3");
        preferences.end();
        return timeZone;
    }

    void storeTimeZone(String timeZone)
    {
        FlashGuard guard("TimeZone:set");
        if (!guard.isLocked()) {
            return;
        }
        
        Preferences preferences;
        preferences.begin("spot", false);
        preferences.putString("tz", timeZone);
        preferences.end();
    }

    String getProviderCaption(ElectricityPriceProvider_t provider)
    {
        if (provider >= 0 && provider < BZN_COUNT) {
            return String(BIDDING_ZONES[provider].caption);
        }
        return "Unknown";
    }

    String getCurrency(ElectricityPriceProvider_t provider)
    {
        if (provider >= 0 && provider < BZN_COUNT) {
            return String(BIDDING_ZONES[provider].currency);
        }
        return "ct/kWh";
    }

private:
    /**
     * Práh pro levnou cenu (v lokální měně/kWh)
     * Základ: 0.06 EUR/kWh = 6 ct/kWh
     */
    float getCheapPriceThreshold(ElectricityPriceProvider_t provider)
    {
        if (provider < 0 || provider >= BZN_COUNT) return 6.0f;
        
        const BiddingZoneInfo& zoneInfo = BIDDING_ZONES[provider];
        float rate = getCurrencyRate(zoneInfo.currCode, zoneInfo.defaultRate);
        // Základ je 0.06 EUR/kWh, násobíme multiplikátorem
        return 0.06f * rate;
    }

    /**
     * Práh pro drahou cenu (v lokální měně/kWh)
     * Základ: 0.12 EUR/kWh = 12 ct/kWh
     */
    float getExpensivePriceThreshold(ElectricityPriceProvider_t provider)
    {
        if (provider < 0 || provider >= BZN_COUNT) return 12.0f;
        
        const BiddingZoneInfo& zoneInfo = BIDDING_ZONES[provider];
        float rate = getCurrencyRate(zoneInfo.currCode, zoneInfo.defaultRate);
        // Základ je 0.12 EUR/kWh, násobíme multiplikátorem
        return 0.12f * rate;
    }

    PriceLevel_t getPriceLevel(float price, ElectricityPriceProvider_t provider)
    {
        if (price < 0.0f)
        {
            return PRICE_LEVEL_NEGATIVE;
        }
        else if (price <= getCheapPriceThreshold(provider))
        {
            return PRICE_LEVEL_CHEAP;
        }
        else if (price >= getExpensivePriceThreshold(provider))
        {
            return PRICE_LEVEL_EXPENSIVE;
        }
        else
        {
            return PRICE_LEVEL_MEDIUM;
        }
    }
};
