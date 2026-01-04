#pragma once

/**
 * Helper functions for converting between project types and library types
 */

#include <SolarIntelligence.h>
#include "../Inverters/InverterResult.hpp"
#include "../Spot/ElectricityPriceResult.hpp"

/**
 * Convert InverterData_t to SolarBatteryState_t
 */
inline SolarBatteryState_t toBatteryState(const InverterData_t& inverterData) {
    return SolarBatteryState_t(inverterData.soc);
}

/**
 * Convert ElectricityPriceTwoDays_t to SolarPriceData_t
 */
inline SolarPriceData_t toPriceData(const ElectricityPriceTwoDays_t& prices) {
    SolarPriceData_t priceData;
    
    int count = prices.hasTomorrowData ? SI_QUARTERS_TWO_DAYS : SI_QUARTERS_PER_DAY;
    priceData.hasTomorrowData = prices.hasTomorrowData;
    priceData.updated = prices.updated;
    
    strncpy(priceData.currency, prices.currency, SI_CURRENCY_LENGTH - 1);
    priceData.currency[SI_CURRENCY_LENGTH - 1] = '\0';
    
    for (int i = 0; i < count && i < SI_QUARTERS_TWO_DAYS; i++) {
        priceData.prices[i] = prices.prices[i].electricityPrice;
    }
    
    return priceData;
}
