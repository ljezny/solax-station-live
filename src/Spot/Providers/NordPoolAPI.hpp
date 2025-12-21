#pragma once

#include <WiFi.h>
#include <RemoteLogger.hpp>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <math.h>
#include <time.h>
#include "../ElectricityPriceResult.hpp"

class NordPoolAPI
{
public:
    ElectricityPriceResult_t reloadData(String domain, bool tomorrow)
    {
        ElectricityPriceResult_t result;
        result.updated = 0;

        time_t timestamp = time(NULL);
        tm *localTime = localtime((time_t *)&timestamp);
        if (tomorrow)
        {
            localTime->tm_mday++;
        }
        char buf[100];
        strftime(buf, sizeof(buf), "%Y-%m-%d", localTime);

        WiFiClientSecure *client = new WiFiClientSecure;
        if (client)
        {
            client->setInsecure();
            client->setTimeout(20);
            {
                String url = "https://dataportal-api.nordpoolgroup.com/api/DayAheadPrices?date=" + String(buf) + "&market=DayAhead&deliveryArea=" + domain + "&currency=EUR";
                
                // Add a scoping block for HTTPClient https to make sure it is destroyed before WiFiClientSecure *client is
                HTTPClient https;
                if (https.begin(*client, url))
                {
                    int httpCode = https.GET();
                    // httpCode will be negative on error
                    if (httpCode > 0)
                    {
                        // file found at server
                        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY)
                        {
                            String payload = https.getString();
                            DynamicJsonDocument filter(1024);
                            filter["multiAreaEntries"][0]["entryPerArea"][domain] = true;

                            DynamicJsonDocument doc(12 * 1024);
                            deserializeJson(doc, payload);
                            for (int i = 0; i < QUARTERS_OF_DAY; i++)
                            {
                                if (doc["multiAreaEntries"][0]["entryPerArea"][domain].isNull())
                                {
                                    break;
                                }
                                result.prices[i].electricityPrice = doc["multiAreaEntries"][i]["entryPerArea"][domain].as<float>() / 10.0f; // Convert from EUR/MWh to cEUR/kWh
                                result.updated = time(NULL);
                            }
                            
                            LOGD("Response payload: %s", payload.c_str());
                        }
                    }
                    else
                    {
                        LOGD("ERROR: %s", https.errorToString(httpCode).c_str());
                    }

                    https.end();
                }
                else
                {
                    LOGD("Unable to connect to URL: %s", url.c_str());
                }
            }
            client->stop();
            delete client;
        }

        return result;
    }
};
