#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "ui/ui.h"

#define BALLS_COUNT 4

typedef struct UIBallAnimationItem {
    lv_obj_t *ball;
    lv_anim_t posXAnimation;
    lv_anim_t posYAnimation;
} UIBallAnimationItem_t;

class UIBallAnimator
{
public:
    UIBallAnimator(lv_obj_t *parent, const ui_theme_variable_t *color) {
        for(int i = 0; i < BALLS_COUNT; i++) {
            items[i].ball = lv_obj_create(parent);
            lv_obj_remove_style_all(items[i].ball);
            int radius = 10 * (BALLS_COUNT - i);
            lv_obj_set_width(items[i].ball, radius);
            lv_obj_set_height(items[i].ball, radius);
            lv_obj_clear_flag(items[i].ball, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE );    /// Flags
            lv_obj_set_style_radius(items[i].ball, radius / 2, LV_PART_MAIN| LV_STATE_DEFAULT);
            ui_object_set_themeable_style_property(items[i].ball, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BG_COLOR, color);
            ui_object_set_themeable_style_property(items[i].ball, LV_PART_MAIN| LV_STATE_DEFAULT, LV_STYLE_BG_OPA, _ui_theme_alpha_pvColor);
            
            lv_obj_move_background(items[i].ball);
        }
    }

    ~UIBallAnimator() {
        for(int i = 0; i < BALLS_COUNT; i++) {
            lv_anim_del(&items[i].posXAnimation, (lv_anim_exec_xcb_t) lv_obj_set_x);
            lv_anim_del(&items[i].posYAnimation, (lv_anim_exec_xcb_t) lv_obj_set_y);
            lv_obj_del(items[i].ball);
        }
    }

    void run(lv_area_t start, lv_area_t destination, int duration, int delay, int direction) {
        int centerStartX = start.x1 + (start.x2 - start.x1) / 2;
        int centerStartY = start.y1 + (start.y2 - start.y1) / 2;
        int centerDestinationX = destination.x1 + (destination.x2 - destination.x1) / 2;
        int centerDestinationY = destination.y1 + (destination.y2 - destination.y1) / 2;
        int distanceX = (destination.x1 + (destination.x2 - destination.x1) / 2) - (start.x1 + (start.x2 - start.x1) / 2);
        int distanceY = (destination.y1 + (destination.y2 - destination.y1) / 2) - (start.y1 + (start.y2 - start.y1) / 2);

        int xDelay = direction ? 0 : duration / 2;
        int yDelay = direction ? duration / 2 : 0;
        int ballDelay = duration / BALLS_COUNT / 2;
        for(int i = 0; i < BALLS_COUNT; i++) {
            int ballSize = items[i].ball->coords.x2 - items[i].ball->coords.x1;
            lv_obj_set_pos(items[i].ball, centerStartX + ballSize / 2, centerStartY + ballSize / 2);
            
            lv_anim_init(&items[i].posXAnimation);
            lv_anim_set_exec_cb(&items[i].posXAnimation, (lv_anim_exec_xcb_t) lv_obj_set_x);
            lv_anim_set_var(&items[i].posXAnimation, items[i].ball);
            lv_anim_set_time(&items[i].posXAnimation, duration / 2);
            lv_anim_set_values(&items[i].posXAnimation, centerStartX, centerStartX + distanceX);
            lv_anim_set_delay(&items[i].posXAnimation, delay + i * ballDelay + xDelay);
            lv_anim_start(&items[i].posXAnimation);

            lv_anim_init(&items[i].posYAnimation);
            lv_anim_set_exec_cb(&items[i].posYAnimation, (lv_anim_exec_xcb_t) lv_obj_set_y);
            lv_anim_set_var(&items[i].posYAnimation, items[i].ball);
            lv_anim_set_time(&items[i].posYAnimation, duration / 2);
            lv_anim_set_values(&items[i].posYAnimation, centerStartY, centerStartY + distanceY);
            lv_anim_set_delay(&items[i].posYAnimation, delay + i * ballDelay + yDelay);
            lv_anim_start(&items[i].posYAnimation);
        }
    }
private:
    UIBallAnimationItem_t items[BALLS_COUNT];
};

