#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "ui/ui.h"
#include "UnitFormatter.hpp"

struct UITextChangeAnimatorVariables {
    lv_obj_t *label = nullptr;
    Unit_t unit;
    int32_t from = -1;
    int32_t to = -1;
};

void animation_set_text(UITextChangeAnimatorVariables *variables, int32_t value) {
    // Guard against deleted labels during screen transitions
    if (variables == nullptr || variables->label == nullptr) {
        return;
    }
    int32_t v = value;
    int32_t step = abs(variables->to - variables->from) / 6;
    if(v != variables->to && step > 0) {
        v = (v / step) * step;
    }
    String text = format(variables->unit, v).value;
    if(!strcmp(lv_label_get_text(variables->label), text.c_str())) {
        return;
    }
    lv_label_set_text(variables->label, text.c_str());
}

// Callback to null out label pointer when LVGL deletes the object
static void onLabelDeleted(lv_event_t *e) {
    UITextChangeAnimatorVariables *vars = (UITextChangeAnimatorVariables*)lv_event_get_user_data(e);
    if (vars) {
        vars->label = nullptr;
    }
}

class UITextChangeAnimator {
    private:
        lv_anim_t anim;
        UITextChangeAnimatorVariables variables;
    public:
    
        UITextChangeAnimator(Unit_t unit, uint16_t duration) {
            variables.unit = unit;
            variables.label = nullptr;
            lv_anim_init(&anim);
            lv_anim_set_exec_cb(&anim, (lv_anim_exec_xcb_t)animation_set_text);
            lv_anim_set_var(&anim, &variables);
            lv_anim_set_time(&anim, duration);
            lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);       
            lv_anim_set_early_apply(&anim, true); 
        }

        ~UITextChangeAnimator() {
            // Remove delete callback from label if it still exists
            if (variables.label != nullptr) {
                lv_obj_remove_event_cb_with_user_data(variables.label, onLabelDeleted, &variables);
            }
            // Set label to null and delete animation
            variables.label = nullptr;
            lv_anim_del(&variables, (lv_anim_exec_xcb_t)animation_set_text);
        }

        void animate(lv_obj_t *label, int32_t from, int32_t to) {
            if (variables.to == to && variables.label == label) {
                return; // No change needed
            }
            
            // If label changed, update delete callback
            if (variables.label != label) {
                // Remove callback from old label
                if (variables.label != nullptr) {
                    lv_obj_remove_event_cb_with_user_data(variables.label, onLabelDeleted, &variables);
                }
                // Add callback to new label
                if (label != nullptr) {
                    lv_obj_add_event_cb(label, onLabelDeleted, LV_EVENT_DELETE, &variables);
                }
            }
            
            variables.label = label;
            variables.from = from;
            variables.to = to;
            lv_anim_set_values(&anim, from, to);
            lv_anim_start(&anim);
        }
};