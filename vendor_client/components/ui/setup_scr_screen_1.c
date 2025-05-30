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
#include "custom.h"



void setup_scr_screen_1(lv_ui *ui)
{
    //Write codes screen_1
    ui->screen_1 = lv_obj_create(NULL);
    lv_obj_set_size(ui->screen_1, 128, 128);
    lv_obj_set_scrollbar_mode(ui->screen_1, LV_SCROLLBAR_MODE_OFF);

    //Write style for screen_1, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_bg_opa(ui->screen_1, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->screen_1, lv_color_hex(0xffffff), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_dir(ui->screen_1, LV_GRAD_DIR_NONE, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_1_canvas_1
    ui->screen_1_canvas_1 = lv_canvas_create(ui->screen_1);
    static lv_color_t buf_screen_1_canvas_1[LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(128, 64)];
    lv_canvas_set_buffer(ui->screen_1_canvas_1, buf_screen_1_canvas_1, 128, 64, LV_IMG_CF_TRUE_COLOR_ALPHA);
    lv_canvas_fill_bg(ui->screen_1_canvas_1, lv_color_hex(0x000000), 255);
    //Canvas draw rectangle
    lv_draw_rect_dsc_t screen_1_canvas_1_rect_dsc_0;
    lv_draw_rect_dsc_init(&screen_1_canvas_1_rect_dsc_0);
    screen_1_canvas_1_rect_dsc_0.radius = 0;
    screen_1_canvas_1_rect_dsc_0.bg_opa = 255;
    screen_1_canvas_1_rect_dsc_0.bg_color = lv_color_hex(0x000000);
    screen_1_canvas_1_rect_dsc_0.bg_grad.dir = LV_GRAD_DIR_NONE;
    screen_1_canvas_1_rect_dsc_0.border_width = 0;
    screen_1_canvas_1_rect_dsc_0.border_opa = 255;
    screen_1_canvas_1_rect_dsc_0.border_color = lv_color_hex(0x000000);
    lv_canvas_draw_rect(ui->screen_1_canvas_1, 100, 80, 100, 50, &screen_1_canvas_1_rect_dsc_0);

    lv_obj_set_pos(ui->screen_1_canvas_1, 0, 64);
    lv_obj_set_size(ui->screen_1_canvas_1, 128, 64);
    lv_obj_set_scrollbar_mode(ui->screen_1_canvas_1, LV_SCROLLBAR_MODE_OFF);

    //Write codes screen_1_TempNum
    ui->screen_1_TempNum = lv_label_create(ui->screen_1);
    lv_label_set_text(ui->screen_1_TempNum, "26");
    lv_label_set_long_mode(ui->screen_1_TempNum, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(ui->screen_1_TempNum, 2, -2);
    lv_obj_set_size(ui->screen_1_TempNum, 50, 50);

    //Write style for screen_1_TempNum, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_border_width(ui->screen_1_TempNum, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->screen_1_TempNum, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->screen_1_TempNum, lv_color_hex(0x000000), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->screen_1_TempNum, &lv_font_Tanker_55, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui->screen_1_TempNum, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(ui->screen_1_TempNum, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_line_space(ui->screen_1_TempNum, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui->screen_1_TempNum, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->screen_1_TempNum, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui->screen_1_TempNum, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui->screen_1_TempNum, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui->screen_1_TempNum, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui->screen_1_TempNum, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->screen_1_TempNum, 0, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_1_DeviceIndex
    ui->screen_1_DeviceIndex = lv_label_create(ui->screen_1);
    lv_label_set_text(ui->screen_1_DeviceIndex, "Control-Index");
    lv_label_set_long_mode(ui->screen_1_DeviceIndex, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(ui->screen_1_DeviceIndex, 7, 49);
    lv_obj_set_size(ui->screen_1_DeviceIndex, 115, 16);

    //Write style for screen_1_DeviceIndex, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_border_width(ui->screen_1_DeviceIndex, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->screen_1_DeviceIndex,, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->screen_1_DeviceIndex, lv_color_hex(0x000000), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->screen_1_DeviceIndex, &lv_font_Tanker_16, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui->screen_1_DeviceIndex, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(ui->screen_1_DeviceIndex, 1, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_line_space(ui->screen_1_DeviceIndex, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui->screen_1_DeviceIndex, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->screen_1_DeviceIndex, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui->screen_1_DeviceIndex, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui->screen_1_DeviceIndex, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui->screen_1_DeviceIndex, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui->screen_1_DeviceIndex, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->screen_1_DeviceIndex, 0, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_1_TempUnit
    ui->screen_1_TempUnit = lv_label_create(ui->screen_1);
    lv_label_set_text(ui->screen_1_TempUnit, "°C");
    lv_label_set_long_mode(ui->screen_1_TempUnit, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(ui->screen_1_TempUnit, 52, 32);
    lv_obj_set_size(ui->screen_1_TempUnit, 18, 19);

    //Write style for screen_1_TempUnit, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_border_width(ui->screen_1_TempUnit, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->screen_1_TempUnit, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->screen_1_TempUnit, lv_color_hex(0x000000), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->screen_1_TempUnit, &lv_font_Tanker_16, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui->screen_1_TempUnit, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(ui->screen_1_TempUnit, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_line_space(ui->screen_1_TempUnit, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui->screen_1_TempUnit, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->screen_1_TempUnit, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui->screen_1_TempUnit, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui->screen_1_TempUnit, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui->screen_1_TempUnit, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui->screen_1_TempUnit, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->screen_1_TempUnit, 0, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_1_OnOff
    ui->screen_1_OnOff = lv_label_create(ui->screen_1);
    lv_label_set_text(ui->screen_1_OnOff, "OFF");
    lv_label_set_long_mode(ui->screen_1_OnOff, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(ui->screen_1_OnOff, 92, 4);
    lv_obj_set_size(ui->screen_1_OnOff, 28, 17);

    //Write style for screen_1_OnOff, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_border_width(ui->screen_1_OnOff, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->screen_1_OnOff, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->screen_1_OnOff, lv_color_hex(0x000000), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->screen_1_OnOff, &lv_font_Tanker_18, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui->screen_1_OnOff, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(ui->screen_1_OnOff, 1, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_line_space(ui->screen_1_OnOff, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui->screen_1_OnOff, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->screen_1_OnOff, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui->screen_1_OnOff, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui->screen_1_OnOff, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui->screen_1_OnOff, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui->screen_1_OnOff, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->screen_1_OnOff, 0, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_1_Mode
    ui->screen_1_Mode = lv_label_create(ui->screen_1);
    lv_label_set_text(ui->screen_1_Mode, "MODE");
    lv_label_set_long_mode(ui->screen_1_Mode, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(ui->screen_1_Mode, 66, 27);
    lv_obj_set_size(ui->screen_1_Mode, 58, 17);

    //Write style for screen_1_Mode, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_border_width(ui->screen_1_Mode, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->screen_1_Mode, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(ui->screen_1_Mode, lv_color_hex(0x000000), LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui->screen_1_Mode, &lv_font_Tanker_18, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui->screen_1_Mode, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(ui->screen_1_Mode, 1, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_line_space(ui->screen_1_Mode, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui->screen_1_Mode, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui->screen_1_Mode, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ui->screen_1_Mode, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ui->screen_1_Mode, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(ui->screen_1_Mode, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ui->screen_1_Mode, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui->screen_1_Mode, 0, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_1_HeartEmpty
    ui->screen_1_HeartEmpty = lv_img_create(ui->screen_1);
    lv_obj_add_flag(ui->screen_1_HeartEmpty, LV_OBJ_FLAG_CLICKABLE);
    lv_img_set_src(ui->screen_1_HeartEmpty, &_heart_empty_alpha_18x18);
    lv_img_set_pivot(ui->screen_1_HeartEmpty, 50,50);
    lv_img_set_angle(ui->screen_1_HeartEmpty, 0);
    lv_obj_set_pos(ui->screen_1_HeartEmpty, 57, 3);
    lv_obj_set_size(ui->screen_1_HeartEmpty, 18, 18);

    //Write style for screen_1_HeartEmpty, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_img_recolor_opa(ui->screen_1_HeartEmpty, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_img_opa(ui->screen_1_HeartEmpty, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->screen_1_HeartEmpty, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(ui->screen_1_HeartEmpty, true, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_1_HeartReal
    ui->screen_1_HeartReal = lv_img_create(ui->screen_1);
    lv_obj_add_flag(ui->screen_1_HeartReal, LV_OBJ_FLAG_CLICKABLE);
    lv_img_set_src(ui->screen_1_HeartReal, &_heart_real_alpha_18x18);
    lv_img_set_pivot(ui->screen_1_HeartReal, 50,50);
    lv_img_set_angle(ui->screen_1_HeartReal, 0);
    lv_obj_set_pos(ui->screen_1_HeartReal, 57, 3);
    lv_obj_set_size(ui->screen_1_HeartReal, 18, 18);
    lv_obj_add_flag(ui->screen_1_HeartReal, LV_OBJ_FLAG_HIDDEN);

    //Write style for screen_1_HeartReal, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_img_recolor_opa(ui->screen_1_HeartReal, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_img_opa(ui->screen_1_HeartReal, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->screen_1_HeartReal, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(ui->screen_1_HeartReal, true, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_1_speed3
    ui->screen_1_speed3 = lv_img_create(ui->screen_1);
    lv_obj_add_flag(ui->screen_1_speed3, LV_OBJ_FLAG_CLICKABLE);
    lv_img_set_src(ui->screen_1_speed3, &_speed3_alpha_15x15);
    lv_img_set_pivot(ui->screen_1_speed3, 50,50);
    lv_img_set_angle(ui->screen_1_speed3, 0);
    lv_obj_set_pos(ui->screen_1_speed3, 77, 6);
    lv_obj_set_size(ui->screen_1_speed3, 15, 15);
    lv_obj_add_flag(ui->screen_1_speed3, LV_OBJ_FLAG_HIDDEN);

    //Write style for screen_1_speed3, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_img_recolor_opa(ui->screen_1_speed3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_img_opa(ui->screen_1_speed3, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->screen_1_speed3, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(ui->screen_1_speed3, true, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_1_speed2
    ui->screen_1_speed2 = lv_img_create(ui->screen_1);
    lv_obj_add_flag(ui->screen_1_speed2, LV_OBJ_FLAG_CLICKABLE);
    lv_img_set_src(ui->screen_1_speed2, &_speed2_alpha_15x15);
    lv_img_set_pivot(ui->screen_1_speed2, 50,50);
    lv_img_set_angle(ui->screen_1_speed2, 0);
    lv_obj_set_pos(ui->screen_1_speed2, 77, 6);
    lv_obj_set_size(ui->screen_1_speed2, 15, 15);

    //Write style for screen_1_speed2, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_img_recolor_opa(ui->screen_1_speed2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_img_opa(ui->screen_1_speed2, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->screen_1_speed2, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(ui->screen_1_speed2, true, LV_PART_MAIN|LV_STATE_DEFAULT);

    //Write codes screen_1_speed1
    ui->screen_1_speed1 = lv_img_create(ui->screen_1);
    lv_obj_add_flag(ui->screen_1_speed1, LV_OBJ_FLAG_CLICKABLE);
    lv_img_set_src(ui->screen_1_speed1, &_speed1_alpha_15x15);
    lv_img_set_pivot(ui->screen_1_speed1, 50,50);
    lv_img_set_angle(ui->screen_1_speed1, 0);
    lv_obj_set_pos(ui->screen_1_speed1, 77, 6);
    lv_obj_set_size(ui->screen_1_speed1, 15, 15);
    lv_obj_add_flag(ui->screen_1_speed1, LV_OBJ_FLAG_HIDDEN);

    //Write style for screen_1_speed1, Part: LV_PART_MAIN, State: LV_STATE_DEFAULT.
    lv_obj_set_style_img_recolor_opa(ui->screen_1_speed1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_img_opa(ui->screen_1_speed1, 255, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui->screen_1_speed1, 0, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_clip_corner(ui->screen_1_speed1, true, LV_PART_MAIN|LV_STATE_DEFAULT);

    //The custom code of screen_1.


    //Update current screen layout.
    lv_obj_update_layout(ui->screen_1);

}
