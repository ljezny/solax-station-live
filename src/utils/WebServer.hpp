#pragma once

#include <Arduino.h>
#include <esp_http_server.h>
#include <lvgl.h>
#include <ArduinoJson.h>
#include "../gfx_conf.h"
#include "../Inverters/InverterResult.hpp"
#include "../Spot/ElectricityPriceResult.hpp"
#include "../webserver/icons.h"

// Forward declarations
extern LGFX tft;
extern SemaphoreHandle_t lvgl_mutex;

// Embedded web files - will be defined at bottom of file
extern const char INDEX_HTML[];
extern const char STYLE_CSS[];
extern const char APP_JS[];

class WebServer
{
public:
    WebServer() : server(nullptr), lvglMutex(nullptr), inverterData(nullptr), priceData(nullptr) {}

    void begin(SemaphoreHandle_t mutex)
    {
        lvglMutex = mutex;
        
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.server_port = 80;
        config.stack_size = 8192;
        config.max_uri_handlers = 16;
        config.uri_match_fn = httpd_uri_match_wildcard;
        
        if (httpd_start(&server, &config) == ESP_OK)
        {
            // Index page
            httpd_uri_t indexUri = {
                .uri = "/",
                .method = HTTP_GET,
                .handler = indexHandler,
                .user_ctx = this
            };
            httpd_register_uri_handler(server, &indexUri);

            // Style CSS
            httpd_uri_t styleUri = {
                .uri = "/style.css",
                .method = HTTP_GET,
                .handler = styleHandler,
                .user_ctx = this
            };
            httpd_register_uri_handler(server, &styleUri);

            // App JS
            httpd_uri_t appUri = {
                .uri = "/app.js",
                .method = HTTP_GET,
                .handler = appHandler,
                .user_ctx = this
            };
            httpd_register_uri_handler(server, &appUri);

            // API - Data endpoint
            httpd_uri_t dataUri = {
                .uri = "/api/data",
                .method = HTTP_GET,
                .handler = dataHandler,
                .user_ctx = this
            };
            httpd_register_uri_handler(server, &dataUri);

            // Icon endpoints
            httpd_uri_t iconUri = {
                .uri = "/icons/*",
                .method = HTTP_GET,
                .handler = iconHandler,
                .user_ctx = this
            };
            httpd_register_uri_handler(server, &iconUri);

            // Screenshot endpoint
            httpd_uri_t screenshotUri = {
                .uri = "/screenshot.bmp",
                .method = HTTP_GET,
                .handler = screenshotHandler,
                .user_ctx = this
            };
            httpd_register_uri_handler(server, &screenshotUri);

            log_i("Web server started on port 80");
        }
        else
        {
            log_e("Failed to start web server");
        }
    }

    void stop()
    {
        if (server)
        {
            httpd_stop(server);
            server = nullptr;
            log_i("Web server stopped");
        }
    }

    void setInverterData(InverterData_t *data)
    {
        inverterData = data;
    }

    void setPriceData(ElectricityPriceTwoDays_t *data)
    {
        priceData = data;
    }

private:
    httpd_handle_t server;
    SemaphoreHandle_t lvglMutex;
    InverterData_t *inverterData;
    ElectricityPriceTwoDays_t *priceData;

    static esp_err_t indexHandler(httpd_req_t *req)
    {
        httpd_resp_set_type(req, "text/html");
        httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
        return httpd_resp_send(req, INDEX_HTML, strlen(INDEX_HTML));
    }

    static esp_err_t styleHandler(httpd_req_t *req)
    {
        httpd_resp_set_type(req, "text/css");
        httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
        return httpd_resp_send(req, STYLE_CSS, strlen(STYLE_CSS));
    }

    static esp_err_t appHandler(httpd_req_t *req)
    {
        httpd_resp_set_type(req, "application/javascript");
        httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");
        return httpd_resp_send(req, APP_JS, strlen(APP_JS));
    }

    static esp_err_t dataHandler(httpd_req_t *req)
    {
        WebServer *self = (WebServer *)req->user_ctx;
        
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        
        if (self->inverterData == nullptr)
        {
            return httpd_resp_send(req, "{\"error\":\"No data\"}", 19);
        }
        
        // Build JSON response - need larger buffer for spot prices
        DynamicJsonDocument doc(8192);
        InverterData_t &data = *self->inverterData;
        
        doc["status"] = data.status;
        doc["sn"] = data.sn;
        doc["pv1Power"] = data.pv1Power;
        doc["pv2Power"] = data.pv2Power;
        doc["pv3Power"] = data.pv3Power;
        doc["pv4Power"] = data.pv4Power;
        doc["soc"] = data.soc;
        doc["batteryPower"] = data.batteryPower;
        doc["batteryCapacityWh"] = data.batteryCapacityWh;
        doc["batteryChargedToday"] = data.batteryChargedToday;
        doc["batteryDischargedToday"] = data.batteryDischargedToday;
        doc["batteryTemperature"] = data.batteryTemperature;
        doc["gridBuyToday"] = data.gridBuyToday;
        doc["gridSellToday"] = data.gridSellToday;
        doc["L1Power"] = data.L1Power;
        doc["L2Power"] = data.L2Power;
        doc["L3Power"] = data.L3Power;
        doc["loadPower"] = data.loadPower;
        doc["loadToday"] = data.loadToday;
        doc["gridPowerL1"] = data.gridPowerL1;
        doc["gridPowerL2"] = data.gridPowerL2;
        doc["gridPowerL3"] = data.gridPowerL3;
        doc["inverterTemperature"] = data.inverterTemperature;
        doc["pvToday"] = data.pvToday;
        doc["pvTotal"] = data.pvTotal;
        doc["inverterMode"] = (int)data.inverterMode;
        doc["hasBattery"] = data.hasBattery;
        
        // Add spot prices if available
        if (self->priceData != nullptr && self->priceData->updated > 0)
        {
            doc["spotCurrency"] = self->priceData->currency;
            doc["spotEnergyUnit"] = self->priceData->energyUnit;
            
            // Get current quarter
            time_t now = time(nullptr);
            struct tm timeinfo;
            localtime_r(&now, &timeinfo);
            int currentQuarter = timeinfo.tm_hour * 4 + timeinfo.tm_min / 15;
            doc["currentQuarter"] = currentQuarter;
            
            // Current price
            if (currentQuarter >= 0 && currentQuarter < QUARTERS_TWO_DAYS)
            {
                doc["currentPrice"] = self->priceData->prices[currentQuarter].electricityPrice;
                doc["currentPriceLevel"] = (int)self->priceData->prices[currentQuarter].priceLevel;
            }
            
            // Send today's 96 quarters (for chart)
            JsonArray prices = doc.createNestedArray("spotPrices");
            for (int i = 0; i < QUARTERS_OF_DAY; i++)
            {
                JsonObject p = prices.createNestedObject();
                p["price"] = self->priceData->prices[i].electricityPrice;
                p["level"] = (int)self->priceData->prices[i].priceLevel;
            }
        }
        
        String jsonString;
        serializeJson(doc, jsonString);
        
        return httpd_resp_send(req, jsonString.c_str(), jsonString.length());
    }

    static esp_err_t screenshotHandler(httpd_req_t *req)
    {
        WebServer *self = (WebServer *)req->user_ctx;
        
        // Screen dimensions
        const int width = 800;
        const int height = 480;
        const int bytesPerPixel = 3;
        const int rowSize = ((width * bytesPerPixel + 3) / 4) * 4;
        const int imageSize = rowSize * height;
        const int fileSize = 54 + imageSize;
        
        // BMP header
        uint8_t bmpHeader[54] = {
            'B', 'M',
            (uint8_t)(fileSize), (uint8_t)(fileSize >> 8), (uint8_t)(fileSize >> 16), (uint8_t)(fileSize >> 24),
            0, 0, 0, 0,
            54, 0, 0, 0,
            40, 0, 0, 0,
            (uint8_t)(width), (uint8_t)(width >> 8), 0, 0,
            (uint8_t)(height), (uint8_t)(height >> 8), 0, 0,
            1, 0,
            24, 0,
            0, 0, 0, 0,
            (uint8_t)(imageSize), (uint8_t)(imageSize >> 8), (uint8_t)(imageSize >> 16), (uint8_t)(imageSize >> 24),
            0x13, 0x0B, 0, 0,
            0x13, 0x0B, 0, 0,
            0, 0, 0, 0,
            0, 0, 0, 0
        };
        
        httpd_resp_set_type(req, "image/bmp");
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
        httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"screenshot.bmp\"");
        
        if (httpd_resp_send_chunk(req, (const char *)bmpHeader, sizeof(bmpHeader)) != ESP_OK)
        {
            return ESP_FAIL;
        }
        
        uint8_t *rowBuffer = (uint8_t *)heap_caps_malloc(rowSize, MALLOC_CAP_DEFAULT);
        lgfx::rgb888_t *pixelBuffer = (lgfx::rgb888_t *)heap_caps_malloc(width * sizeof(lgfx::rgb888_t), MALLOC_CAP_DEFAULT);
        
        if (!rowBuffer || !pixelBuffer)
        {
            if (rowBuffer) free(rowBuffer);
            if (pixelBuffer) free(pixelBuffer);
            httpd_resp_send_chunk(req, NULL, 0);
            return ESP_FAIL;
        }
        
        bool mutexTaken = false;
        if (self->lvglMutex)
        {
            mutexTaken = (xSemaphoreTake(self->lvglMutex, pdMS_TO_TICKS(2000)) == pdTRUE);
        }
        
        if (mutexTaken || !self->lvglMutex)
        {
            for (int y = height - 1; y >= 0; y--)
            {
                memset(rowBuffer, 0, rowSize);
                tft.readRect(0, y, width, 1, pixelBuffer);
                
                for (int x = 0; x < width; x++)
                {
                    rowBuffer[x * 3 + 0] = pixelBuffer[x].b;
                    rowBuffer[x * 3 + 1] = pixelBuffer[x].g;
                    rowBuffer[x * 3 + 2] = pixelBuffer[x].r;
                }
                
                if (httpd_resp_send_chunk(req, (const char *)rowBuffer, rowSize) != ESP_OK)
                {
                    free(pixelBuffer);
                    free(rowBuffer);
                    if (mutexTaken) xSemaphoreGive(self->lvglMutex);
                    return ESP_FAIL;
                }
            }
            
            if (mutexTaken) xSemaphoreGive(self->lvglMutex);
        }
        
        free(pixelBuffer);
        free(rowBuffer);
        httpd_resp_send_chunk(req, NULL, 0);
        
        return ESP_OK;
    }

    static esp_err_t iconHandler(httpd_req_t *req)
    {
        // Extract icon name from URI: /icons/name.png
        const char *uri = req->uri;
        const char *iconName = uri + 7; // Skip "/icons/"
        
        const uint8_t *iconData = nullptr;
        size_t iconLen = 0;
        
        // Match icon name to data
        if (strcmp(iconName, "solar-panel.png") == 0) {
            iconData = ICON_SOLAR_PANEL_4;
            iconLen = ICON_SOLAR_PANEL_4_LEN;
        } else if (strcmp(iconName, "battery.png") == 0) {
            iconData = ICON_ECO_BATTERY;
            iconLen = ICON_ECO_BATTERY_LEN;
        } else if (strcmp(iconName, "house.png") == 0) {
            iconData = ICON_ECO_HOUSE_3;
            iconLen = ICON_ECO_HOUSE_3_LEN;
        } else if (strcmp(iconName, "grid.png") == 0) {
            iconData = ICON_POWER_PLANT;
            iconLen = ICON_POWER_PLANT_LEN;
        } else if (strcmp(iconName, "inverter.png") == 0) {
            iconData = ICON_SOLAR_INVERTER;
            iconLen = ICON_SOLAR_INVERTER_LEN;
        } else if (strcmp(iconName, "battery_0.png") == 0) {
            iconData = ICON_BATTERY_0;
            iconLen = ICON_BATTERY_0_LEN;
        } else if (strcmp(iconName, "battery_20.png") == 0) {
            iconData = ICON_BATTERY_20;
            iconLen = ICON_BATTERY_20_LEN;
        } else if (strcmp(iconName, "battery_40.png") == 0) {
            iconData = ICON_BATTERY_40;
            iconLen = ICON_BATTERY_40_LEN;
        } else if (strcmp(iconName, "battery_60.png") == 0) {
            iconData = ICON_BATTERY_60;
            iconLen = ICON_BATTERY_60_LEN;
        } else if (strcmp(iconName, "battery_80.png") == 0) {
            iconData = ICON_BATTERY_80;
            iconLen = ICON_BATTERY_80_LEN;
        } else if (strcmp(iconName, "battery_100.png") == 0) {
            iconData = ICON_BATTERY_100;
            iconLen = ICON_BATTERY_100_LEN;
        }
        
        if (iconData == nullptr) {
            httpd_resp_send_404(req);
            return ESP_FAIL;
        }
        
        httpd_resp_set_type(req, "image/png");
        httpd_resp_set_hdr(req, "Cache-Control", "max-age=86400");
        return httpd_resp_send(req, (const char *)iconData, iconLen);
    }
};

// ============================================================================
// Embedded Web Files
// ============================================================================

const char INDEX_HTML[] = R"rawliteral(<!DOCTYPE html>
<html lang="cs">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Solar Station Live</title>
    <link rel="stylesheet" href="/style.css">
</head>
<body>
    <div class="dashboard">
        <div class="left-section">
            <div class="tile pv-tile">
                <img src="/icons/solar-panel.png" class="tile-icon-img" alt="PV">
                <div class="tile-content">
                    <div class="tile-value"><span id="pvPower">--</span><span class="unit">W</span></div>
                    <div class="tile-details"><div><span id="pv1Power">--</span>W</div><div><span id="pv2Power">--</span>W</div></div>
                </div>
                <div class="tile-badge pv-badge" id="pvPercent">--%</div>
            </div>
            <div class="tile battery-tile" id="batteryTile">
                <div class="tile-header">
                    <img src="/icons/battery.png" class="tile-icon-img battery-icon" id="batteryIcon" alt="Battery">
                    <div class="battery-info">
                        <div class="tile-value"><span id="soc">--</span><span class="unit">%</span></div>
                        <div class="tile-secondary"><span id="batteryTime">--</span></div>
                        <div class="tile-secondary power"><span id="batteryPowerAbs">--</span>W</div>
                    </div>
                </div>
                <div class="tile-badge temp-badge" id="batteryTemp">--°C</div>
            </div>
            <div class="tile load-tile" id="loadTile">
                <img src="/icons/house.png" class="tile-icon-img" alt="Home">
                <div class="tile-value"><span id="loadPower">--</span><span class="unit">W</span></div>
                <div class="tile-badge mode-badge" id="inverterMode">NORMAL</div>
            </div>
            <div class="tile inverter-tile">
                <div class="inverter-left">
                    <img src="/icons/inverter.png" class="tile-icon-img inverter-icon" alt="Inverter">
                    <div class="tile-value"><span id="inverterPower">--</span><span class="unit">W</span></div>
                    <div class="phase-bars">
                        <div class="phase-row"><span class="phase-dot"></span><div class="phase-bar"><div class="phase-fill" id="L1Bar"></div></div><span class="phase-value"><span id="L1Power">--</span> W</span></div>
                        <div class="phase-row"><span class="phase-dot"></span><div class="phase-bar"><div class="phase-fill" id="L2Bar"></div></div><span class="phase-value"><span id="L2Power">--</span> W</span></div>
                        <div class="phase-row"><span class="phase-dot"></span><div class="phase-bar"><div class="phase-fill" id="L3Bar"></div></div><span class="phase-value"><span id="L3Power">--</span> W</span></div>
                    </div>
                    <div class="inverter-info"><span id="inverterSN">--</span></div>
                </div>
                <div class="inverter-right" id="gridSection">
                    <img src="/icons/grid.png" class="tile-icon-img grid-icon" alt="Grid">
                    <div class="tile-value"><span id="gridPowerDisplay">--</span><span class="unit">W</span></div>
                    <div class="phase-bars grid-bars" id="gridBars">
                        <div class="phase-row"><span class="phase-dot grid-dot"></span><div class="phase-bar"><div class="phase-fill grid-fill" id="gridL1Bar"></div></div><span class="phase-value"><span id="gridL1Power">--</span> W</span></div>
                        <div class="phase-row"><span class="phase-dot grid-dot"></span><div class="phase-bar"><div class="phase-fill grid-fill" id="gridL2Bar"></div></div><span class="phase-value"><span id="gridL2Power">--</span> W</span></div>
                        <div class="phase-row"><span class="phase-dot grid-dot"></span><div class="phase-bar"><div class="phase-fill grid-fill" id="gridL3Bar"></div></div><span class="phase-value"><span id="gridL3Power">--</span> W</span></div>
                    </div>
                    <div class="tile-badge temp-badge inverter-temp" id="inverterTemp">--°C</div>
                </div>
            </div>
            <div class="tile bottom-tile" id="bottomTile">
                <div class="bottom-left">
                    <img src="/icons/house.png" class="tile-icon-img small" alt="Home">
                    <div class="tile-value"><span id="homeLoadPower">--</span><span class="unit">W</span></div>
                </div>
                <div class="bottom-right">
                    <img src="/icons/grid.png" class="tile-icon-img small" alt="Grid">
                    <div class="tile-value"><span id="gridPower">--</span><span class="unit">W</span></div>
                </div>
            </div>
        </div>
        <div class="right-section">
            <div class="stats-row">
                <div class="stat-tile">
                    <img src="/icons/solar-panel.png" class="stat-icon-img" alt="PV">
                    <div class="stat-values">
                        <div class="stat-value"><span id="pvToday">--</span> <span class="stat-unit">kWh</span></div>
                        <div class="stat-value secondary"><span id="pvTotal">--</span> <span class="stat-unit">kWh</span></div>
                    </div>
                </div>
                <div class="stat-tile">
                    <img src="/icons/house.png" class="stat-icon-img" alt="Home">
                    <div class="stat-values">
                        <div class="stat-value"><span id="loadToday">--</span> <span class="stat-unit">kWh</span></div>
                        <div class="stat-value secondary green"><span id="selfUsePercent">--</span><span class="stat-unit">%</span></div>
                    </div>
                </div>
            </div>
            <div class="clock-tile"><span id="clock">--:--</span></div>
            <div class="stats-row">
                <div class="stat-tile">
                    <img src="/icons/battery.png" class="stat-icon-img" alt="Battery">
                    <div class="stat-values">
                        <div class="stat-value positive">+<span id="batteryChargedToday">--</span> <span class="stat-unit">kWh</span></div>
                        <div class="stat-value negative">-<span id="batteryDischargedToday">--</span> <span class="stat-unit">kWh</span></div>
                    </div>
                </div>
                <div class="stat-tile">
                    <img src="/icons/grid.png" class="stat-icon-img" alt="Grid">
                    <div class="stat-values">
                        <div class="stat-value positive">+<span id="gridSellToday">--</span> <span class="stat-unit">kWh</span></div>
                        <div class="stat-value negative">-<span id="gridBuyToday">--</span> <span class="stat-unit">kWh</span></div>
                    </div>
                </div>
            </div>
            <div class="intelligence-tile">
                <span class="intelligence-title">Intelligence</span>
                <div class="intelligence-badges">
                    <span class="intelligence-badge savings" id="intelligenceSavings">+12 CZK</span>
                    <span class="intelligence-badge mode" id="intelligenceMode">NORMAL</span>
                </div>
            </div>
            <div class="chart-tile">
                <div class="chart-container">
                    <canvas id="spotChart" width="400" height="120"></canvas>
                </div>
                <div class="spot-price-row">
                    <span class="spot-price-badge" id="spotPrice">-- CZK / kWh</span>
                </div>
            </div>
            <div class="debug-row">
                <div class="status-bar">
                    <span class="status-indicator loading" id="statusIndicator">●</span>
                    <span id="statusText">Connecting...</span>
                </div>
                <a href="/screenshot.bmp" class="btn btn-screenshot" download="screenshot.bmp">📷</a>
            </div>
        </div>
    </div>
    <script src="/app.js"></script>
</body>
</html>)rawliteral";

const char STYLE_CSS[] = R"rawliteral(:root{--color-pv:#FFD400;--color-battery:#779ECB;--color-load:#03AD36;--color-grid:#C25964;--color-inverter:#D3D3D3;--color-card:#fff;--color-card-shadow:rgba(0,0,0,0.2);--color-text:#333;--color-text-light:#666;--color-text-muted:#999;--radius-large:24px;--radius-medium:16px;--radius-small:12px;--radius-xs:8px}
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Open Sans','Segoe UI',-apple-system,BlinkMacSystemFont,Roboto,sans-serif;background:linear-gradient(180deg,#FFD400 0%,#779ECB 100%);min-height:100vh;padding:8px;color:var(--color-text)}
.dashboard{display:grid;grid-template-columns:minmax(300px,420px) minmax(300px,1fr);gap:8px;max-width:900px;margin:0 auto}
.left-section{background:var(--color-card);border-radius:var(--radius-large);padding:8px;display:grid;grid-template-columns:38% 1fr;grid-template-rows:auto auto auto auto;gap:8px;box-shadow:0 8px 32px var(--color-card-shadow)}
.tile{border-radius:var(--radius-medium);padding:12px 14px;position:relative;overflow:hidden}
.tile-icon-img{width:48px;height:48px;object-fit:contain}
.tile-icon-img.small{width:36px;height:36px}
.tile-icon-img.battery-icon{width:48px;height:48px}
.tile-icon-img.inverter-icon,.tile-icon-img.grid-icon{width:40px;height:40px;margin-bottom:4px}
.tile-value{font-size:38px;font-weight:700;line-height:1}
.tile-value .unit{font-size:16px;font-weight:400;vertical-align:super}
.tile-secondary{font-size:11px;font-weight:500;opacity:0.9}
.tile-secondary.power{font-size:16px;font-weight:600}
.tile-content{display:flex;flex-direction:column}
.tile-details{font-size:11px;color:rgba(255,255,255,0.9);margin-top:2px}
.tile-badge{position:absolute;padding:4px 10px;border-radius:var(--radius-small);font-size:11px;font-weight:700;color:#fff}
.pv-badge{top:6px;right:6px;background:var(--color-load);color:#fff;font-size:14px;font-weight:700}
.temp-badge{background:#FFAA00;color:#fff;padding:3px 8px;font-size:10px;top:6px;right:6px;box-shadow:0 4px 16px rgba(255,170,0,0.5)}
.mode-badge{bottom:6px;left:6px;background:var(--color-load);font-size:9px;padding:3px 8px}
.pv-tile{grid-column:1;grid-row:1;background:var(--color-pv);color:#333;display:flex;gap:8px;align-items:flex-start;box-shadow:0 12px 48px rgba(255,212,0,0.5)}
.pv-tile .tile-icon-img{opacity:0.9}
.pv-tile .tile-value{font-size:36px}
.pv-tile .tile-details{color:rgba(0,0,0,0.7)}
.battery-tile{grid-column:2;grid-row:1/span 2;background:var(--color-battery);color:#fff;display:flex;flex-direction:column;box-shadow:0 12px 48px rgba(119,158,203,0.5)}
.battery-tile.discharging{background:var(--color-grid)}
.battery-tile.charging{background:var(--color-battery)}
.battery-tile .tile-header{display:flex;gap:8px;align-items:flex-start}
.battery-tile .battery-info{flex:1}
.battery-tile .tile-value{font-size:32px}
.load-tile{grid-column:1;grid-row:2;background:var(--color-load);color:#fff;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:4px;box-shadow:0 12px 48px rgba(3,173,54,0.5)}
.load-tile .tile-icon-img{display:none}
.load-tile .tile-value{font-size:32px}
.inverter-tile{grid-column:1/span 2;grid-row:3;background:var(--color-card);display:grid;grid-template-columns:1fr 1fr;gap:10px;padding:12px;border:4px solid var(--color-inverter);box-shadow:inset 0 0 32px rgba(211,211,211,0.3)}
.inverter-left,.inverter-right{display:flex;flex-direction:column;align-items:center}
.inverter-right{border-left:3px solid var(--color-grid);padding-left:10px}
.inverter-right.selling{border-left-color:var(--color-load)}
.inverter-tile .tile-value{font-size:32px;color:var(--color-text);text-align:center;margin-bottom:4px}
.phase-bars{display:flex;flex-direction:column;gap:2px;width:100%}
.phase-row{display:flex;align-items:center;gap:4px;font-size:11px;color:var(--color-text)}
.phase-dot{width:8px;height:8px;border-radius:50%;background:var(--color-load);flex-shrink:0}
.grid-dot{background:var(--color-grid)}
.phase-bar{flex:1;height:6px;background:#e8e8e8;border-radius:3px;overflow:hidden}
.phase-fill{height:100%;background:var(--color-load);border-radius:3px;transition:width .5s ease}
.grid-fill{background:var(--color-grid)}
.phase-value{min-width:48px;text-align:right;font-weight:500;font-size:10px}
.grid-bars.selling .grid-fill{background:var(--color-load)}
.grid-bars.selling .grid-dot{background:var(--color-load)}
.inverter-right.selling .tile-value{color:var(--color-load)}
.inverter-info{font-size:7px;color:var(--color-text-muted);font-family:monospace;margin-top:3px}
.inverter-temp{top:auto;bottom:6px;right:6px}
.bottom-tile{grid-column:1/span 2;grid-row:4;background:var(--color-grid);color:#fff;display:grid;grid-template-columns:1fr 1fr;box-shadow:0 12px 48px rgba(194,89,100,0.5)}
.bottom-tile.selling{background:var(--color-load);box-shadow:0 12px 48px rgba(3,173,54,0.5)}
.bottom-tile .tile-value{font-size:28px}
.bottom-left,.bottom-right{padding:8px;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:3px}
.right-section{display:flex;flex-direction:column;gap:8px}
.stats-row{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.stat-tile{background:var(--color-card);border-radius:var(--radius-large);padding:10px 12px;display:flex;align-items:center;gap:10px;box-shadow:0 8px 32px var(--color-card-shadow)}
.stat-icon-img{width:44px;height:44px;object-fit:contain}
.stat-values{flex:1}
.stat-value{font-size:14px;font-weight:600;display:flex;align-items:baseline;gap:3px}
.stat-value.secondary{font-size:13px;color:var(--color-text-muted)}
.stat-value.secondary.green{color:var(--color-load)}
.stat-unit{font-size:10px;font-weight:400;color:var(--color-text-muted)}
.stat-value.positive{color:var(--color-load)}
.stat-value.negative{color:var(--color-grid)}
.clock-tile{background:var(--color-card);border-radius:var(--radius-large);padding:6px 16px;text-align:center;box-shadow:0 8px 32px var(--color-card-shadow)}
.clock-tile #clock{font-size:28px;font-weight:700;font-variant-numeric:tabular-nums;letter-spacing:2px}
.intelligence-tile{background:var(--color-card);border-radius:var(--radius-large);padding:10px 14px;box-shadow:0 8px 32px var(--color-card-shadow);display:flex;align-items:center;justify-content:space-between}
.intelligence-title{font-size:14px;font-weight:500;color:var(--color-text-light)}
.intelligence-badges{display:flex;gap:6px}
.intelligence-badge{padding:4px 10px;border-radius:var(--radius-small);font-size:11px;font-weight:700}
.intelligence-badge.savings{background:#e3f2fd;color:#1976d2}
.intelligence-badge.mode{background:var(--color-load);color:#fff}
.intelligence-badge.mode.charge{background:var(--color-grid)}
.intelligence-badge.mode.discharge{background:var(--color-battery)}
.chart-tile{background:var(--color-card);border-radius:var(--radius-large);padding:12px;box-shadow:0 8px 32px var(--color-card-shadow);display:flex;flex-direction:column;gap:8px;flex:1}
.chart-container{flex:1;min-height:100px;position:relative}
.chart-container canvas{width:100%!important;height:100%!important}
.spot-price-row{display:flex;justify-content:center}
.spot-price-badge{background:linear-gradient(135deg,#ffeb3b,#ffc107);color:#333;padding:6px 16px;border-radius:var(--radius-medium);font-size:15px;font-weight:700}
.debug-row{display:flex;gap:8px;align-items:stretch}
.status-bar{flex:1;background:var(--color-card);border-radius:var(--radius-small);padding:6px 10px;display:flex;align-items:center;gap:6px;font-size:11px;box-shadow:0 4px 16px var(--color-card-shadow)}
.status-indicator{font-size:10px}
.status-indicator.ok{color:var(--color-load)}
.status-indicator.error{color:var(--color-grid)}
.status-indicator.loading{color:#ff9800;animation:pulse 1s infinite}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.3}}
.btn-screenshot{background:var(--color-card);border-radius:var(--radius-small);padding:6px 12px;text-decoration:none;font-size:16px;box-shadow:0 4px 16px var(--color-card-shadow);display:flex;align-items:center;justify-content:center;transition:transform .2s}
.btn-screenshot:hover{transform:scale(1.05)}
@media(max-width:700px){.dashboard{grid-template-columns:1fr}.left-section{grid-template-columns:1fr 1fr}.tile-value{font-size:24px}.clock-tile #clock{font-size:22px}}
.updated{animation:flash .3s ease}@keyframes flash{0%{opacity:1}50%{opacity:.6}100%{opacity:1}})rawliteral";

const char APP_JS[] = R"rawliteral((function(){'use strict';const REFRESH_INTERVAL=5000;const el={pvPower:document.getElementById('pvPower'),pv1Power:document.getElementById('pv1Power'),pv2Power:document.getElementById('pv2Power'),pvPercent:document.getElementById('pvPercent'),pvToday:document.getElementById('pvToday'),pvTotal:document.getElementById('pvTotal'),soc:document.getElementById('soc'),batteryPowerAbs:document.getElementById('batteryPowerAbs'),batteryTime:document.getElementById('batteryTime'),batteryTemp:document.getElementById('batteryTemp'),batteryTile:document.getElementById('batteryTile'),batteryChargedToday:document.getElementById('batteryChargedToday'),batteryDischargedToday:document.getElementById('batteryDischargedToday'),inverterPower:document.getElementById('inverterPower'),L1Power:document.getElementById('L1Power'),L2Power:document.getElementById('L2Power'),L3Power:document.getElementById('L3Power'),L1Bar:document.getElementById('L1Bar'),L2Bar:document.getElementById('L2Bar'),L3Bar:document.getElementById('L3Bar'),inverterSN:document.getElementById('inverterSN'),inverterMode:document.getElementById('inverterMode'),inverterTemp:document.getElementById('inverterTemp'),gridPower:document.getElementById('gridPower'),gridPowerDisplay:document.getElementById('gridPowerDisplay'),gridL1Power:document.getElementById('gridL1Power'),gridL2Power:document.getElementById('gridL2Power'),gridL3Power:document.getElementById('gridL3Power'),gridL1Bar:document.getElementById('gridL1Bar'),gridL2Bar:document.getElementById('gridL2Bar'),gridL3Bar:document.getElementById('gridL3Bar'),bottomTile:document.getElementById('bottomTile'),gridBars:document.getElementById('gridBars'),gridSection:document.getElementById('gridSection'),gridBuyToday:document.getElementById('gridBuyToday'),gridSellToday:document.getElementById('gridSellToday'),loadPower:document.getElementById('loadPower'),loadTile:document.getElementById('loadTile'),homeLoadPower:document.getElementById('homeLoadPower'),loadToday:document.getElementById('loadToday'),selfUsePercent:document.getElementById('selfUsePercent'),intelligenceSavings:document.getElementById('intelligenceSavings'),intelligenceMode:document.getElementById('intelligenceMode'),spotPrice:document.getElementById('spotPrice'),clock:document.getElementById('clock'),statusIndicator:document.getElementById('statusIndicator'),statusText:document.getElementById('statusText')};async function fetchData(){try{setStatus('loading','Updating...');const r=await fetch('/api/data');if(!r.ok)throw new Error('HTTP '+r.status);const d=await r.json();updateUI(d);setStatus('ok','Connected');}catch(e){console.error(e);setStatus('error','Connection error');}}function updateUI(d){const pvP=(d.pv1Power||0)+(d.pv2Power||0)+(d.pv3Power||0)+(d.pv4Power||0);upd(el.pvPower,pvP);upd(el.pv1Power,d.pv1Power||0);upd(el.pv2Power,d.pv2Power||0);el.pvPercent.textContent=Math.min(100,Math.round(pvP/100))+'%';upd(el.soc,d.soc||0);const bp=d.batteryPower||0;upd(el.batteryPowerAbs,Math.abs(bp));if(d.batteryCapacityWh&&bp!==0){const rWh=(d.soc/100)*d.batteryCapacityWh;if(bp>0){el.batteryTime.textContent=fmtT(rWh/bp);el.batteryTile.classList.add('discharging');el.batteryTile.classList.remove('charging');}else{const toFill=((100-d.soc)/100)*d.batteryCapacityWh;el.batteryTime.textContent=fmtT(toFill/Math.abs(bp));el.batteryTile.classList.remove('discharging');el.batteryTile.classList.add('charging');}}else{el.batteryTime.textContent='';el.batteryTile.classList.remove('discharging','charging');}if(el.batteryTemp)el.batteryTemp.textContent=(d.batteryTemperature||'--')+'°C';upd(el.batteryChargedToday,(d.batteryChargedToday||0).toFixed(1));upd(el.batteryDischargedToday,(d.batteryDischargedToday||0).toFixed(1));const invP=(d.L1Power||0)+(d.L2Power||0)+(d.L3Power||0);upd(el.inverterPower,invP);upd(el.L1Power,d.L1Power||0);upd(el.L2Power,d.L2Power||0);upd(el.L3Power,d.L3Power||0);const mxP=5000;el.L1Bar.style.width=Math.min(100,Math.abs(d.L1Power||0)/mxP*100)+'%';el.L2Bar.style.width=Math.min(100,Math.abs(d.L2Power||0)/mxP*100)+'%';el.L3Bar.style.width=Math.min(100,Math.abs(d.L3Power||0)/mxP*100)+'%';el.inverterSN.textContent=d.sn||'--';if(el.inverterTemp)el.inverterTemp.textContent=(d.inverterTemperature||'--')+'°C';const modes={0:'UNKNOWN',1:'SELF USE',2:'CHARGE',3:'DISCHARGE',4:'HOLD',5:'NORMAL'};const modeCss={0:'',1:'self-use',2:'charge',3:'discharge',4:'hold',5:'normal'};const mode=modes[d.inverterMode]||'NORMAL';const modeCl=modeCss[d.inverterMode]||'';el.inverterMode.textContent=mode;if(el.intelligenceMode){el.intelligenceMode.textContent=mode;el.intelligenceMode.className='intelligence-badge mode';if(modeCl)el.intelligenceMode.classList.add(modeCl);}const gP=(d.gridPowerL1||0)+(d.gridPowerL2||0)+(d.gridPowerL3||0);upd(el.gridPower,Math.abs(gP));if(el.gridPowerDisplay)upd(el.gridPowerDisplay,gP);upd(el.gridL1Power,d.gridPowerL1||0);upd(el.gridL2Power,d.gridPowerL2||0);upd(el.gridL3Power,d.gridPowerL3||0);el.gridL1Bar.style.width=Math.min(100,Math.abs(d.gridPowerL1||0)/mxP*100)+'%';el.gridL2Bar.style.width=Math.min(100,Math.abs(d.gridPowerL2||0)/mxP*100)+'%';el.gridL3Bar.style.width=Math.min(100,Math.abs(d.gridPowerL3||0)/mxP*100)+'%';const sell=gP<0;if(el.bottomTile)el.bottomTile.classList.toggle('selling',sell);if(el.gridBars)el.gridBars.classList.toggle('selling',sell);if(el.gridSection)el.gridSection.classList.toggle('selling',sell);upd(el.gridBuyToday,(d.gridBuyToday||0).toFixed(1));upd(el.gridSellToday,(d.gridSellToday||0).toFixed(1));const lP=d.loadPower||0;upd(el.loadPower,lP);if(el.homeLoadPower)upd(el.homeLoadPower,lP);upd(el.pvToday,(d.pvToday||0).toFixed(1));upd(el.pvTotal,(d.pvTotal||0).toFixed(0));upd(el.loadToday,(d.loadToday||0).toFixed(1));const lT=d.loadToday||0,gB=d.gridBuyToday||0;const su=lT>0?Math.round(((lT-gB)/lT)*100):0;upd(el.selfUsePercent,Math.max(0,Math.min(100,su)));if(el.intelligenceSavings)el.intelligenceSavings.textContent='+12 CZK';if(el.spotPrice)el.spotPrice.textContent='3,11 CZK / kWh';}function upd(e,v){if(!e)return;const t=String(v);if(e.textContent!==t){e.textContent=t;e.classList.add('updated');setTimeout(()=>e.classList.remove('updated'),300);}}function fmtT(h){if(!isFinite(h)||h<0||h>99)return'--';const d=Math.floor(h/24),hr=Math.floor(h%24),mn=Math.floor((h-Math.floor(h))*60);if(d>0)return d+'d '+hr+'h '+mn+'m';if(hr>0)return hr+'h '+mn+'m';return mn+'m';}function setStatus(s,t){el.statusIndicator.className='status-indicator '+s;el.statusText.textContent=t;}function updClock(){const n=new Date();el.clock.textContent=String(n.getHours()).padStart(2,'0')+':'+String(n.getMinutes()).padStart(2,'0');}updClock();setInterval(updClock,1000);fetchData();setInterval(fetchData,REFRESH_INTERVAL);})();)rawliteral";
