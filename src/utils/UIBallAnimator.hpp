#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include "ui/ui.h"

#define BALLS_RADIUS 16
#define MAX_BALLS 10  // Assuming a reasonable max; adjust based on hardware constraints

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
        this->ballsCount = (ballsCount > MAX_BALLS) ? MAX_BALLS : ballsCount;  // Limit to prevent overflow
        // Use static array to avoid dynamic allocation overhead
        // If ballsCount can be larger, revert to dynamic allocation

        // Precompute constants
        radius_half = BALLS_RADIUS / 2;

        // Create shared style for balls
        lv_style_init(&ball_style);
        lv_style_set_radius(&ball_style, radius_half);
        lv_style_set_bg_opa(&ball_style, 255);

        for (int i = 0; i < this->ballsCount; i++)
        {
            items[i].ball = lv_obj_create(parent);
            
            lv_obj_remove_style_all(items[i].ball);
            lv_obj_set_size(items[i].ball, BALLS_RADIUS, BALLS_RADIUS);
            lv_obj_clear_flag(items[i].ball, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_style(items[i].ball, &ball_style, LV_PART_MAIN | LV_STATE_DEFAULT);
            ui_object_set_themeable_style_property(items[i].ball, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_COLOR, color);
            // Removed as it's now in shared style
            lv_obj_move_background(items[i].ball);

            // Initialize animations once here
            lv_anim_init(&items[i].posXAnimation);
            lv_anim_set_exec_cb(&items[i].posXAnimation, (lv_anim_exec_xcb_t)lv_obj_set_x);
            lv_anim_set_var(&items[i].posXAnimation, items[i].ball);

            lv_anim_init(&items[i].posYAnimation);
            lv_anim_set_exec_cb(&items[i].posYAnimation, (lv_anim_exec_xcb_t)lv_obj_set_y);
            lv_anim_set_var(&items[i].posYAnimation, items[i].ball);
        }

        hLine = lv_obj_create(parent);
        lv_obj_set_style_bg_color(hLine, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
        // lv_obj_set_style_line_dash_gap(hLine, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
        // lv_obj_set_style_line_width(hLine, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
        // lv_obj_set_style_line_rounded(hLine, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_move_background(hLine);

        vLine = lv_obj_create(parent);
        lv_obj_set_style_bg_color(vLine, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_line_dash_gap(vLine, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_line_width(vLine, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_line_rounded(vLine, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
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
        // No delete[] since using static array
    }

    void run(lv_obj_t *start, lv_obj_t *destination, int duration, int delay, bool direction, int xOffset = 0, int yOffset = 0)
    {
        // Precompute all common values
        const int centerStartX = lv_obj_get_x(start) + (lv_obj_get_width(start) / 2);
        const int centerStartY = lv_obj_get_y(start) + (lv_obj_get_height(start) / 2);
        const int centerDestinationX = lv_obj_get_x(destination) + (lv_obj_get_width(destination) / 2);
        const int centerDestinationY = lv_obj_get_y(destination) + (lv_obj_get_height(destination) / 2);
        const int distanceX = centerDestinationX - centerStartX;
        const int distanceY = centerDestinationY - centerStartY;
        const int xDelay = direction ? 0 : duration / 2;
        const int yDelay = direction ? duration / 2 : 0;
        const int ballDelay = duration / 5;  // Precomputed
        const int lineWidth = 3;
        const int lineHalf = lineWidth / 2;
        const int animTime = duration / 2;  // Precompute half duration

        // Update vLine
        lv_obj_set_pos(vLine,
                       (direction == 0 ? centerStartX : centerDestinationX) - lineHalf + xOffset,
                       (distanceY > 0 ? centerStartY : centerDestinationY) + yOffset - lineHalf);
        lv_obj_set_size(vLine, lineWidth, abs(distanceY) + lineWidth);

        // Update hLine
        lv_obj_set_pos(hLine,
                       (distanceX > 0 ? centerStartX : centerDestinationX) - lineHalf + xOffset,
                       yOffset + (direction == 0 ? centerDestinationY : centerStartY) - lineHalf);
        lv_obj_set_size(hLine, abs(distanceX) + lineWidth, lineWidth);

        // Precompute position values
        const int startXAdjusted = centerStartX - radius_half + xOffset;
        const int startYAdjusted = centerStartY - radius_half + yOffset;
        const int endXAdjusted = centerStartX + distanceX - radius_half + xOffset;
        const int endYAdjusted = centerStartY + distanceY - radius_half + yOffset;

        for (int i = 0; i < ballsCount; i++)
        {
            // Set initial position
            lv_obj_set_pos(items[i].ball, startXAdjusted, startYAdjusted);

            // Update and start X animation (reuse initialized anim)
            lv_anim_set_time(&items[i].posXAnimation, animTime);
            lv_anim_set_values(&items[i].posXAnimation, startXAdjusted, endXAdjusted);
            lv_anim_set_delay(&items[i].posXAnimation, delay + i * ballDelay + xDelay);
            lv_anim_start(&items[i].posXAnimation);

            // Update and start Y animation (reuse initialized anim)
            lv_anim_set_time(&items[i].posYAnimation, animTime);
            lv_anim_set_values(&items[i].posYAnimation, startYAdjusted, endYAdjusted);
            lv_anim_set_delay(&items[i].posYAnimation, delay + i * ballDelay + yDelay);
            lv_anim_start(&items[i].posYAnimation);
        }
    }

private:
    UIBallAnimationItem_t items[MAX_BALLS];  // Static array for better performance
    lv_obj_t *parent;
    lv_obj_t *vLine;
    lv_obj_t *hLine;
    int ballsCount;
    int radius_half;  // Precomputed
    lv_style_t ball_style;  // Shared style
};