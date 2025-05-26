#pragma once

#include "../Inverters/InverterResult.hpp"

#define BACKLIGHT_TOUCH_TIMEOUT 15000


#if CONFIG_CROWPANEL_ADVANCE
// #include <PCA9557.h>
// PCA9557 io(0x18, &Wire);
#include "driver/i2c_master.h"
#endif

class BacklightResolver
{
private:
    long lastTouchTime = 0;

    bool i2cScanForAddress(uint8_t address)
    {
        // Wire.beginTransmission(address);
        // return (Wire.endTransmission() == 0);
        return true;
    }

public:
    void setup()
    {
#if CONFIG_CROWPANEL_ADVANCE
        // Wire.begin(15, 16);
        // Wire.beginTransmission(0x30);
        // Wire.write(0x10);
        // int error = Wire.endTransmission();
        // delay(500);

        // if (i2cScanForAddress(0x18)) //old V1.0
        // {
        //     io.pinMode(1, OUTPUT);
        //     io.digitalWrite(1, 1);
        // }
        // else if (i2cScanForAddress(0x30)) //new V1.2
        // {
        //     Wire.beginTransmission(0x30);
        //     Wire.write(0x10);
        //     int error = Wire.endTransmission();
        //     log_e("PCA9557 error: %d", error);
        // }

        i2c_master_bus_config_t i2c_mst_config = {
            .i2c_port = I2C_NUM_0,
            .sda_io_num = GPIO_NUM_15,
            .scl_io_num = GPIO_NUM_16,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags = {
                .enable_internal_pullup = true,
            },
        };

        i2c_master_bus_handle_t bus_handle;
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_mst_config, &bus_handle));

        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = 0x30,
            .scl_speed_hz = 800000,
        };
        i2c_master_dev_handle_t dev_handle;
        i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);
        
        uint8_t command = 0x10;
        for (int i = 0; i < 10; i++)
        {
            i2c_master_transmit(dev_handle, &command, 1, -1);
            delay(100);
        }

#endif
        setBacklightAnimated(255);
    }

    void resolve(InverterData_t inverterData)
    {
        int pvPower = inverterData.pv1Power + inverterData.pv2Power + inverterData.pv3Power + inverterData.pv4Power;
        int brightness = 100;
        if (inverterData.status == DONGLE_STATUS_OK)
        {
            if (pvPower > 2000)
            {
                log_d("Setting brightness to 100%");
                brightness = 255;
            }
            else if (pvPower > 0)
            {
                log_d("Setting brightness to 80%");
                brightness = 192;
            }
            else
            {
                log_d("Setting brightness to 20%");
                brightness = 16;
            }
        }

        if ((millis() - this->lastTouchTime) > BACKLIGHT_TOUCH_TIMEOUT)
        {
            setBacklightAnimated(brightness);
        }
        else
        {
            setBacklightAnimated(255);
        }
    }

    void touch()
    {
        this->lastTouchTime = millis();
        setBacklightAnimated(255);
    }

    void setBacklightAnimated(int brightness)
    {
#if CONFIG_CROWPANEL_ADVANCE
        if (i2cScanForAddress(0x30)) // new V1.2
        {
            // Wire.beginTransmission(0x30);
            // Wire.write(brightness == 255 ? 0x10 : 0x07);
            // Wire.endTransmission();
        }
#else
        // for (int i = tft.getBrightness(); i != brightness; i += (brightness > tft.getBrightness()) ? 1 : -1)
        // {
        //     tft.setBrightness(i);
        //     delay(5);
        // }
#endif
    }
};