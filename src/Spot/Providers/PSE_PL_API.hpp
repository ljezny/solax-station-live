#ifndef PSE_PL_API_h
#define PSE_PL_API_h

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <math.h>
#include <time.h>
#include "../ElectricityPriceResult.hpp"

class PSE_PL_API
{
public:
  ElectricityPriceResult_t reloadData(bool tomorrow)
  {
    ElectricityPriceResult_t result;
    result.updated = 0;

    time_t timestamp = time(NULL);
    char buf[100];

    tm *localTime = localtime((time_t *)&timestamp);
    if (tomorrow)
    {
      localTime->tm_mday++;
    }
    strftime(buf, sizeof(buf), "%Y-%m-%d", localTime);
    String fromDay = String(buf);

    WiFiClientSecure *client = new WiFiClientSecure;
    if (client)
    {
      client->setInsecure();
      {
        // https://api.raporty.pse.pl/api/rce-pln?$filter=business_date%20eq%20%272024-11-14%27

        String url = "https://api.raporty.pse.pl/api/rce-pln?$filter=business_date%20eq%20%27" + fromDay + "%27";
        log_d("Fetching URL: %s", url.c_str());
        // Add a scoping block for HTTPClient https to make sure it is destroyed before WiFiClientSecure *client is
        HTTPClient https;
        if (https.begin(*client, url))
        {
          int httpCode = https.GET();
          // httpCode will be negative on error
          if (httpCode > 0)
          {
            // file found at server
            if (httpCode == HTTP_CODE_OK)
            {
              DynamicJsonDocument doc(12 * 1024);
              DynamicJsonDocument filter(1024);
              filter["value"][0]["rce_pln"] = true;
              DeserializationError error = deserializeJson(doc, https.getString(), DeserializationOption::Filter(filter));
              if (error == DeserializationError::Ok)
              {
                for (int i = 0; i < QUARTERS_OF_DAY; i++)
                {
                  // already in 15 minutes intervals
                  result.prices[i].electricityPrice = round(doc["value"][i]["rce_pln"].as<float>() / 1000.0f * 100.0f);
                  result.updated = time(NULL);
                }
              }
              else
              {
                log_d("deserializeJson() failed: %s", error.c_str());
              }
            }
          }
          else
          {
            log_d("ERROR: %s", https.errorToString(httpCode).c_str());
          }

          https.end();
        }
        else
        {
          log_d("Unable to connect.");
        }
      }
      client->stop();
      delete client;
    }
    else
    {
      log_d("Unable to create client.");
    }

    return result;
  }
};

#endif