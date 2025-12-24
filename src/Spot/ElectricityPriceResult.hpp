#pragma once

#include <Arduino.h>

#define QUARTERS_OF_DAY 96
#define QUARTERS_TWO_DAYS 192  // 96 quarters today + 96 quarters tomorrow

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

#define CURRENCY_LENGTH 12
#define ENERGY_UNIT_LENGTH 8

// Základní struktura pro jeden den (96 čtvrthodin) - používá se v API providerech
typedef struct ElectricityPriceResult
{
    time_t updated;
    ElectricityPriceItem_t prices[QUARTERS_OF_DAY];  // Only today (96 quarters)
    char currency[CURRENCY_LENGTH];
    char energyUnit[ENERGY_UNIT_LENGTH];
    float scaleMaxValue;
    int pricesHorizontalSeparatorStep;
} ElectricityPriceResult_t;

// Rozšířená struktura pro dva dny - alokuje se v PSRAM
typedef struct ElectricityPriceTwoDays
{
    time_t updated;
    ElectricityPriceItem_t prices[QUARTERS_TWO_DAYS];  // Today + tomorrow (192 quarters)
    bool hasTomorrowData;
    char currency[CURRENCY_LENGTH];
    char energyUnit[ENERGY_UNIT_LENGTH];
    float scaleMaxValue;
    int pricesHorizontalSeparatorStep;
} ElectricityPriceTwoDays_t;

typedef struct CurrentPrice
{
    float price;
    String currency;
} CurrentPrice_t;

float getTotalPrice(ElectricityPriceItem_t item);
ElectricityPriceItem_t getQuarterElectricityPrice(const ElectricityPriceResult_t& result, int quarter);
ElectricityPriceItem_t getCurrentQuarterElectricityPrice(const ElectricityPriceResult_t& result);
ElectricityPriceItem_t getMinimumElectricityPrice(const ElectricityPriceResult_t& result);
ElectricityPriceItem_t getMaximumElectricityPrice(const ElectricityPriceResult_t& result);
ElectricityPriceItem_t getAverageElectricityPrice(const ElectricityPriceResult_t& result);
int getMinimumQuarterElectricityPrice(const ElectricityPriceResult_t& result);
int getMaximumQuarterElectricityPrice(const ElectricityPriceResult_t& result);
int getCurrentQuarterPriceRank(const ElectricityPriceResult_t& result);
int getPriceRank(const ElectricityPriceResult_t& result, float price);

// Funkce pro dvoudenní strukturu
ElectricityPriceItem_t getQuarterElectricityPriceTwoDays(const ElectricityPriceTwoDays_t& result, int quarter);