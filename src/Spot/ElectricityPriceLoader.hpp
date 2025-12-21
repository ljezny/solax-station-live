#pragma once

#include <Arduino.h>
#include <RemoteLogger.hpp>
#include <esp_heap_caps.h>
#include "ElectricityPriceResult.hpp"
#include "utils/FlashMutex.hpp"
#include "utils/Localization.hpp"

#include "Spot/Providers/OTE_CZ_API.hpp"
#include "Spot/Providers/OTK_SK_API.hpp"
#include "Spot/Providers/PSE_PL_API.hpp"
#include "Spot/Providers/Awattar_API.hpp"
#include "Spot/Providers/Elpris_API.hpp"
#include "Spot/Providers/SpotHintaFI_API.hpp"
#include "Spot/Providers/NordPoolAPI.hpp"

typedef enum
{
    NONE = 0,
    OTE_CZ,
    OTK_SK,
    PSE_PL,
    AWATTAR_DE,
    AWATTAR_AT,
    ELPRIS_SE1,
    ELPRIS_SE2,
    ELPRIS_SE3,
    ELPRIS_SE4,
    SPOT_HINTA_FI_DK1,
    SPOT_HINTA_FI_DK2,
    SPOT_HINTA_FI_EE,
    SPOT_HINTA_FIFI,
    SPOT_HINTA_FI_LT,
    SPOT_HINTA_FI_LV,
    SPOT_HINTA_FI_NO1,
    SPOT_HINTA_FI_NO2,
    SPOT_HINTA_FI_NO3,
    SPOT_HINTA_FI_NO4,
    SPOT_HINTA_FI_NO5,
    NORDPOOL_AT,
    NORDPOOL_BE,
    NORDPOOL_FR,
    NORDPOOL_GER,
    NORDPOOL_NL,
    NORDPOOL_PL
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

class ElectricityPriceLoader
{
public:
    /**
     * Načte pouze dnešní spotové ceny do dvoudenní struktury
     * @param provider Poskytovatel cen
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
        
        // Načteme dnešní data
        ElectricityPriceResult_t result = getElectricityPriceInternal(provider, false);
        
        if (result.updated == 0) {
            return false;
        }
        
        // Zkopírujeme do výstupní struktury
        outResult->updated = result.updated;
        strcpy(outResult->currency, result.currency);
        strcpy(outResult->energyUnit, result.energyUnit);
        outResult->scaleMaxValue = result.scaleMaxValue;
        outResult->pricesHorizontalSeparatorStep = result.pricesHorizontalSeparatorStep;
        for (int i = 0; i < QUARTERS_OF_DAY; i++) {
            outResult->prices[i] = result.prices[i];
        }
        
        return true;
    }
    
    /**
     * Načte zítřejší spotové ceny a přidá je do dvoudenní struktury
     * @param provider Poskytovatel cen
     * @param outResult Pointer na dvoudenní strukturu (musí již obsahovat dnešní data)
     * @return true pokud se podařilo načíst zítřejší data
     */
    bool loadTomorrowPrices(ElectricityPriceProvider_t provider, ElectricityPriceTwoDays_t* outResult)
    {
        if (!outResult || outResult->updated == 0) return false;
        
        // Načteme zítřejší data
        ElectricityPriceResult_t result = getElectricityPriceInternal(provider, true);
        
        if (result.updated == 0) {
            LOGD("Tomorrow electricity prices not yet available");
            return false;
        }
        
        // Zkopírujeme zítřejší ceny na pozice 96-191
        for (int i = 0; i < QUARTERS_OF_DAY; i++) {
            outResult->prices[QUARTERS_OF_DAY + i] = result.prices[i];
        }
        outResult->hasTomorrowData = true;
        LOGD("Tomorrow electricity prices loaded successfully");
        
        return true;
    }
    
    /**
     * Načte spotové ceny pouze pro jeden den (původní API)
     */
    ElectricityPriceResult_t getElectricityPrice(ElectricityPriceProvider_t provider, bool tomorrow)
    {
        return getElectricityPriceInternal(provider, tomorrow);
    }

private:
    ElectricityPriceResult_t getElectricityPriceInternal(ElectricityPriceProvider_t provider, bool tomorrow)
    {
        ElectricityPriceResult_t result;
        result.updated = 0;
        memset(result.currency, 0, CURRENCY_LENGTH);
        memset(result.energyUnit, 0, ENERGY_UNIT_LENGTH);
        LOGD("Loading electricity price data for provider %s", getProviderCaption(provider).c_str());
        if(provider == NONE)
        {
            LOGD("No electricity price provider selected.");
            return result;
        }
        for (int r = 0; r < 5; r++)
        {
            switch (provider)
            {
            case OTE_CZ:
                result = OTE_CZ_API().reloadData(tomorrow);
                break;
            case OTK_SK:
                result = OTK_SK_API().reloadData(tomorrow);
                break;
            case PSE_PL:
                result = PSE_PL_API().reloadData(tomorrow);
                break;
            case AWATTAR_DE:
                result = Awattar_API().reloadData("de", tomorrow);
                break;
            case AWATTAR_AT:
                result = Awattar_API().reloadData("at", tomorrow);
                break;
            case ELPRIS_SE1:
                result = Elpris_API().reloadData("SE1", tomorrow);
                break;
            case ELPRIS_SE2:
                result = Elpris_API().reloadData("SE2", tomorrow);
                break;
            case ELPRIS_SE3:
                result = Elpris_API().reloadData("SE3", tomorrow);
                break;
            case ELPRIS_SE4:
                result = Elpris_API().reloadData("SE4", tomorrow);
                break;
            case SPOT_HINTA_FI_DK1:
                result = SpotHinta_API().reloadData("DK1", tomorrow);
                break;
            case SPOT_HINTA_FI_DK2:
                result = SpotHinta_API().reloadData("DK2", tomorrow);
                break;
            case SPOT_HINTA_FI_EE:
                result = SpotHinta_API().reloadData("EE", tomorrow);
                break;
            case SPOT_HINTA_FIFI:
                result = SpotHinta_API().reloadData("FI", tomorrow);
                break;
            case SPOT_HINTA_FI_LT:
                result = SpotHinta_API().reloadData("LT", tomorrow);
                break;
            case SPOT_HINTA_FI_LV:
                result = SpotHinta_API().reloadData("LV", tomorrow);
                break;
            case SPOT_HINTA_FI_NO1:
                result = SpotHinta_API().reloadData("NO1", tomorrow);
                break;
            case SPOT_HINTA_FI_NO2:
                result = SpotHinta_API().reloadData("NO2", tomorrow);
                break;
            case SPOT_HINTA_FI_NO3:
                result = SpotHinta_API().reloadData("NO3", tomorrow);
                break;
            case SPOT_HINTA_FI_NO4:
                result = SpotHinta_API().reloadData("NO4", tomorrow);
                break;
            case SPOT_HINTA_FI_NO5:
                result = SpotHinta_API().reloadData("NO5", tomorrow);
                break;
            case NORDPOOL_AT:
                result = NordPoolAPI().reloadData("AT", tomorrow);
                break;
            case NORDPOOL_BE:
                result = NordPoolAPI().reloadData("BE", tomorrow);
                break;
            case NORDPOOL_FR:
                result = NordPoolAPI().reloadData("FR", tomorrow);
                break;
            case NORDPOOL_GER:
                result = NordPoolAPI().reloadData("GER", tomorrow);
                break;
            case NORDPOOL_NL:
                result = NordPoolAPI().reloadData("NL", tomorrow);
                break;
            case NORDPOOL_PL:
                result = NordPoolAPI().reloadData("PL", tomorrow);
                break;
            }

            if (result.updated > 0)
            {
                break;
            }
            delay(5000 * (r + 1)); // wait before retry
        }

        strcpy(result.currency, getCurrency(provider).c_str());
        strcpy(result.energyUnit, "kWh");
        result.scaleMaxValue = getRecommendedScaleMaxValue(provider);
        result.pricesHorizontalSeparatorStep = getHorizontalSeparatorStep(provider);
        for (int i = 0; i < QUARTERS_OF_DAY; i++)
        {
            result.prices[i].priceLevel = getPriceLevel(result.prices[i].electricityPrice);
        }
        return result;
    }

public:
    ElectricityPriceProvider_t getStoredElectricityPriceProvider()
    {
        FlashGuard guard("SpotProvider:get");
        if (!guard.isLocked()) {
            LOGE("Failed to lock NVS mutex for reading provider");
            return NONE;
        }
        
        Preferences preferences;
        preferences.begin("spot", true);
        int provider = preferences.getInt("provider", NONE);
        preferences.end();
        return (ElectricityPriceProvider_t)provider;
    }

    void storeElectricityPriceProvider(ElectricityPriceProvider_t provider)
    {
        FlashGuard guard("SpotProvider:set");
        if (!guard.isLocked()) {
            LOGE("Failed to lock NVS mutex for storing provider");
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
            LOGE("Failed to lock NVS mutex for reading timezone");
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
            LOGE("Failed to lock NVS mutex for storing timezone");
            return;
        }
        
        Preferences preferences;
        preferences.begin("spot", false);
        preferences.putString("tz", timeZone);
        preferences.end();
    }

    String getProviderCaption(ElectricityPriceProvider_t provider)
    {
        switch (provider)
        {
        case NONE:
            return "None";
        case OTE_CZ:
            return "OTE Czech Republic";
        case OTK_SK:
            return "OTK Slovakia";
        case PSE_PL:
            return "PSE Poland";
        case AWATTAR_DE:
            return "Awattar Germany";
        case AWATTAR_AT:
            return "Awattar Austria";
        case ELPRIS_SE1:
            return "Elpris Sweden SE1";
        case ELPRIS_SE2:
            return "Elpris Sweden SE2";
        case ELPRIS_SE3:
            return "Elpris Sweden SE3";
        case ELPRIS_SE4:
            return "Elpris Sweden SE4";
        case SPOT_HINTA_FI_DK1:
            return "SpotHinta Finland DK1";
        case SPOT_HINTA_FI_DK2:
            return "SpotHinta Finland DK2";
        case SPOT_HINTA_FI_EE:
            return "SpotHinta Finland EE";
        case SPOT_HINTA_FIFI:
            return "SpotHinta Finland FI";
        case SPOT_HINTA_FI_LT:
            return "SpotHinta Finland LT";
        case SPOT_HINTA_FI_LV:
            return "SpotHinta Finland LV";
        case SPOT_HINTA_FI_NO1:
            return "SpotHinta Finland NO1";
        case SPOT_HINTA_FI_NO2:
            return "SpotHinta Finland NO2";
        case SPOT_HINTA_FI_NO3:
            return "SpotHinta Finland NO3";
        case SPOT_HINTA_FI_NO4:
            return "SpotHinta Finland NO4";
        case SPOT_HINTA_FI_NO5:
            return "SpotHinta Finland NO5";
        case NORDPOOL_AT:
            return "NordPool Austria";
        case NORDPOOL_BE:
            return "NordPool Belgium";
        case NORDPOOL_FR:
            return "NordPool France";
        case NORDPOOL_GER:
            return "NordPool Germany";
        case NORDPOOL_NL:
            return "NordPool Netherlands";
        case NORDPOOL_PL:
            return "NordPool Poland";
        default:
            return "Unknown";
        }
    }

private:
    String getCurrency(ElectricityPriceProvider_t provider)
    {
        switch (provider)
        {
        case OTE_CZ:
            return TR(STR_CURRENCY_CZK);
        case PSE_PL:
            return TR(STR_CURRENCY_PLN_GR);
        case ELPRIS_SE1:
        case ELPRIS_SE2:
        case ELPRIS_SE3:
        case ELPRIS_SE4:
            return TR(STR_CURRENCY_SEK_ORE);
        default:
            return TR(STR_CURRENCY_EUR_CENT);
        }
    }

    float getRecommendedScaleMaxValue(ElectricityPriceProvider_t provider)
    {
        switch (provider)
        {
        case OTE_CZ:
            return 10.0f;
        case PSE_PL:
            return 100.0f;
        case ELPRIS_SE1:
        case ELPRIS_SE2:
        case ELPRIS_SE3:
        case ELPRIS_SE4:
            return 10.0f;
        default:
            return 30.0f;
        }
    }

    int getHorizontalSeparatorStep(ElectricityPriceProvider_t provider)
    {
        switch (provider)
        {
        case OTE_CZ:
            return 1;
        case PSE_PL:
            return 25;
        case ELPRIS_SE1:
        case ELPRIS_SE2:
        case ELPRIS_SE3:
        case ELPRIS_SE4:
            return 1;
        default:
            return 2;
        }
    }

    float getCheapPriceThreshold(ElectricityPriceProvider_t provider)
    {
        switch (provider)
        {
        case OTE_CZ:
            return 1.5f;
        case PSE_PL:
            return 50.0f;
        case ELPRIS_SE1:
        case ELPRIS_SE2:
        case ELPRIS_SE3:
        case ELPRIS_SE4:
            return 6.0f;
        default:
            return 6.0f;
        }
    }

    float getExpensivePriceThreshold(ElectricityPriceProvider_t provider)
    {
        switch (provider)
        {
        case OTE_CZ:
            return 3.0f;
        case PSE_PL:
            return 45.0f;
        case ELPRIS_SE1:
        case ELPRIS_SE2:
        case ELPRIS_SE3:
        case ELPRIS_SE4:
            return 13.0f;
        default:
            return 12.0f;
        }
    }

    PriceLevel_t getPriceLevel(float price)
    {
        if (price < 0.0f)
        {
            return PRICE_LEVEL_NEGATIVE;
        }
        else if (price <= getCheapPriceThreshold(getStoredElectricityPriceProvider()))
        {
            return PRICE_LEVEL_CHEAP;
        }
        else if (price >= getExpensivePriceThreshold(getStoredElectricityPriceProvider()))
        {
            return PRICE_LEVEL_EXPENSIVE;
        }
        else
        {
            return PRICE_LEVEL_MEDIUM;
        }
    }
};