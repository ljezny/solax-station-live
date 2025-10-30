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
    OTE_CZ = 0,
    OTK_SK = 1,
    PSE_PL = 2,
    AWATTAR_DE = 3,
    AWATTAR_AT = 4,
    ELPRIS_SE1 = 5,
    ELPRIS_SE2 = 6,
    ELPRIS_SE3 = 7,
    ELPRIS_SE4 = 8,
    SPOT_HINTA_FI_DK1 = 9,
    SPOT_HINTA_FI_DK2 = 10,
    SPOT_HINTA_FI_EE = 11,
    SPOT_HINTA_FIFI = 12,
    SPOT_HINTA_FI_LT = 13,
    SPOT_HINTA_FI_LV = 14,
    SPOT_HINTA_FI_NO1 = 15,
    SPOT_HINTA_FI_NO2 = 16,
    SPOT_HINTA_FI_NO3 = 17,
    SPOT_HINTA_FI_NO4 = 18,
    SPOT_HINTA_FI_NO5 = 19,
    NORDPOOL_AT = 20,
    NORDPOOL_BE = 21,
    NORDPOOL_FR = 22,
    NORDPOOL_GER = 23,
    NORDPOOL_NL = 24,
    NORDPOOL_PL = 25
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

            if(result.updated > 0)
            {
                break;
            }
            delay(5000 * (r + 1)); // wait before retry
        }


        strcpy(result.currency, getCurrency(provider).c_str());
        strcpy(result.energyUnit, "kWh");
        result.scaleMaxValue = getRecommendedScaleMaxValue(provider);
        result.pricesHorizontalSeparatorStep = getHorizontalSeparatorStep(provider);

        return result;
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
            return 10;
        case ELPRIS_SE1:
        case ELPRIS_SE2:
        case ELPRIS_SE3:
        case ELPRIS_SE4:
            return 1;
        default:
            return 2;
        }
    }
};