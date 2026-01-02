#pragma once

#include <Arduino.h>
#include <RemoteLogger.hpp>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>

/**
 * Loader pro směnné kurzy z Frankfurter API (ECB data)
 * https://api.frankfurter.app/
 * 
 * Kurzy se aktualizují denně a jsou cachovány.
 */
class ExchangeRateLoader
{
private:
    // Cached kurzy (EUR -> měna)
    float rateCZK = 25.0f;   // Záložní hodnoty
    float ratePLN = 4.3f;
    float rateSEK = 11.5f;
    float rateNOK = 11.5f;
    float rateDKK = 7.45f;
    float rateHUF = 400.0f;
    float rateBGN = 1.96f;   // Fixní kurz k EUR
    float rateRON = 5.0f;
    float rateCHF = 0.95f;
    float rateGBP = 0.85f;
    
    // Čas poslední aktualizace
    time_t lastUpdate = 0;
    
    // Interval aktualizace (24 hodin)
    static const unsigned long UPDATE_INTERVAL = 24 * 60 * 60;
    
    ExchangeRateLoader() {}

public:
    // Singleton - použití Meyers' Singleton pattern (thread-safe v C++11+)
    static ExchangeRateLoader* getInstance()
    {
        static ExchangeRateLoader instance;
        return &instance;
    }
    
    /**
     * Načte aktuální kurzy z API (pokud je potřeba)
     * @return true pokud se podařilo načíst nebo jsou kurzy aktuální
     */
    bool updateRatesIfNeeded()
    {
        time_t now = time(NULL);
        
        // Kontrola, zda je potřeba aktualizovat
        if (lastUpdate > 0 && (now - lastUpdate) < UPDATE_INTERVAL) {
            LOGD("ExchangeRates: Using cached rates (age: %ld seconds)", (long)(now - lastUpdate));
            return true;
        }
        
        return fetchRates();
    }
    
    /**
     * Vynutí načtení kurzů z API
     */
    bool fetchRates()
    {
        LOGD("ExchangeRates: Fetching rates from Frankfurter API");
        
        // Alokujeme vše na heapu pro minimalizaci stack usage
        WiFiClientSecure *client = new WiFiClientSecure;
        HTTPClient *https = new HTTPClient;
        
        if (!client || !https) {
            LOGD("ExchangeRates: Failed to allocate client/https");
            if (client) delete client;
            if (https) delete https;
            return false;
        }
        
        client->setInsecure();
        
        bool success = false;
        const char* url = "https://api.frankfurter.app/latest?from=EUR&to=CZK,PLN,SEK,NOK,DKK,HUF,BGN,RON,CHF,GBP";
        
        if (https->begin(*client, url))
        {
            int httpCode = https->GET();
            
            if (httpCode == HTTP_CODE_OK)
            {
                // Parsujeme přímo ze streamu
                WiFiClient* stream = https->getStreamPtr();
                
                // Alokujeme JSON document na heapu
                DynamicJsonDocument* doc = new DynamicJsonDocument(512);
                if (doc) {
                    DeserializationError error = deserializeJson(*doc, *stream);
                    
                    if (!error)
                    {
                        JsonObject rates = (*doc)["rates"];
                        
                        if (!rates.isNull()) {
                            if (rates.containsKey("CZK")) rateCZK = rates["CZK"].as<float>();
                            if (rates.containsKey("PLN")) ratePLN = rates["PLN"].as<float>();
                            if (rates.containsKey("SEK")) rateSEK = rates["SEK"].as<float>();
                            if (rates.containsKey("NOK")) rateNOK = rates["NOK"].as<float>();
                            if (rates.containsKey("DKK")) rateDKK = rates["DKK"].as<float>();
                            if (rates.containsKey("HUF")) rateHUF = rates["HUF"].as<float>();
                            if (rates.containsKey("BGN")) rateBGN = rates["BGN"].as<float>();
                            if (rates.containsKey("RON")) rateRON = rates["RON"].as<float>();
                            if (rates.containsKey("CHF")) rateCHF = rates["CHF"].as<float>();
                            if (rates.containsKey("GBP")) rateGBP = rates["GBP"].as<float>();
                            
                            lastUpdate = time(NULL);
                            success = true;
                            
                            LOGD("ExchangeRates: Updated - CZK=%.2f, PLN=%.2f, SEK=%.2f, NOK=%.2f, DKK=%.2f",
                                 rateCZK, ratePLN, rateSEK, rateNOK, rateDKK);
                        }
                        else
                        {
                            LOGD("ExchangeRates: rates object is null");
                        }
                    }
                    else
                    {
                        LOGD("ExchangeRates: JSON parse error: %s", error.c_str());
                    }
                    
                    delete doc;
                }
            }
            else
            {
                LOGD("ExchangeRates: HTTP error %d", httpCode);
            }
            
            https->end();
        }
        else
        {
            LOGD("ExchangeRates: Failed to connect to API");
        }
        
        // Cleanup
        client->stop();
        delete https;
        delete client;
        
        yield();
        
        return success;
    }
    
    // Gettery pro jednotlivé kurzy
    float getCZK() const { return rateCZK; }
    float getPLN() const { return ratePLN; }
    float getSEK() const { return rateSEK; }
    float getNOK() const { return rateNOK; }
    float getDKK() const { return rateDKK; }
    float getHUF() const { return rateHUF; }
    float getBGN() const { return rateBGN; }
    float getRON() const { return rateRON; }
    float getCHF() const { return rateCHF; }
    float getGBP() const { return rateGBP; }
    
    /**
     * Vrátí kurz pro danou měnu
     * @param currencyCode Kód měny (CZK, PLN, SEK, ...)
     * @return Kurz EUR -> měna, nebo 1.0 pro EUR
     */
    float getRate(const char* currencyCode) const
    {
        if (strcmp(currencyCode, "CZK") == 0) return rateCZK;
        if (strcmp(currencyCode, "PLN") == 0) return ratePLN;
        if (strcmp(currencyCode, "SEK") == 0) return rateSEK;
        if (strcmp(currencyCode, "NOK") == 0) return rateNOK;
        if (strcmp(currencyCode, "DKK") == 0) return rateDKK;
        if (strcmp(currencyCode, "HUF") == 0) return rateHUF;
        if (strcmp(currencyCode, "BGN") == 0) return rateBGN;
        if (strcmp(currencyCode, "RON") == 0) return rateRON;
        if (strcmp(currencyCode, "CHF") == 0) return rateCHF;
        if (strcmp(currencyCode, "GBP") == 0) return rateGBP;
        return 1.0f; // EUR nebo neznámá měna
    }
    
    /**
     * Vrátí čas poslední aktualizace
     */
    time_t getLastUpdateTime() const { return lastUpdate; }
    
    /**
     * Kontrola, zda byly kurzy někdy načteny
     */
    bool hasValidRates() const { return lastUpdate > 0; }
    
    // Zabránění kopírování
    ExchangeRateLoader(const ExchangeRateLoader&) = delete;
    ExchangeRateLoader& operator=(const ExchangeRateLoader&) = delete;
};
