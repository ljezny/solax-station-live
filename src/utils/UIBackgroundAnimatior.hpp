#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "ui/ui.h"

struct UIBackgroundAnimatorVariables
{
    lv_obj_t *obj;
    lv_color_t startColor;
    lv_color_t endColor;
};

void animation_set_bg_color(UIBackgroundAnimatorVariables *variables, int32_t value)
{
    lv_color_t c = lv_color_mix(variables->startColor, variables->endColor, value);
    lv_color_t old = lv_obj_get_style_bg_color(variables->obj, 0);
    if (old.red == c.red && old.green == c.green && old.blue == c.blue)
    {
        return;
    }
    lv_obj_set_style_bg_color(variables->obj, c, 0);
}

class UIBackgroundAnimator
{
private:
    lv_anim_t anim;
    UIBackgroundAnimatorVariables variables;

public:
    UIBackgroundAnimator(uint16_t duration)
    {
        lv_anim_init(&anim);
        lv_anim_set_time(&anim, duration);
        lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)animation_set_bg_color);
    }

    ~UIBackgroundAnimator()
    {
        lv_anim_del(&anim, (lv_anim_exec_xcb_t)animation_set_bg_color);
    }

    void animate(lv_obj_t *obj, lv_color_t endColor)
    {
        lv_color_t startColor = lv_obj_get_style_bg_color(obj, 0);
        if (startColor.red == endColor.red && startColor.green == endColor.green && startColor.blue == endColor.blue)
        {
            lv_obj_set_style_bg_color(obj, startColor, 0);
            return;
        }
        variables.obj = obj;
        variables.startColor = startColor;
        variables.endColor = endColor;
        lv_anim_set_var(&anim, &variables);
        lv_anim_set_values(&anim, 0, 255);
        lv_anim_start(&anim);
    }
};