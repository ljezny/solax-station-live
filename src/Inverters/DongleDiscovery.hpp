#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>

#include "DongleResult.hpp"

class DongleDiscovery {
    public:
        DongleDiscoveryResult_t discoverDongle() {
            DongleDiscoveryResult_t result;
            
            if(WiFi.status() == WL_CONNECTED) {
                result.sn = parseDongleSN(WiFi.SSID());
                result.result = true;
                return result;
            }
            
            int found = WiFi.scanNetworks(false);
            for(int i = 0; i < found; i++) {
                log_d("Found network: %s", WiFi.SSID(i).c_str());
                String ssid = WiFi.SSID(i);

                if(isSolaxDongleSSID(ssid)) {
                    WiFi.begin(ssid);
                    
                    if(awaitWifiConnection()) {
                        if(checkSolaxConnection()) {
                            result.sn = parseDongleSN(ssid);
                            result.type = DONGLE_TYPE_SOLAX;
                            result.result = true;
                            break;
                        }
                    }

                    WiFi.disconnect();
                }

                if(isGoodWeSSID(ssid)) {
                    WiFi.begin(ssid);
                    
                    if(awaitWifiConnection()) {
                        if(checkGoodWeConnection()) {
                            result.sn = parseDongleSN(ssid);
                            result.type = DONGLE_TYPE_GOODWE;
                            result.result = true;
                            break;
                        }
                    }

                    WiFi.disconnect();
                }
            }
            return result;
        }
    private:
        bool awaitWifiConnection() {
            int retries = 100;
            for(int r = 0; r < retries; r++) {
                if(WiFi.status() == WL_CONNECTED) {
                    return true;
                } else {
                    delay(100);
                }
            }
            return false;
        }

        bool checkSolaxConnection() {
            WiFiClient client;
            int result = client.connect("5.8.8.8", 80, 5000);
            client.stop();
            return result == 1;
        }

        bool checkGoodWeConnection() {
            WiFiClient client;
            int result = client.connect("10.10.100.253", 8899, 5000);
            client.stop();
            return result == 1;
        }

        bool isSolaxDongleSSID(String ssid) {
            return ssid.startsWith("Wifi_");
        }

        bool isGoodWeSSID(String ssid) {
            return ssid.startsWith("Solar-WiFi");
        }

        String parseDongleSN(String ssid) {
            String sn = ssid;
            sn.replace("Wifi_", "");
            sn.replace("Solar-WiFi", "");
            return sn;
        }
};