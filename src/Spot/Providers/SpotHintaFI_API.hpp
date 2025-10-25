#pragma once

#include <WiFi.h>
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
      log_d("SpotHinta_API does not support fetching electricity prices for tomorrow.");
      return result;
    }

    WiFiClientSecure *client = new WiFiClientSecure;
    if (client)
    {
      client->setInsecure();
      {
        String url = "https://api.spot-hinta.fi/Today?region=" + domain;
        log_d("Fetching URL: %s", url.c_str());
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
            log_d("HTTP error code: %d", httpCode);
          }

          https.end();
        }
        else
        {
          log_d("Unable to connect to URL: %s", url.c_str());
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
