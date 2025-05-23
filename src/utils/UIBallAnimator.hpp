#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "ui/ui.h"

#define BALLS_RADIUS 16
typedef struct UIBallAnimationItem
{
    lv_obj_t *ball;
    lv_anim_t posXAnimation;
    lv_anim_t posYAnimation;
} UIBallAnimationItem_t;

class UIBallAnimator
{
public:
    UIBallAnimator(lv_obj_t *parent, const ui_theme_variable_t *color, int ballsCount)
    {
        this->parent = parent;
        this->ballsCount = ballsCount;
        this->items = new UIBallAnimationItem_t[ballsCount];
        for (int i = 0; i < ballsCount; i++)
        {
            items[i].ball = lv_obj_create(parent);
            lv_obj_remove_style_all(items[i].ball);

            int radius = BALLS_RADIUS;// - i * (BALLS_RADIUS / ballsCount);
            int opa = 255;// - i * (128 / ballsCount);
            lv_obj_set_width(items[i].ball, radius);
            lv_obj_set_height(items[i].ball, radius);
            //lv_obj_clear_flag(items[i].ball, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE); /// Flags
            lv_obj_set_style_radius(items[i].ball, radius / 2, LV_PART_MAIN | LV_STATE_DEFAULT);
            ui_object_set_themeable_style_property(items[i].ball, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_COLOR, color);
            //ui_object_set_themeable_style_property(items[i].ball, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_OPA, _ui_theme_alpha_pvColor);
            lv_obj_set_style_bg_opa(items[i].ball, opa , LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_move_background(items[i].ball);
        }

        hLine = lv_obj_create(parent);
        lv_obj_set_style_bg_color(hLine, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
        // lv_obj_set_style_line_dash_gap(hLine, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
        // lv_obj_set_style_line_width(hLine, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
        // lv_obj_set_style_line_rounded(hLine, 6, LV_PART_MAIN | LV_STATE_DEFAULT);

        vLine = lv_obj_create(parent);
        lv_obj_set_style_bg_color(vLine, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_line_dash_gap(vLine, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_line_width(vLine, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_line_rounded(vLine, 6, LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_move_background(hLine);
        lv_obj_move_background(vLine);
    }

    ~UIBallAnimator()
    {
        for (int i = 0; i < ballsCount; i++)
        {
            lv_anim_del(&items[i].posXAnimation, (lv_anim_exec_xcb_t)lv_obj_set_x);
            lv_anim_del(&items[i].posYAnimation, (lv_anim_exec_xcb_t)lv_obj_set_y);
            lv_obj_del(items[i].ball);
        }
        lv_obj_del(vLine);
        lv_obj_del(hLine);
        delete[] items;
    }

    void run(lv_obj_t *start, lv_obj_t *destination, int duration, int delay, bool direction, int xOffset = 0, int yOffset = 0)
    {
        int centerStartX = lv_obj_get_x(start) + (lv_obj_get_width(start) / 2);
        int centerStartY = lv_obj_get_y(start) + (lv_obj_get_height(start) / 2);
        int centerDestinationX = lv_obj_get_x(destination) + (lv_obj_get_width(destination) / 2);
        int centerDestinationY = lv_obj_get_y(destination) + (lv_obj_get_height(destination) / 2);
        int distanceX = centerDestinationX - centerStartX;
        int distanceY = centerDestinationY - centerStartY;

        int xDelay = direction ? 0 : duration / 2;
        int yDelay = direction ? duration / 2 : 0;
        int ballDelay = duration / 5; //duration / ballsCount / 2;
        int lineWidth = 3;
        lv_obj_set_pos(vLine, (direction == 0 ? centerStartX : centerDestinationX) - lineWidth / 2 + xOffset, (distanceY > 0 ? centerStartY : centerDestinationY) + yOffset - lineWidth / 2);
        lv_obj_set_size(vLine, 3, abs(distanceY) + 3);

        lv_obj_set_pos(hLine, (distanceX > 0 ? centerStartX : centerDestinationX) - lineWidth / 2+ xOffset, yOffset + (direction == 0 ? centerDestinationY : centerStartY) - lineWidth / 2);
        lv_obj_set_size(hLine, abs(distanceX) + lineWidth, 3);

        for (int i = 0; i < ballsCount; i++)
        {
            int radius = BALLS_RADIUS;// - i * (BALLS_RADIUS / ballsCount);
            lv_obj_set_pos(items[i].ball, centerStartX - radius / 2 + xOffset, centerStartY - radius / 2 + yOffset);
            lv_anim_init(&items[i].posXAnimation);
            lv_anim_set_exec_cb(&items[i].posXAnimation, (lv_anim_exec_xcb_t)lv_obj_set_x);
            lv_anim_set_var(&items[i].posXAnimation, items[i].ball);
            lv_anim_set_time(&items[i].posXAnimation, duration / 2);
            lv_anim_set_values(&items[i].posXAnimation, centerStartX - radius / 2 + xOffset, centerStartX + distanceX - radius / 2 + xOffset);
            lv_anim_set_delay(&items[i].posXAnimation, delay + i * ballDelay + xDelay);
            lv_anim_start(&items[i].posXAnimation);

            lv_anim_init(&items[i].posYAnimation);
            lv_anim_set_exec_cb(&items[i].posYAnimation, (lv_anim_exec_xcb_t)lv_obj_set_y);
            lv_anim_set_var(&items[i].posYAnimation, items[i].ball);
            lv_anim_set_time(&items[i].posYAnimation, duration / 2);
            lv_anim_set_values(&items[i].posYAnimation, centerStartY - radius / 2 + yOffset, centerStartY + distanceY - radius / 2 + yOffset);
            lv_anim_set_delay(&items[i].posYAnimation, delay + i * ballDelay + yDelay);
            lv_anim_start(&items[i].posYAnimation);
        }
    }

private:
    UIBallAnimationItem_t *items;
    lv_obj_t *parent;
    lv_obj_t *vLine;
    lv_obj_t *hLine;
    int ballsCount;
};
