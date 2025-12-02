#pragma once

#include <Arduino.h>
#include <esp_http_server.h>
#include <lvgl.h>
#include "../gfx_conf.h"

// Forward declaration
extern LGFX tft;
extern SemaphoreHandle_t lvgl_mutex;

// HTML page with embedded screenshot viewer
static const char *SCREENSHOT_HTML = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Solar Station Live - Screenshot</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, sans-serif;
            background: linear-gradient(135deg, #1a1a2e 0%, #16213e 50%, #0f3460 100%);
            min-height: 100vh;
            display: flex;
            flex-direction: column;
            align-items: center;
            padding: 20px;
            color: #fff;
        }
        .header {
            text-align: center;
            margin-bottom: 20px;
        }
        .header h1 {
            font-size: 1.8rem;
            margin-bottom: 5px;
            background: linear-gradient(90deg, #f39c12, #e74c3c);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
            background-clip: text;
        }
        .header p {
            color: #888;
            font-size: 0.9rem;
        }
        .screenshot-container {
            background: #1e1e1e;
            border-radius: 16px;
            padding: 15px;
            box-shadow: 0 10px 40px rgba(0,0,0,0.5);
            max-width: 100%;
        }
        .screenshot-frame {
            background: #000;
            border-radius: 8px;
            overflow: hidden;
            position: relative;
        }
        #screenshot {
            display: block;
            max-width: 100%;
            height: auto;
            image-rendering: auto;
        }
        .loading {
            position: absolute;
            top: 50%;
            left: 50%;
            transform: translate(-50%, -50%);
            color: #666;
        }
        .controls {
            margin-top: 15px;
            display: flex;
            gap: 10px;
            justify-content: center;
            flex-wrap: wrap;
        }
        .btn {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            border: none;
            padding: 12px 24px;
            border-radius: 8px;
            cursor: pointer;
            font-size: 0.95rem;
            font-weight: 500;
            transition: transform 0.2s, box-shadow 0.2s;
        }
        .btn:hover {
            transform: translateY(-2px);
            box-shadow: 0 5px 20px rgba(102, 126, 234, 0.4);
        }
        .btn:active {
            transform: translateY(0);
        }
        .btn-secondary {
            background: linear-gradient(135deg, #2d3436 0%, #636e72 100%);
        }
        .status {
            margin-top: 15px;
            padding: 10px 15px;
            border-radius: 8px;
            font-size: 0.85rem;
            text-align: center;
        }
        .status.success {
            background: rgba(46, 204, 113, 0.2);
            color: #2ecc71;
        }
        .status.error {
            background: rgba(231, 76, 60, 0.2);
            color: #e74c3c;
        }
        .status.loading {
            background: rgba(241, 196, 15, 0.2);
            color: #f1c40f;
        }
        .info {
            margin-top: 20px;
            font-size: 0.8rem;
            color: #666;
            text-align: center;
        }
        .auto-refresh {
            display: flex;
            align-items: center;
            gap: 10px;
            margin-top: 10px;
            color: #888;
            font-size: 0.9rem;
        }
        .auto-refresh input {
            width: 18px;
            height: 18px;
        }
        @media (max-width: 850px) {
            .screenshot-container {
                padding: 10px;
            }
        }
    </style>
</head>
<body>
    <div class="header">
        <h1>☀️ Solar Station Live</h1>
        <p>Real-time display screenshot</p>
    </div>
    
    <div class="screenshot-container">
        <div class="screenshot-frame">
            <img id="screenshot" src="/screenshot.bmp" alt="Screenshot" onload="onImageLoad()" onerror="onImageError()">
            <div class="loading" id="loadingIndicator">Loading...</div>
        </div>
        
        <div class="controls">
            <button class="btn" onclick="refreshScreenshot()">🔄 Refresh</button>
            <button class="btn btn-secondary" onclick="downloadScreenshot()">💾 Download</button>
        </div>
        
        <div class="auto-refresh">
            <input type="checkbox" id="autoRefresh" onchange="toggleAutoRefresh()">
            <label for="autoRefresh">Auto-refresh every 5 seconds</label>
        </div>
        
        <div class="status" id="status" style="display: none;"></div>
    </div>
    
    <div class="info">
        <p>Screenshot resolution: 800 × 480 pixels</p>
        <p id="timestamp"></p>
    </div>

    <script>
        let autoRefreshInterval = null;
        
        function showStatus(message, type) {
            const status = document.getElementById('status');
            status.textContent = message;
            status.className = 'status ' + type;
            status.style.display = 'block';
            if (type !== 'loading') {
                setTimeout(() => status.style.display = 'none', 3000);
            }
        }
        
        function onImageLoad() {
            document.getElementById('loadingIndicator').style.display = 'none';
            document.getElementById('timestamp').textContent = 'Last updated: ' + new Date().toLocaleTimeString();
            showStatus('Screenshot loaded successfully', 'success');
        }
        
        function onImageError() {
            document.getElementById('loadingIndicator').textContent = 'Failed to load';
            showStatus('Failed to load screenshot', 'error');
        }
        
        function refreshScreenshot() {
            showStatus('Loading screenshot...', 'loading');
            document.getElementById('loadingIndicator').style.display = 'block';
            document.getElementById('loadingIndicator').textContent = 'Loading...';
            const img = document.getElementById('screenshot');
            img.src = '/screenshot.bmp?t=' + Date.now();
        }
        
        function downloadScreenshot() {
            const link = document.createElement('a');
            link.href = '/screenshot.bmp?t=' + Date.now();
            link.download = 'solar-station-screenshot-' + new Date().toISOString().slice(0,19).replace(/:/g, '-') + '.bmp';
            link.click();
            showStatus('Download started', 'success');
        }
        
        function toggleAutoRefresh() {
            const checkbox = document.getElementById('autoRefresh');
            if (checkbox.checked) {
                autoRefreshInterval = setInterval(refreshScreenshot, 5000);
                showStatus('Auto-refresh enabled', 'success');
            } else {
                if (autoRefreshInterval) {
                    clearInterval(autoRefreshInterval);
                    autoRefreshInterval = null;
                }
                showStatus('Auto-refresh disabled', 'success');
            }
        }
        
        // Initial load complete
        document.getElementById('timestamp').textContent = 'Last updated: ' + new Date().toLocaleTimeString();
    </script>
</body>
</html>
)rawliteral";

class ScreenshotServer
{
public:
    ScreenshotServer() : server(nullptr), lvglMutex(nullptr) {}

    void begin(SemaphoreHandle_t mutex)
    {
        lvglMutex = mutex;
        
        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.server_port = 80;
        config.stack_size = 8192;
        config.max_uri_handlers = 4;
        
        if (httpd_start(&server, &config) == ESP_OK)
        {
            // Register URI handlers
            httpd_uri_t indexUri = {
                .uri = "/",
                .method = HTTP_GET,
                .handler = indexHandler,
                .user_ctx = this
            };
            httpd_register_uri_handler(server, &indexUri);

            httpd_uri_t screenshotUri = {
                .uri = "/screenshot.bmp",
                .method = HTTP_GET,
                .handler = screenshotHandler,
                .user_ctx = this
            };
            httpd_register_uri_handler(server, &screenshotUri);

            log_i("Screenshot server started on port 80");
        }
        else
        {
            log_e("Failed to start screenshot server");
        }
    }

    void stop()
    {
        if (server)
        {
            httpd_stop(server);
            server = nullptr;
            log_i("Screenshot server stopped");
        }
    }

private:
    httpd_handle_t server;
    SemaphoreHandle_t lvglMutex;

    static esp_err_t indexHandler(httpd_req_t *req)
    {
        httpd_resp_set_type(req, "text/html");
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
        return httpd_resp_send(req, SCREENSHOT_HTML, strlen(SCREENSHOT_HTML));
    }

    static esp_err_t screenshotHandler(httpd_req_t *req)
    {
        ScreenshotServer *self = (ScreenshotServer *)req->user_ctx;
        
        // Screen dimensions
        const int width = 800;
        const int height = 480;
        const int bytesPerPixel = 2; // RGB565
        const int rowSize = ((width * bytesPerPixel + 3) / 4) * 4; // Rows must be 4-byte aligned for BMP
        const int imageSize = rowSize * height;
        const int fileSize = 54 + imageSize; // BMP header + image data
        
        // BMP header (54 bytes for BITMAPINFOHEADER with RGB565)
        uint8_t bmpHeader[54] = {
            // BMP file header (14 bytes)
            'B', 'M',                       // Signature
            (uint8_t)(fileSize), (uint8_t)(fileSize >> 8), (uint8_t)(fileSize >> 16), (uint8_t)(fileSize >> 24), // File size
            0, 0, 0, 0,                     // Reserved
            54, 0, 0, 0,                    // Pixel data offset
            
            // DIB header (40 bytes - BITMAPINFOHEADER)
            40, 0, 0, 0,                    // Header size
            (uint8_t)(width), (uint8_t)(width >> 8), 0, 0,  // Width
            (uint8_t)(height), (uint8_t)(height >> 8), 0, 0, // Height (positive = bottom-up)
            1, 0,                           // Color planes
            16, 0,                          // Bits per pixel (16 = RGB565)
            0, 0, 0, 0,                     // Compression (0 = BI_RGB, no compression for 16-bit)
            (uint8_t)(imageSize), (uint8_t)(imageSize >> 8), (uint8_t)(imageSize >> 16), (uint8_t)(imageSize >> 24), // Image size
            0x13, 0x0B, 0, 0,               // Horizontal resolution (72 DPI)
            0x13, 0x0B, 0, 0,               // Vertical resolution (72 DPI)
            0, 0, 0, 0,                     // Colors in palette
            0, 0, 0, 0                      // Important colors
        };
        
        httpd_resp_set_type(req, "image/bmp");
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");
        httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=\"screenshot.bmp\"");
        
        // Send BMP header
        if (httpd_resp_send_chunk(req, (const char *)bmpHeader, sizeof(bmpHeader)) != ESP_OK)
        {
            return ESP_FAIL;
        }
        
        // Allocate row buffer with padding
        uint8_t *rowBuffer = (uint8_t *)heap_caps_malloc(rowSize, MALLOC_CAP_DEFAULT);
        if (!rowBuffer)
        {
            log_e("Failed to allocate row buffer");
            httpd_resp_send_chunk(req, NULL, 0);
            return ESP_FAIL;
        }
        
        // Allocate pixel read buffer
        uint16_t *pixelBuffer = (uint16_t *)heap_caps_malloc(width * sizeof(uint16_t), MALLOC_CAP_DEFAULT);
        if (!pixelBuffer)
        {
            log_e("Failed to allocate pixel buffer");
            free(rowBuffer);
            httpd_resp_send_chunk(req, NULL, 0);
            return ESP_FAIL;
        }
        
        // Take LVGL mutex to safely read display
        bool mutexTaken = false;
        if (self->lvglMutex)
        {
            mutexTaken = (xSemaphoreTake(self->lvglMutex, pdMS_TO_TICKS(2000)) == pdTRUE);
        }
        
        if (mutexTaken || !self->lvglMutex)
        {
            // Send rows bottom-up (BMP format)
            for (int y = height - 1; y >= 0; y--)
            {
                // Clear row buffer (for padding)
                memset(rowBuffer, 0, rowSize);
                
                // Read row of pixels from display using LovyanGFX
                tft.readRect(0, y, width, 1, pixelBuffer);
                
                // Convert to BMP format (little-endian RGB565)
                for (int x = 0; x < width; x++)
                {
                    uint16_t pixel = pixelBuffer[x];
                    rowBuffer[x * 2] = pixel & 0xFF;
                    rowBuffer[x * 2 + 1] = (pixel >> 8) & 0xFF;
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
        else
        {
            log_w("Could not acquire LVGL mutex for screenshot");
            // Send gray image as fallback
            memset(rowBuffer, 0x80, rowSize);
            for (int y = 0; y < height; y++)
            {
                httpd_resp_send_chunk(req, (const char *)rowBuffer, rowSize);
            }
        }
        
        free(pixelBuffer);
        free(rowBuffer);
        
        // End chunked response
        httpd_resp_send_chunk(req, NULL, 0);
        
        log_d("Screenshot sent successfully");
        return ESP_OK;
    }
};
