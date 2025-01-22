#pragma once

#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <driver/i2c.h>

/*******************************************************************************
 * Please define the corresponding macros based on the board you have purchased.
 * CrowPanel_43 means CrowPanel 4.3inch Board
 * CrowPanel_50 means CrowPanel 5.0inch Board
 * CrowPanel_70 means CrowPanel 7.0inch Board
 ******************************************************************************/

#define screenWidth 800
#define screenHeight 480
class LGFX : public lgfx::LGFX_Device
{
public:
    lgfx::Bus_RGB _bus_instance;
    lgfx::Panel_RGB _panel_instance;
    lgfx::Light_PWM _light_instance;
    LGFX(void)
    {
        {
            auto cfg = _panel_instance.config();
            cfg.memory_width = screenWidth;
            cfg.memory_height = screenHeight;
            cfg.panel_width = screenWidth;
            cfg.panel_height = screenHeight;
            cfg.offset_x = 0;
            cfg.offset_y = 0;
            _panel_instance.config(cfg);
        }
#if CROW_PANEL_ADVANCE
        {
            auto cfg = _panel_instance.config_detail();

            cfg.use_psram = 1;

            _panel_instance.config_detail(cfg);
        }

        {
            auto cfg = _bus_instance.config();
            cfg.panel = &_panel_instance;
            cfg.pin_d0 = GPIO_NUM_21;  // B0
            cfg.pin_d1 = GPIO_NUM_47;  // B1
            cfg.pin_d2 = GPIO_NUM_48;  // B2
            cfg.pin_d3 = GPIO_NUM_45;  // B3
            cfg.pin_d4 = GPIO_NUM_38;  // B4
            cfg.pin_d5 = GPIO_NUM_9;   // G0
            cfg.pin_d6 = GPIO_NUM_10;  // G1
            cfg.pin_d7 = GPIO_NUM_11;  // G2
            cfg.pin_d8 = GPIO_NUM_12;  // G3
            cfg.pin_d9 = GPIO_NUM_13;  // G4
            cfg.pin_d10 = GPIO_NUM_14; // G5
            cfg.pin_d11 = GPIO_NUM_7;  // R0
            cfg.pin_d12 = GPIO_NUM_17; // R1
            cfg.pin_d13 = GPIO_NUM_18; // R2
            cfg.pin_d14 = GPIO_NUM_3;  // R3
            cfg.pin_d15 = GPIO_NUM_46; // R4

            cfg.pin_henable = GPIO_NUM_42;
            cfg.pin_vsync = GPIO_NUM_41;
            cfg.pin_hsync = GPIO_NUM_40;
            cfg.pin_pclk = GPIO_NUM_39;
            cfg.freq_write = 16000000;

            cfg.hsync_polarity = 0;
            cfg.hsync_front_porch = 8;
            cfg.hsync_pulse_width = 4;
            cfg.hsync_back_porch = 8;
            cfg.vsync_polarity = 0;
            cfg.vsync_front_porch = 8;
            cfg.vsync_pulse_width = 4;
            cfg.vsync_back_porch = 8;
            cfg.pclk_idle_high = 1;

            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }
#elif CROW_PANEL
        {
            auto cfg = _bus_instance.config();
            cfg.panel = &_panel_instance;

            cfg.pin_d0 = GPIO_NUM_15;  // B0
            cfg.pin_d1 = GPIO_NUM_7;  // B1
            cfg.pin_d2 = GPIO_NUM_6; // B2
            cfg.pin_d3 = GPIO_NUM_5;  // B3
            cfg.pin_d4 = GPIO_NUM_4;  // B4

            cfg.pin_d5 = GPIO_NUM_9;  // G0
            cfg.pin_d6 = GPIO_NUM_46;  // G1
            cfg.pin_d7 = GPIO_NUM_3;  // G2
            cfg.pin_d8 = GPIO_NUM_8; // G3
            cfg.pin_d9 = GPIO_NUM_16; // G4
            cfg.pin_d10 = GPIO_NUM_1; // G5

            cfg.pin_d11 = GPIO_NUM_14; // R0
            cfg.pin_d12 = GPIO_NUM_21; // R1
            cfg.pin_d13 = GPIO_NUM_47; // R2
            cfg.pin_d14 = GPIO_NUM_48; // R3
            cfg.pin_d15 = GPIO_NUM_45; // R4

            cfg.pin_henable = GPIO_NUM_41;
            cfg.pin_vsync = GPIO_NUM_40;
            cfg.pin_hsync = GPIO_NUM_39;
            cfg.pin_pclk = GPIO_NUM_0;
            cfg.freq_write = 14000000;

            cfg.hsync_polarity    = 0;
            cfg.hsync_front_porch = 40;
            cfg.hsync_pulse_width = 48;
            cfg.hsync_back_porch  = 40;
            
            cfg.vsync_polarity    = 0;
            cfg.vsync_front_porch = 1;
            cfg.vsync_pulse_width = 31;
            cfg.vsync_back_porch  = 13;

            cfg.pclk_active_neg = 1;
            cfg.de_idle_high = 0;
            cfg.pclk_idle_high = 0;

            _bus_instance.config(cfg);
            _panel_instance.setBus(&_bus_instance);
        }
#endif
        {
            auto cfg = _light_instance.config();
            cfg.pin_bl = GPIO_NUM_2;
            _light_instance.config(cfg);
            _panel_instance.light(&_light_instance);
        }

        setPanel(&_panel_instance);
    }
};

LGFX tft;
