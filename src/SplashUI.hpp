#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "ui/ui.h"

class SplashUI
{
public:
    void show()
    {
        lv_scr_load(ui_Splash);
    }

    void update(String espId, String fwVersion)
    {
        lv_label_set_text(ui_fwVersionLabel, fwVersion.c_str());
        lv_label_set_text(ui_ESPIdLabel, espId.c_str());
    }
};