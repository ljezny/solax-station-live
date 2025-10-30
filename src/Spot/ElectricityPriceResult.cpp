#include "ElectricityPriceResult.hpp"

float getTotalPrice(ElectricityPriceItem_t item)
{
    return item.electricityPrice;
}

ElectricityPriceItem_t getQuarterElectricityPrice(ElectricityPriceResult_t result, int quarter)
{
    return result.prices[quarter];
}

ElectricityPriceItem_t getCurrentQuarterElectricityPrice(ElectricityPriceResult_t result)
{
    time_t now = time(nullptr);
    struct tm *timeinfo = localtime(&now);
    return getQuarterElectricityPrice(result, timeinfo->tm_hour * 4 + timeinfo->tm_min / 15);
}

ElectricityPriceItem_t getMinimumElectricityPrice(ElectricityPriceResult_t result)
{
    ElectricityPriceItem_t min = result.prices[0];
    for (int i = 1; i < QUARTERS_OF_DAY; i++)
    {
        if (getTotalPrice(result.prices[i]) < getTotalPrice(min))
        {
            min = result.prices[i];
        }
    }
    return min;
}

ElectricityPriceItem_t getMaximumElectricityPrice(ElectricityPriceResult_t result)
{
    ElectricityPriceItem_t max = result.prices[0];
    for (int i = 1; i < QUARTERS_OF_DAY; i++)
    {
        if (getTotalPrice(result.prices[i]) > getTotalPrice(max))
        {
            max = result.prices[i];
        }
    }
    return max;
}

ElectricityPriceItem_t getAverageElectricityPrice(ElectricityPriceResult_t result)
{
    ElectricityPriceItem_t average;
    average.electricityPrice = 0;
    for (int i = 0; i < QUARTERS_OF_DAY; i++)
    {
        average.electricityPrice += result.prices[i].electricityPrice;
    }
    average.electricityPrice /= QUARTERS_OF_DAY;
    return average;
}

int getMinimumQuarterElectricityPrice(ElectricityPriceResult_t result)
{
    ElectricityPriceItem_t min = result.prices[0];
    int minIndex = 0;
    for (int i = 1; i < QUARTERS_OF_DAY; i++)
    {
        if (getTotalPrice(result.prices[i]) < getTotalPrice(min))
        {
            min = result.prices[i];
            minIndex = i;
        }
    }
    return minIndex;
}

int getMaximumQuarterElectricityPrice(ElectricityPriceResult_t result)
{
    ElectricityPriceItem_t max = result.prices[0];
    int maxIndex = 0;
    for (int i = 1; i < QUARTERS_OF_DAY; i++)
    {
        if (getTotalPrice(result.prices[i]) > getTotalPrice(max))
        {
            max = result.prices[i];
            maxIndex = i;
        }
    }
    return maxIndex;
}

int getPriceRank(ElectricityPriceResult_t result, float price)
{
    int rank = 1;
    for (int i = 0; i < QUARTERS_OF_DAY; i++)
    {
        float quarterPrice = getTotalPrice(getQuarterElectricityPrice(result, i));
        // count all prices lower than current, when same price is found, ranks same prices according to hour of day
        if (quarterPrice < price)
        {
            rank++;
        }
    }
    return rank;
}

int getCurrentQuarterPriceRank(ElectricityPriceResult_t result)
{
    time_t now = time(nullptr);
    struct tm *timeinfo = localtime(&now);
    return getPriceRank(result, timeinfo->tm_hour * 4 + timeinfo->tm_min / 15);
}