#pragma once

#include <Arduino.h>
#include "driver/i2c_master.h"
#include "TAMC_GT911.h"

#if CONFIG_CROWPANEL
#define TOUCH_GT911_SCL 20
#define TOUCH_GT911_SDA 19
#endif
#if CONFIG_CROWPANEL_ADVANCE
#define TOUCH_GT911_SCL GPIO_NUM_16
#define TOUCH_GT911_SDA GPIO_NUM_15
#endif

#define TOUCH_GT911_ROTATION ROTATION_INVERTED
#define TOUCH_MAP_X1 800
#define TOUCH_MAP_X2 0
#define TOUCH_MAP_Y1 480
#define TOUCH_MAP_Y2 0

class Touch
{
public:
    uint16_t touchX, touchY;

    void init(i2c_master_bus_handle_t bus_handle)
    {
        ts.begin(bus_handle);
        ts.setRotation(TOUCH_GT911_ROTATION);
        log_d("Touch initialized with rotation %d", TOUCH_GT911_ROTATION);
    }
    bool hasTouch()
    {
        ts.read();
        if (ts.isTouched)
        {
            touchX = map(ts.points[0].x, TOUCH_MAP_X1, TOUCH_MAP_X2, 0, TOUCH_MAP_X1 - 1);
            touchY = map(ts.points[0].y, TOUCH_MAP_Y1, TOUCH_MAP_Y2, 0, TOUCH_MAP_Y1 - 1);
            return true;
        }

        return false;
    }

private:
    TAMC_GT911 ts = TAMC_GT911(TOUCH_MAP_X1, TOUCH_MAP_Y1);
};