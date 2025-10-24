#pragma once

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <math.h>
#include <time.h>
#include "../ElectricityPriceResult.hpp"

typedef struct ExchangeRateResult
{
  time_t updated = 0;
  float eur2czkRate;
} ExchangeRateResult_t;

class OTE_CZ_API
{
public:
  ElectricityPriceResult_t reloadData(bool tomorrow)
  {
    ElectricityPriceResult_t result;
    result.updated = 0;

    ExchangeRateResult_t exchangeRateResult = reloadExchangeRateData();
    if (exchangeRateResult.updated > 0)
    {
      result = reloadSpotPriceData(exchangeRateResult.eur2czkRate, tomorrow);
    }

    return result;
  }

private:
  ElectricityPriceResult_t reloadSpotPriceData(float exchangeRate, bool tomorrow)
  {
    {
      ElectricityPriceResult_t result;
      result.updated = 0;
      WiFiClientSecure *client = new WiFiClientSecure;
      if (client)
      {
        client->setInsecure();
        client->setTimeout(20);
        {
          String url = "https://www.ote-cr.cz/cs/kratkodobe-trhy/elektrina/denni-trh/@@chart-data?report_date=";
          time_t timestamp = time(NULL);

          if (tomorrow)
          {
            timestamp += 86400;
          }

          char buf[100];
          strftime(buf, sizeof(buf), "%Y-%m-%d", localtime((time_t *)&timestamp));
          url += String(buf);
          url += "&amp;time_resolution=PT15M";
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
              if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY)
              {
                DynamicJsonDocument doc(12 * 1024);
                DynamicJsonDocument filter(512);
                filter["data"]["dataLine"][0]["point"][0]["y"] = true;
                DeserializationError error = deserializeJson(doc, https.getStream(), DeserializationOption::Filter(filter));
                if (error == DeserializationError::Ok)
                {
                  for (int i = 0; i < QUARTERS_OF_DAY; i++)
                  {
                    if (doc["data"]["dataLine"][1]["point"][i]["y"].isNull())
                    {
                      break;
                    }
                    result.prices[i].electricityPrice = doc["data"]["dataLine"][1]["point"][i]["y"].as<float>() * exchangeRate / 1000.0f;
                    result.updated = time(NULL);
                  }
                }
                else
                {
                  log_d("ERROR: %s", error.c_str());
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
            log_d("Unable to connect to URL: %s", url.c_str());
          }
        }
        delete client;
      }
      return result;
    }
  }

  ExchangeRateResult_t reloadExchangeRateData()
  {
    {
      ExchangeRateResult_t result;
      result.updated = 0;
      WiFiClientSecure *client = new WiFiClientSecure;
      if (client)
      {
        client->setInsecure();
        client->setTimeout(20);
        {
          // Add a scoping block for HTTPClient https to make sure it is destroyed before WiFiClientSecure *client is
          HTTPClient https;
          if (https.begin(*client, "https://api.cnb.cz/cnbapi/exrates/daily"))
          {
            int httpCode = https.GET();
            // httpCode will be negative on error
            if (httpCode > 0)
            {
              // file found at server
              if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY)
              {
                String payload = https.getString();

                DynamicJsonDocument doc(4196);
                deserializeJson(doc, payload);
                JsonArray arr = doc["rates"].as<JsonArray>();
                for (JsonVariant value : arr)
                {
                  if (value["currencyCode"].as<String>().equals("EUR"))
                  {
                    result.eur2czkRate = value["rate"].as<float>();
                    result.updated = time(NULL);
                    break;
                  }
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
            log_d("Unable to connect");
          }
        }
        client->stop();
        delete client;
      }
      return result;
    }
  }
};
