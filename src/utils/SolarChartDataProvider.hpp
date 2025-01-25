#pragma once

typedef struct SolarChartDataItem
{
    long timestampMS;
    int samples = 0;
    float pvPower = 0;
    float loadPower = 0;
    int soc = 0;
} SolarChartDataItem_t;

#define CHART_SAMPLE_INTERVAL_MS (3 * 60 * 1000)
#define CHART_SAMPLES_PER_DAY (24 * 60 / 3)

class SolarChartDataProvider
{
private:
    SolarChartDataItem data[CHART_SAMPLES_PER_DAY];

    void shiftData() {
        for(int i = CHART_SAMPLES_PER_DAY - 1; i > 0; i--) {
            data[i] = data[i - 1];
        }
    }

public:
    void addSample(long timestampMS, float pvPower, float loadPower, int soc) {
        if(timestampMS > (data[0].timestampMS + CHART_SAMPLE_INTERVAL_MS)) {
            shiftData();
            data[0].timestampMS = timestampMS;
        }
        data[0].pvPower = ((pvPower * data[0].samples) + pvPower) / (data[0].samples + 1);
        data[0].loadPower = ((loadPower * data[0].samples) + loadPower) / (data[0].samples + 1);
        data[0].soc = ((soc * data[0].samples) + soc) / (data[0].samples + 1);
        data[0].samples++;
    }

    SolarChartDataItem *getData() {
        return data;
    }
};
