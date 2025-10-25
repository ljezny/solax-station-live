#ifndef AWATTAR_API_h
#define AWATTAR_API_h

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <math.h>
#include <time.h>
#include "../ElectricityPriceResult.hpp"

class Awattar_API
{
public:
  ElectricityPriceResult_t reloadData(String domain, bool tomorrow)
  {
    ElectricityPriceResult_t result;
    result.updated = 0;
    WiFiClientSecure *client = new WiFiClientSecure;
    if (client)
    {
      client->setInsecure();
      {
        String url = "https://api.awattar." + domain + "/v1/marketdata?start=";
        time_t timestamp = time(NULL);
        tm *t = localtime((time_t *)&timestamp);
        if (tomorrow)
        {
          t->tm_mday++;
        }
        t->tm_hour = 0;
        t->tm_min = 0;
        t->tm_sec = 0;
        time_t startOfDayTimestamp = mktime(t);
        url += String(startOfDayTimestamp) + "000"; // this is ugly, i know

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
              String payload = https.getString();
              log_d("Response payload: %s", payload.c_str());
              DynamicJsonDocument doc(4196);
              DynamicJsonDocument filter(1024);
              filter["data"][0]["marketprice"] = true;
              deserializeJson(doc, payload, DeserializationOption::Filter(filter));
              for (int i = 0; i < QUARTERS_OF_DAY; i++)
              {
                result.prices[i].electricityPrice = doc["data"][i / 4]["marketprice"].as<float>() / 1000.0f * 100.0f;
                log_d("Price for quarter %d: %f", i, result.prices[i].electricityPrice);
              }
              result.updated = time(NULL);
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
      client->stop();
      delete client;
    }
    return result;
  }
};

#endif