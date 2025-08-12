/*
* Copyright 2025 NXP
* NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
* accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
* activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
* comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
* terms, then you may not retain, install, activate or otherwise use the software.
*/

#include "lvgl.h"
#include <stdio.h>
#include "gui_guider.h"
#include "events_init.h"
#include "widgets_init.h"
// #include "custom.h"



void setup_scr_screen(lv_ui *ui)
{
    //Write codes screen
    ui->screen = lv_obj_create(NULL);
    lv_obj_set_size(ui->screen, 128, 128);
    lv_obj_set_scrollbar_mode(ui->screen, LV_SCROLLBAR_MODE_OFF);

    //Write style for screen, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(ui->screen, 0, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_canvas_1
    ui->screen_canvas_1 = lv_canvas_create(ui->screen);
    static lv_color_t buf_screen_canvas_1[LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(128, 64)];
    lv_canvas_set_buffer(ui->screen_canvas_1, buf_screen_canvas_1, 128, 64, LV_IMG_CF_TRUE_COLOR_ALPHA);
    lv_canvas_fill_bg(ui->screen_canvas_1, lv_color_hex(0x000000), 255);
    //Canvas draw rectangle
    lv_draw_rect_dsc_t screen_canvas_1_rect_dsc_0;
    lv_draw_rect_dsc_init(&screen_canvas_1_rect_dsc_0);
    screen_canvas_1_rect_dsc_0.radius = 0;
    screen_canvas_1_rect_dsc_0.bg_opa = 255;
    screen_canvas_1_rect_dsc_0.bg_color = lv_color_hex(0x000000);
    screen_canvas_1_rect_dsc_0.bg_grad.dir = LV_GRAD_DIR_NONE;
    screen_canvas_1_rect_dsc_0.border_width = 0;
    screen_canvas_1_rect_dsc_0.border_opa = 255;
    screen_canvas_1_rect_dsc_0.border_color = lv_color_hex(0x000000);
    lv_canvas_draw_rect(ui->screen_canvas_1, 100, 80, 100, 50, &screen_canvas_1_rect_dsc_0);

    lv_obj_set_pos(ui->screen_canvas_1, 0, 64);
    lv_obj_set_size(ui->screen_canvas_1, 128, 64);
    lv_obj_set_scrollbar_mode(ui->screen_canvas_1, LV_SCROLLBAR_MODE_OFF);

    //Write codes screen_logo
    ui->screen_logo = lv_img_create(ui->screen);
    lv_obj_add_flag(ui->screen_logo, LV_OBJ_FLAG_CLICKABLE);
    lv_img_set_src(ui->screen_logo, &_Logo_alpha_135x135);
    lv_img_set_pivot(ui->screen_logo, 50,50);
    lv_img_set_angle(ui->screen_logo, 0);
    lv_obj_set_pos(ui->screen_logo, -4, -30);
    lv_obj_set_size(ui->screen_logo, 135, 135);

    //Write style for screen_logo, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_img_recolor_opa(ui->screen_logo, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_img_opa(ui->screen_logo, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->screen_logo, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(ui->screen_logo, true, LV_PART_MAIN|LV_STATE_DEFAULT);

    //The custom code of screen.


    //Update current screen layout.
    lv_obj_update_layout(ui->screen);

}
