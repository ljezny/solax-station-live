#pragma once

#include <Arduino.h>
typedef struct PowerSample
{
    uint32_t timestamp;
    int pvPower = 0;
    int soc = 0;
    int16_t batteryPower = 0;
    int16_t loadPower = 0;
    int32_t feedInPower = 0;
} PowerSample_t;

class MedianPowerSampler
{
public:
    MedianPowerSampler(int maxSamples = 5)
    {
        this->maxSamples = maxSamples;
        powerSamples = new PowerSample_t[maxSamples];
        for (int i = 0; i < maxSamples; i++)
        {
            powerSamples[i].timestamp = 0;
        }
    }
    ~MedianPowerSampler()
    {
        delete[] powerSamples;
    }

    bool hasValidSamples()
    {
        for (int i = 0; i < maxSamples; i++)
        {
            if (powerSamples[i].timestamp == 0)
            {
                return false;
            }
        }
        return true;
    }

    void resetSamples()
    {
        for (int i = 0; i < maxSamples; i++)
        {
            powerSamples[i].timestamp = 0;
        }
    }

    int getMedianPVPower()
    {
        int values[maxSamples];
        for (int i = 0; i < maxSamples; i++)
        {
            values[i] = powerSamples[i].pvPower;
        }
        std::sort(values, values + maxSamples);
        return values[maxSamples / 2];
    }

    int getSOC()
    {
        return powerSamples[0].soc;
    }

    int getMedianBatteryPower()
    {
        int values[maxSamples];
        for (int i = 0; i < maxSamples; i++)
        {
            values[i] = powerSamples[i].batteryPower;
        }
        std::sort(values, values + maxSamples);
        return values[maxSamples / 2];
    }

    int getMedianLoadPower()
    {
        int values[maxSamples];
        for (int i = 0; i < maxSamples; i++)
        {
            values[i] = powerSamples[i].loadPower;
        }
        std::sort(values, values + maxSamples);
        return values[maxSamples / 2];
    }

    int getMedianFeedInPower()
    {
        int values[maxSamples];
        for (int i = 0; i < maxSamples; i++)
        {
            values[i] = powerSamples[i].feedInPower;
        }
        std::sort(values, values + maxSamples);
        return values[maxSamples / 2];
    }

    void addPowerSample(int pvPower, int soc, int batteryPower, int loadPower, int feedInPower)
    {
        PowerSample_t sample;
        sample.timestamp = millis();
        sample.pvPower = pvPower;
        sample.soc = soc;
        sample.batteryPower = batteryPower;
        sample.loadPower = loadPower;
        sample.feedInPower = feedInPower;
        for (int i = maxSamples - 1; i > 0; i--)
        {
            powerSamples[i] = powerSamples[i - 1];
        }
        powerSamples[0] = sample;
    }

private:
    int maxSamples = 0;
    PowerSample_t *powerSamples;
};