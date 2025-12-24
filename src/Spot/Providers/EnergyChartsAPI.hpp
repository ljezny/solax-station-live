#pragma once

#include <WiFi.h>
#include <RemoteLogger.hpp>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <math.h>
#include <time.h>
#include "../ElectricityPriceResult.hpp"

/**
 * Energy Charts API Provider
 * API: https://api.energy-charts.info/price
 * Returns day-ahead spot market prices in EUR/MWh
 * 
 * Supported bidding zones:
 * AT, BE, BG, CH, CZ, DE-LU, DE-AT-LU, DK1, DK2, EE, ES, FI, FR, GR, HR, HU,
 * IT-Calabria, IT-Centre-North, IT-Centre-South, IT-North, IT-SACOAC, IT-SACODC,
 * IT-Sardinia, IT-Sicily, IT-South, LT, LV, ME, NL, NO1, NO2, NO2NSL, NO3, NO4, NO5,
 * PL, PT, RO, RS, SE1, SE2, SE3, SE4, SI, SK
 */
class EnergyChartsAPI
{
public:
    /**
     * Načte spotové ceny pro danou bidding zone
     * @param biddingZone Kód bidding zone (např. "CZ", "DE-LU", "AT")
     * @param tomorrow True pro zítřejší ceny, false pro dnešní
     * @param outResult Reference na strukturu pro uložení výsledku
     * @return true pokud se podařilo načíst data
     */
    bool reloadData(const char* biddingZone, bool tomorrow, ElectricityPriceResult_t& outResult)
    {
        outResult.updated = 0;
        memset(outResult.prices, 0, sizeof(outResult.prices));
        
        // Alokujeme vše na heapu pro minimalizaci stack usage
        WiFiClientSecure *client = new WiFiClientSecure;
        HTTPClient *https = new HTTPClient;
        
        if (!client || !https) {
            LOGD("EnergyChartsAPI: Failed to allocate client/https");
            if (client) delete client;
            if (https) delete https;
            return false;
        }
        
        client->setInsecure();
        
        // Sestavení URL
        char url[128];
        time_t now = time(NULL);
        tm *t = localtime(&now);
        if (tomorrow) {
            t->tm_mday++;
        }
        t->tm_hour = 0;
        t->tm_min = 0;
        t->tm_sec = 0;
        mktime(t);
        
        snprintf(url, sizeof(url), "https://api.energy-charts.info/price?bzn=%s&start=%04d-%02d-%02d",
                 biddingZone, t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);
        
        LOGD("EnergyChartsAPI: Fetching URL: %s", url);
        
        bool success = false;
        
        if (https->begin(*client, url))
        {
            int httpCode = https->GET();
            
            if (httpCode == HTTP_CODE_OK)
            {
                WiFiClient* stream = https->getStreamPtr();
                int contentLength = https->getSize();
                LOGD("EnergyChartsAPI: Response length: %d", contentLength);
                
                // Alokujeme JSON document na heapu
                DynamicJsonDocument* doc = new DynamicJsonDocument(4096);
                if (doc) {
                    StaticJsonDocument<128> filter;
                    filter["unix_seconds"] = true;
                    filter["price"] = true;
                    
                    DeserializationError error = deserializeJson(*doc, *stream, DeserializationOption::Filter(filter));
                    
                    if (!error)
                    {
                        JsonArray prices = (*doc)["price"];
                        
                        if (prices.size() > 0)
                        {
                            int priceCount = prices.size();
                            LOGD("EnergyChartsAPI: Got %d quarter-hourly prices", priceCount);
                            
                            for (int i = 0; i < QUARTERS_OF_DAY && i < priceCount; i++)
                            {
                                JsonVariant priceValue = prices[i];
                                if (!priceValue.isNull()) {
                                    // Uložíme surové EUR/MWh, konverze se dělá v ElectricityPriceLoader
                                    outResult.prices[i].electricityPrice = priceValue.as<float>();
                                }
                            }
                            
                            outResult.updated = time(NULL);
                            success = true;
                            LOGD("EnergyChartsAPI: Successfully loaded prices for %s", biddingZone);
                        }
                        else
                        {
                            LOGD("EnergyChartsAPI: No price data available");
                        }
                    }
                    else
                    {
                        LOGD("EnergyChartsAPI: JSON parse error: %s", error.c_str());
                    }
                    
                    delete doc;
                }
                else
                {
                    LOGD("EnergyChartsAPI: Failed to allocate JSON document");
                }
            }
            else
            {
                LOGD("EnergyChartsAPI: HTTP error %d", httpCode);
            }
            
            https->end();
        }
        else
        {
            LOGD("EnergyChartsAPI: Unable to connect");
        }
        
        // Cleanup
        client->stop();
        delete https;
        delete client;
        
        yield(); // Dáme šanci RTOS
        
        return success;
    }
};
