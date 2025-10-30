#ifndef OTK_SK_API_h
#define OTK_SK_API_h

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <math.h>
#include <time.h>
#include "../ElectricityPriceResult.hpp"

class OTK_SK_API
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
        HTTPClient https;
        String url = "https://isot.okte.sk/api/v1/dam/results?deliveryDayFrom=" + fromDay + "&deliveryDayTo=" + fromDay;
        log_d("Fetching URL: %s", url.c_str());
        https.useHTTP10(true);
        https.addHeader("Accept-Encoding", "identity");
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
              DynamicJsonDocument filter(512);
              filter[0]["price"] = true;
              DeserializationError err = deserializeJson(doc, https.getString(), DeserializationOption::Filter(filter));
              if (err == DeserializationError::Ok)
              {
                for (int i = 0; i < QUARTERS_OF_DAY; i++)
                {
                  result.prices[i].electricityPrice = round(doc[i]["price"].as<float>()) / 1000.0 * 100.0;
                }
                result.updated = time(NULL);
              }
              else
              {
                log_d("ERROR: %s", err.c_str());
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