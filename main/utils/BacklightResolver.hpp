#pragma once

#include "../Inverters/InverterResult.hpp"
#include "driver/i2c_master.h"
#define BACKLIGHT_TOUCH_TIMEOUT 15000

#if CONFIG_CROWPANEL_ADVANCE

#endif

class BacklightResolver
{
private:
    long lastTouchTime = 0;
    i2c_master_bus_handle_t bus_handle;
    i2c_master_dev_handle_t dev_handle;

    bool i2cScanForAddress(uint8_t address)
    {
        bool found = i2c_master_probe(bus_handle, address, -1) == ESP_OK; // Example for I2C master probe;
        log_d("I2C scan for address 0x%02X: %s", address, found ? "found" : "not found");
        return found;
    }

public:
    void setup()
    {
#if CONFIG_CROWPANEL_ADVANCE
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

        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_mst_config, &bus_handle));

        if (i2cScanForAddress(0x30))
        {
            i2c_device_config_t dev_cfg = {
                .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                .device_address = 0x30,
                .scl_speed_hz = 800000,
            };
            i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);

            uint8_t command = 0x10;
            i2c_master_transmit(dev_handle, &command, 1, -1);
        }

        if (i2cScanForAddress(0x18))
        {
            i2c_device_config_t dev_cfg = {
                .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                .device_address = 0x18,
                .scl_speed_hz = 800000,
            };

            i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev_handle);

            uint8_t setupCommand[2] = {0x03, 0x00};
            i2c_master_transmit(dev_handle, setupCommand, 2, -1);

            uint8_t enableBacklightCommand[2] = {0x01, 0x02};
            i2c_master_transmit(dev_handle, enableBacklightCommand, 2, -1);

            delay(100);
        }
#endif

#if CONFIG_CROWPANEL
        pinMode(GPIO_NUM_2, OUTPUT);
        digitalWrite(GPIO_NUM_2, HIGH); // Enable backlight
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
            uint8_t command = brightness / 16;
            i2c_master_transmit(dev_handle, &command, 1, -1);
        }
#endif

#if CONFIG_CROWPANEL
        digitalWrite(GPIO_NUM_2, brightness > 0 ? HIGH : LOW); // Enable backlight
#endif
    }
};