#pragma once

#include <Arduino.h>

#define QUARTERS_OF_DAY 96

typedef enum PriceLevel {
    PRICE_LEVEL_NEGATIVE = -1,
    PRICE_LEVEL_CHEAP,
    PRICE_LEVEL_MEDIUM,
    PRICE_LEVEL_EXPENSIVE,
} PriceLevel_t;

typedef struct ElectricityPriceItem
{
    float electricityPrice;
    PriceLevel_t priceLevel;
} ElectricityPriceItem_t;

#define CURRENCY_LENGTH 4
#define ENERGY_UNIT_LENGTH 4

typedef struct ElectricityPriceResult
{
    time_t updated;
    ElectricityPriceItem_t prices[QUARTERS_OF_DAY];
    char currency[CURRENCY_LENGTH];
    char energyUnit[ENERGY_UNIT_LENGTH];
    float scaleMaxValue;
    int pricesHorizontalSeparatorStep;
} ElectricityPriceResult_t;

typedef struct CurrentPrice
{
    float price;
    String currency;
} CurrentPrice_t;

float getTotalPrice(ElectricityPriceItem_t item);
ElectricityPriceItem_t getQuarterElectricityPrice(ElectricityPriceResult_t result, int quarter);
ElectricityPriceItem_t getCurrentQuarterElectricityPrice(ElectricityPriceResult_t result);
ElectricityPriceItem_t getMinimumElectricityPrice(ElectricityPriceResult_t result);
ElectricityPriceItem_t getMaximumElectricityPrice(ElectricityPriceResult_t result);
ElectricityPriceItem_t getAverageElectricityPrice(ElectricityPriceResult_t result);
int getMinimumQuarterElectricityPrice(ElectricityPriceResult_t result);
int getMaximumQuarterElectricityPrice(ElectricityPriceResult_t result);
int getCurrentQuarterPriceRank(ElectricityPriceResult_t result);
int getPriceRank(ElectricityPriceResult_t result, float price);