#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "ui/ui.h"
#include "UnitFormatter.hpp"

#define MAX_TEXT_LENGTH 16  // Adjust based on expected unit-formatted text length

struct UITextChangeAnimatorVariables {
    lv_obj_t *label;
    Unit_t unit;
    int32_t from;
    int32_t to;
    int32_t step;  // Precomputed step size
    char lastText[MAX_TEXT_LENGTH];  // Cache for last text to avoid redundant updates
};

void animation_set_text(UITextChangeAnimatorVariables *variables, int32_t value) {
    int32_t v = value;
    // Round to nearest step to avoid jittery updates
    if (v != variables->to && variables->step > 0) {
        v = (v / variables->step) * variables->step;
    }

    // Format value into a fixed-size buffer
    char text[MAX_TEXT_LENGTH];
    snprintf(text, MAX_TEXT_LENGTH, "%s", format(variables->unit, v).value.c_str());

    // Only update if text has changed
    if (strncmp(variables->lastText, text, MAX_TEXT_LENGTH) != 0) {
        lv_label_set_text(variables->label, text);
        strncpy(variables->lastText, text, MAX_TEXT_LENGTH);
    }
}

class UITextChangeAnimator {
private:
    lv_anim_t anim;
    UITextChangeAnimatorVariables variables;

public:
    UITextChangeAnimator(Unit_t unit, uint16_t duration) {
        variables.unit = unit;
        variables.lastText[0] = '\0';  // Initialize empty lastText
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
        // Precompute step size
        variables.step = abs(to - from) / 20;
        variables.lastText[0] = '\0';  // Reset lastText to force initial update
        lv_anim_set_values(&anim, from, to);
        lv_anim_start(&anim);
    }
};