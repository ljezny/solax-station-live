#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>

#include "DongleResult.hpp"

#define DONGLE_DISCOVERY_MAX_RESULTS 10

class DongleDiscovery {
    public:
        DongleDiscoveryResult_t discoveries[DONGLE_DISCOVERY_MAX_RESULTS];
         
        bool discoverDongle() {
            bool result = false;

            int found = WiFi.scanComplete();
            if(found == WIFI_SCAN_FAILED) {
                log_d("WiFi scan failed, reseting");
                WiFi.scanNetworks(true);
                return false;
            }
            for(int i = 0; i < found; i++) {
                log_d("Found network: %s", WiFi.SSID(i).c_str());
                String ssid = WiFi.SSID(i);
                if(ssid.length() == 0) {
                    log_d("Empty SSID");
                    continue;
                }
                int discoveryIndex = -1;

                //find if existing in sparse array
                for(int j = 0; j < DONGLE_DISCOVERY_MAX_RESULTS; j++) {
                    if(ssid.equals(discoveries[j].ssid)) {
                        discoveryIndex = j;
                        break;
                    }
                }

                if(discoveryIndex != -1) {
                    log_d("Already discovered this dongle");
                    continue;
                }

                //find empty slot
                for(int j = 0; j < DONGLE_DISCOVERY_MAX_RESULTS; j++) {
                    if(discoveries[j].type == DONGLE_TYPE_UNKNOWN) {
                        discoveryIndex = j;
                        break;
                    }
                }

                if(discoveryIndex == -1) {
                    log_d("No more space for discovery results");
                    continue;
                }

                if(discoveries[discoveryIndex].type != DONGLE_TYPE_UNKNOWN) {
                    log_d("Already discovered this dongle");
                    continue;
                }

                if(isSolaxDongleSSID(ssid)) {
                    discoveries[discoveryIndex].sn = parseDongleSN(ssid);
                    discoveries[discoveryIndex].type = DONGLE_TYPE_SOLAX;
                    discoveries[discoveryIndex].ssid = ssid;
                }

                if(isGoodWeSSID(ssid)) {
                    discoveries[discoveryIndex].sn = parseDongleSN(ssid);
                    discoveries[discoveryIndex].type = DONGLE_TYPE_GOODWE;
                    discoveries[discoveryIndex].ssid = ssid;
                    result = true;                   
                }

                if(isShellySSID(ssid)) {
                    discoveries[discoveryIndex].type = DONGLE_TYPE_SHELLY;
                    discoveries[discoveryIndex].sn = parseDongleSN(ssid);   
                    discoveries[discoveryIndex].ssid = ssid;
                    result = true;
                }
            }
            
            WiFi.scanDelete();

            return true;
        }

        bool connectToDongle(DongleDiscoveryResult_t& discovery, String password) {
            if(discovery.type == DONGLE_TYPE_UNKNOWN) {
                return false;
            }

            if(WiFi.SSID() == discovery.ssid) {
                return true;
            } else {
                WiFi.disconnect();
            }

            WiFi.begin(discovery.ssid, password);

            return awaitWifiConnection();
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

        bool checkSolaxInverterConnection() {
            //check if IP starts with 5.8.8.X
            return WiFi.localIP()[0] == 5 && WiFi.localIP()[1] == 8 && WiFi.localIP()[2] == 8;
        }

        bool checkSolaxWallboxConnection() {
            //check if IP starts with 192.168.10.X
            return WiFi.localIP()[0] == 192 && WiFi.localIP()[1] == 168 && WiFi.localIP()[2] == 10;
        }

        bool isSolaxDongleSSID(String ssid) {
            return ssid.startsWith("Wifi_");
        }

        bool isGoodWeSSID(String ssid) {
            return ssid.startsWith("Solar-WiFi");
        }

        bool isShellySSID(String ssid) {
            return ssid.startsWith("shellyplug-s-") 
            || ssid.startsWith("shellyplug-") 
            || ssid.startsWith("ShellyPlusPlugS-") 
            || ssid.startsWith("PlusPlugS-") 
            || ssid.startsWith("ShellyPro1PM-") 
            || ssid.startsWith("Pro1PM-") 
            || ssid.startsWith("ShellyPlus1PM-") 
            || ssid.startsWith("Plus1PM-") 
            || ssid.startsWith("ShellyPro3-") 
            || ssid.startsWith("Pro3-");
        }

        String parseDongleSN(String ssid) {
            String sn = ssid;
            sn.replace("Wifi_", "");
            sn.replace("Solar-WiFi", "");
            sn.replace("shellyplug-s-", "");
            sn.replace("shellyplug-", "");
            sn.replace("ShellyPlusPlugS-", "");
            sn.replace("PlusPlugS-", "");
            sn.replace("ShellyPro1PM-", "");
            sn.replace("Pro1PM-", "");
            sn.replace("ShellyPlus1PM-", "");
            sn.replace("Plus1PM-", "");
            sn.replace("ShellyPro3-", "");
            sn.replace("Pro3-", "");
            return sn;
        }
};