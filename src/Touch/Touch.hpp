#pragma once

#include <Arduino.h>
#include "TAMC_GT911.h"

#define TOUCH_GT911
#define TOUCH_GT911_SCL 20
#define TOUCH_GT911_SDA 19
#define TOUCH_GT911_ROTATION ROTATION_INVERTED
#define TOUCH_MAP_X1 800
#define TOUCH_MAP_X2 0
#define TOUCH_MAP_Y1 480
#define TOUCH_MAP_Y2 0

class Touch {
    public:
        uint16_t touchX, touchY;
        void init() {
            ts.begin();
            ts.setRotation(TOUCH_GT911_ROTATION);
        }
        bool hasTouch() {
            ts.read();
            if (!ts.isTouched)
            {
                return false;
            }
            else
            {
                touchX = map(ts.points[0].x, TOUCH_MAP_X1, TOUCH_MAP_X2, 0, TOUCH_MAP_X1 - 1);
                touchY = map(ts.points[0].y, TOUCH_MAP_Y1, TOUCH_MAP_Y2, 0, TOUCH_MAP_Y1 - 1);
                return true;
            }
        }
    private:
        TAMC_GT911 ts = TAMC_GT911(TOUCH_GT911_SDA, TOUCH_GT911_SCL, max(TOUCH_MAP_X1, TOUCH_MAP_X2), max(TOUCH_MAP_Y1, TOUCH_MAP_Y2));

};