#pragma once   

#include <Arduino.h>
#include <lvgl.h>
#include "ui/ui.h"

struct UIBackgroundAnimatorVariables {
    lv_obj_t *obj;
    lv_color_t startColor;
    lv_color_t endColor;
};

void animation_set_bg_color(UIBackgroundAnimatorVariables *variables, int32_t value) {
    lv_color_t c = lv_color_mix(variables->endColor, variables->startColor, value);
    lv_obj_set_style_bg_color(variables->obj, c, LV_PART_MAIN);
}

class UIBackgroundAnimator {
    private:
        lv_anim_t anim;
        UIBackgroundAnimatorVariables variables;
    public:
    
        UIBackgroundAnimator(uint16_t duration) {
            lv_anim_init(&anim);
            lv_anim_set_time(&anim, duration);
            lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)animation_set_bg_color);
            lv_anim_set_var(&anim, &variables);            
            lv_anim_set_values(&anim, 0, 255);
        }

        ~UIBackgroundAnimator() {
            lv_anim_delete(&anim, (lv_anim_exec_xcb_t)animation_set_bg_color);
        }

        void animate(lv_obj_t *obj, lv_color_t endColor) {
            variables.obj = obj;
            lv_color_t currentColor = lv_obj_get_style_bg_color(obj, LV_PART_MAIN);
            if (lv_color_eq(currentColor, endColor)) {
                return; // No need to animate if the color is already the same
            }
            variables.startColor = lv_obj_get_style_bg_color(obj, LV_PART_MAIN); 
            variables.endColor = endColor;
            lv_anim_start(&anim);
        }
};