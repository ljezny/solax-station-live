#pragma once

#include <Arduino.h>
#include "ElectricityPriceResult.hpp"

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

class ElectricityPriceLoader
{
public:
    ElectricityPriceResult_t getElectricityPrice(ElectricityPriceProvider_t provider, bool tommorow)
    {
        ElectricityPriceResult_t result;
        result.updated = 0;
        memset(result.currency, 0, CURRENCY_LENGTH);
        memset(result.energyUnit, 0, ENERGY_UNIT_LENGTH);
        log_d("Loading electricity price data for provider %s", getProviderCaption(provider).c_str());
        if(provider == NONE)
        {
            log_d("No electricity price provider selected.");
            return result;
        }
        for (int r = 0; r < 5; r++)
        {
            switch (provider)
            {
            case OTE_CZ:
                result = OTE_CZ_API().reloadData(tommorow);
                break;
            case OTK_SK:
                result = OTK_SK_API().reloadData(tommorow);
                break;
            case PSE_PL:
                result = PSE_PL_API().reloadData(tommorow);
                break;
            case AWATTAR_DE:
                result = Awattar_API().reloadData("de", tommorow);
                break;
            case AWATTAR_AT:
                result = Awattar_API().reloadData("at", tommorow);
                break;
            case ELPRIS_SE1:
                result = Elpris_API().reloadData("SE1", tommorow);
                break;
            case ELPRIS_SE2:
                result = Elpris_API().reloadData("SE2", tommorow);
                break;
            case ELPRIS_SE3:
                result = Elpris_API().reloadData("SE3", tommorow);
                break;
            case ELPRIS_SE4:
                result = Elpris_API().reloadData("SE4", tommorow);
                break;
            case SPOT_HINTA_FI_DK1:
                result = SpotHinta_API().reloadData("DK1", tommorow);
                break;
            case SPOT_HINTA_FI_DK2:
                result = SpotHinta_API().reloadData("DK2", tommorow);
                break;
            case SPOT_HINTA_FI_EE:
                result = SpotHinta_API().reloadData("EE", tommorow);
                break;
            case SPOT_HINTA_FIFI:
                result = SpotHinta_API().reloadData("FI", tommorow);
                break;
            case SPOT_HINTA_FI_LT:
                result = SpotHinta_API().reloadData("LT", tommorow);
                break;
            case SPOT_HINTA_FI_LV:
                result = SpotHinta_API().reloadData("LV", tommorow);
                break;
            case SPOT_HINTA_FI_NO1:
                result = SpotHinta_API().reloadData("NO1", tommorow);
                break;
            case SPOT_HINTA_FI_NO2:
                result = SpotHinta_API().reloadData("NO2", tommorow);
                break;
            case SPOT_HINTA_FI_NO3:
                result = SpotHinta_API().reloadData("NO3", tommorow);
                break;
            case SPOT_HINTA_FI_NO4:
                result = SpotHinta_API().reloadData("NO4", tommorow);
                break;
            case SPOT_HINTA_FI_NO5:
                result = SpotHinta_API().reloadData("NO5", tommorow);
                break;
            case NORDPOOL_AT:
                result = NordPoolAPI().reloadData("AT", tommorow);
                break;
            case NORDPOOL_BE:
                result = NordPoolAPI().reloadData("BE", tommorow);
                break;
            case NORDPOOL_FR:
                result = NordPoolAPI().reloadData("FR", tommorow);
                break;
            case NORDPOOL_GER:
                result = NordPoolAPI().reloadData("GER", tommorow);
                break;
            case NORDPOOL_NL:
                result = NordPoolAPI().reloadData("NL", tommorow);
                break;
            case NORDPOOL_PL:
                result = NordPoolAPI().reloadData("PL", tommorow);
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

    ElectricityPriceProvider_t getStoredElectricityPriceProvider()
    {
        Preferences preferences;
        preferences.begin("spot", true);
        int provider = preferences.getInt("provider", NONE);
        preferences.end();
        return (ElectricityPriceProvider_t)provider;
    }

    void storeElectricityPriceProvider(ElectricityPriceProvider_t provider)
    {
        Preferences preferences;
        preferences.begin("spot", false);
        preferences.putInt("provider", (int)provider);
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
            return "CZK";
        case PSE_PL:
            return "gr";
        case ELPRIS_SE1:
        case ELPRIS_SE2:
        case ELPRIS_SE3:
        case ELPRIS_SE4:
            return "öre";
        default:
            return "ct";
        }
    }

    float getRecommendedScaleMaxValue(ElectricityPriceProvider_t provider)
    {
        switch (provider)
        {
        case OTE_CZ:
            return 7.0f;
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