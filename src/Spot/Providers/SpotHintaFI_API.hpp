#pragma once

#include <WiFi.h>
#include <RemoteLogger.hpp>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <math.h>
#include <time.h>
#include "../ElectricityPriceResult.hpp"

class SpotHinta_API
{
public:
  ElectricityPriceResult_t reloadData(String domain, bool tommorow)
  {
    ElectricityPriceResult_t result;

    if (tommorow)
    {
      LOGD("SpotHinta_API does not support fetching electricity prices for tomorrow.");
      return result;
    }

    WiFiClientSecure *client = new WiFiClientSecure;
    if (client)
    {
      client->setInsecure();
      {
        String url = "https://api.spot-hinta.fi/Today?region=" + domain;
        LOGD("Fetching URL: %s", url.c_str());
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
              DynamicJsonDocument doc(4196);
              DynamicJsonDocument filter(1024);
              filter[0]["PriceNoTax"] = true;
              deserializeJson(doc, payload, DeserializationOption::Filter(filter));
              for (int i = 0; i < QUARTERS_OF_DAY; i++)
              {
                result.prices[i].electricityPrice = doc[i]["PriceNoTax"].as<float>() * 100.0f;
                result.updated = time(NULL);
              }
            }
          }
          else
          {
            LOGD("HTTP error code: %d", httpCode);
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
    else
    {
      LOGD("Unable to create client.");
    }

    return result;
  }
};
