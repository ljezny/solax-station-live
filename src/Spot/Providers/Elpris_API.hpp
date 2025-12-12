#pragma once

#include <WiFi.h>
#include "../../utils/RemoteLogger.hpp"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>
#include <time.h>
#include "../ElectricityPriceResult.hpp"

// Price field key we are interested in API response. For example from https://www.elprisetjustnu.se/api/v1/prices/2024/05-28_SE3.json
static const char priceFieldKey[] = "SEK_per_kWh";

class Elpris_API
{
public:
  ElectricityPriceResult_t reloadData(String domain, bool tomorrow)
  {
    ElectricityPriceResult_t result;
    // https://www.elprisetjustnu.se/api/v1/prices/2024/05-28_SE3.json

    time_t timestamp = time(NULL);
    tm *t = localtime((time_t *)&timestamp);
    if (tomorrow)
    {
      t->tm_mday++;
    }
    char month[6];
    sprintf(month, "%02d", t->tm_mon + 1);
    char day[6];
    sprintf(day, "%02d", t->tm_mday);

    WiFiClientSecure *client = new WiFiClientSecure;
    if (client)
    {
      client->setInsecure();
      {
        String url = "https://www.elprisetjustnu.se/api/v1/prices/" + String(t->tm_year + 1900) + "/" + String(month) + "-" + String(day) + "_" + domain + ".json";
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
              String payload = https.getString();
              DynamicJsonDocument doc(12 * 1024);
              DynamicJsonDocument filter(1024);
              filter[0][priceFieldKey] = true;
              deserializeJson(doc, payload, DeserializationOption::Filter(filter));
              for (int i = 0; i < QUARTERS_OF_DAY; i++)
              {
                result.prices[i].electricityPrice = doc[i][priceFieldKey].as<float>() * 100.0f;
                result.updated = time(NULL);
              }
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
