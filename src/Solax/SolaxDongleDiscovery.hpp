#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>

typedef struct {
    bool result = false;
    String sn = "";
} SolaxDongleDiscoveryResult_t;

class SolaxDongleDiscovery {
    public:
        SolaxDongleDiscoveryResult_t discoverDongle() {
            SolaxDongleDiscoveryResult_t result;
            
            if(WiFi.status() == WL_CONNECTED) {
                result.sn = parseDongleSN(WiFi.SSID());
                result.result = true;
                return result;
            }
            WiFi.scanNetworks(true);
            
            int found = WiFi.scanNetworks(false);
            for(int i = 0; i < found; i++) {
                log_d("Found network: %s", WiFi.SSID(i).c_str());
                String ssid = WiFi.SSID(i);
                if(isSolaxDongleSSID(ssid)) {
                    WiFi.begin(ssid);
                    
                    int retries = 100;
                    for(int r = 0; r < retries; r++) {
                        if(WiFi.status() == WL_CONNECTED) {
                            if(canConnect()) {
                                result.sn = parseDongleSN(WiFi.SSID());
                                result.result = true;
                                return result;
                            }
                            break;
                        } else {
                            delay(100);
                        }
                    }

                    WiFi.disconnect();
                }
            }
            return result;
        }
    private:
        bool canConnect() {
            WiFiClient client;
            int result = client.connect("5.8.8.8", 80, 5000);
            client.stop();
            return result == 1;
        }

        bool isSolaxDongleSSID(String ssid) {
            return ssid.startsWith("Wifi_");
        }

        String parseDongleSN(String ssid) {
            String sn = ssid;
            sn.replace("Wifi_", "");
            return sn;
        }
};