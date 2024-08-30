#pragma once   

#include <Arduino.h>
#include <lvgl.h>
#include "ui/ui.h"

struct UIBackgroundAnimatorVariables {
    lv_obj_t *obj;
    lv_color_t color;
};

void animation_set_bg_color(UIBackgroundAnimatorVariables *variables, int32_t value) {
    lv_color_t c = lv_color_mix(variables->color, lv_color_white(), value);
    lv_color_t old = lv_obj_get_style_bg_color(variables->obj, 0);
    if(old.full == c.full) {
        return;
    }
    lv_obj_set_style_bg_color(variables->obj, c, 0);
}

class UIBackgroundAnimator {
    private:
        lv_anim_t anim;
        UIBackgroundAnimatorVariables variables;
    public:
    
        UIBackgroundAnimator(uint16_t duration, lv_color_t color) {
            lv_anim_init(&anim);
            variables.color = color;
            lv_anim_set_time(&anim, duration);
            lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)animation_set_bg_color);
        }

        ~UIBackgroundAnimator() {
            lv_anim_del(&anim, (lv_anim_exec_xcb_t)animation_set_bg_color);
        }

        void animate(lv_obj_t *obj, bool from, bool to) {
            
            variables.obj = obj;
            lv_anim_set_var(&anim, &variables);            
            lv_anim_set_values(&anim, from ? 255 : 0, to ? 255 : 0);
        
            lv_anim_start(&anim);
        }
};