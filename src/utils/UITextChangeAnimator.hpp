#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "ui/ui.h"
#include "UnitFormatter.hpp"

struct UITextChangeAnimatorVariables {
    lv_obj_t *label;
    Unit_t unit;
    int32_t from;
    int32_t to;
};

void animation_set_text(UITextChangeAnimatorVariables *variables, int32_t value) {
    int32_t v = value;
    int32_t step = abs(variables->to - variables->from) / 10;
    if(v != variables->to && step > 0) {
        v = (v / step) * step;
    }
    lv_label_set_text(variables->label, format(variables->unit, v).value.c_str());
}

class UITextChangeAnimator {
    private:
        lv_anim_t anim;
        UITextChangeAnimatorVariables variables;
    public:
    
        UITextChangeAnimator(Unit_t unit, uint16_t duration) {
            variables.unit = unit;
            lv_anim_init(&anim);
            lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)animation_set_text);
            lv_anim_set_var(&anim, &variables);
            lv_anim_set_time(&anim, duration);
            lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);       
            lv_anim_set_early_apply(&anim, true); 
        }

        ~UITextChangeAnimator() {
            lv_anim_del(&anim, (lv_anim_exec_xcb_t)animation_set_text);
        }

        void animate(lv_obj_t *label, int32_t from, int32_t to) {
            variables.label = label;
            variables.from = from;
            variables.to = to;
            lv_anim_set_values(&anim, from, to);
            lv_anim_start(&anim);
        }
};