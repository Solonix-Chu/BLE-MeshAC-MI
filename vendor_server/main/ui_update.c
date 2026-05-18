/*
 * UI Update Module for Smart-home Server.
 * The server renders the same abstract 128x64 frame produced by smarthome_ui
 * as the client, so both sides stay visually consistent.
 */

#include "ui_update.h"
#include <stdio.h>
#include <string.h>
#include "ac_control.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gui_guider.h"
#include "smarthome_ui.h"

static const char *TAG = "UI_UPDATE";

extern lv_ui guider_ui;

static lv_obj_t *s_frame_labels[SH_UI_MAX_COMMANDS];
static lv_obj_t *s_bar_bg;
static lv_obj_t *s_bar_fill;

static const lv_font_t *map_font(sh_ui_font_t font)
{
    switch (font) {
        case SH_UI_FONT_LARGE:
        case SH_UI_FONT_MEDIUM:
            return &lv_font_Tanker_18;
        case SH_UI_FONT_SMALL:
        default:
            return &lv_font_Tanker_16;
    }
}

static lv_text_align_t map_align(sh_ui_align_t align)
{
    switch (align) {
        case SH_UI_ALIGN_CENTER:
            return LV_TEXT_ALIGN_CENTER;
        case SH_UI_ALIGN_RIGHT:
            return LV_TEXT_ALIGN_RIGHT;
        case SH_UI_ALIGN_LEFT:
        default:
            return LV_TEXT_ALIGN_LEFT;
    }
}

static void hide_generated_controls(void)
{
    lv_obj_t *controls[] = {
        guider_ui.screen_1_canvas_1,
        guider_ui.screen_1_TempNum,
        guider_ui.screen_1_DeviceIndex,
        guider_ui.screen_1_TempUnit,
        guider_ui.screen_1_OnOff,
        guider_ui.screen_1_Mode,
        guider_ui.screen_1_HeartEmpty,
        guider_ui.screen_1_HeartReal,
        guider_ui.screen_1_speed1,
        guider_ui.screen_1_speed2,
        guider_ui.screen_1_speed3,
    };

    for (uint8_t i = 0; i < sizeof(controls) / sizeof(controls[0]); i++) {
        if (controls[i]) {
            lv_obj_add_flag(controls[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static lv_obj_t *get_frame_label(uint8_t index)
{
    if (index >= SH_UI_MAX_COMMANDS || !guider_ui.screen_1) {
        return NULL;
    }
    if (!s_frame_labels[index]) {
        s_frame_labels[index] = lv_label_create(guider_ui.screen_1);
        lv_obj_set_style_text_color(s_frame_labels[index], lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(s_frame_labels[index], 0, 0);
        lv_obj_set_style_pad_all(s_frame_labels[index], 0, 0);
    }
    return s_frame_labels[index];
}

static void ensure_bar_objects(void)
{
    if (!guider_ui.screen_1) {
        return;
    }
    if (!s_bar_bg) {
        s_bar_bg = lv_obj_create(guider_ui.screen_1);
        lv_obj_set_style_border_color(s_bar_bg, lv_color_hex(0x000000), 0);
        lv_obj_set_style_border_width(s_bar_bg, 1, 0);
        lv_obj_set_style_bg_color(s_bar_bg, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(s_bar_bg, 255, 0);
        lv_obj_set_style_radius(s_bar_bg, 0, 0);
        lv_obj_set_scrollbar_mode(s_bar_bg, LV_SCROLLBAR_MODE_OFF);
    }
    if (!s_bar_fill) {
        s_bar_fill = lv_obj_create(guider_ui.screen_1);
        lv_obj_set_style_border_width(s_bar_fill, 0, 0);
        lv_obj_set_style_bg_color(s_bar_fill, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(s_bar_fill, 255, 0);
        lv_obj_set_style_radius(s_bar_fill, 0, 0);
        lv_obj_set_scrollbar_mode(s_bar_fill, LV_SCROLLBAR_MODE_OFF);
    }
}

static void hide_frame_objects(void)
{
    for (uint8_t i = 0; i < SH_UI_MAX_COMMANDS; i++) {
        if (s_frame_labels[i]) {
            lv_obj_add_flag(s_frame_labels[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_bar_bg) {
        lv_obj_add_flag(s_bar_bg, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_bar_fill) {
        lv_obj_add_flag(s_bar_fill, LV_OBJ_FLAG_HIDDEN);
    }
}

static void render_frame(const sh_ui_frame_t *frame)
{
    if (!frame || !guider_ui.screen_1) {
        return;
    }

    hide_generated_controls();
    hide_frame_objects();

    uint8_t label_index = 0;
    for (uint8_t i = 0; i < frame->command_count; i++) {
        const sh_ui_draw_cmd_t *cmd = &frame->commands[i];
        if (cmd->type == SH_UI_CMD_TEXT) {
            lv_obj_t *label = get_frame_label(label_index++);
            if (!label) {
                continue;
            }
            lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(label, cmd->x, cmd->y);
            lv_obj_set_size(label, cmd->w, cmd->h);
            lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
            lv_label_set_text(label, cmd->text.text);
            lv_obj_set_style_text_font(label, map_font(cmd->text.font), 0);
            lv_obj_set_style_text_align(label, map_align(cmd->text.align), 0);
        } else if (cmd->type == SH_UI_CMD_BAR) {
            ensure_bar_objects();
            if (!s_bar_bg || !s_bar_fill) {
                continue;
            }
            lv_obj_clear_flag(s_bar_bg, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_bar_fill, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(s_bar_bg, cmd->x, cmd->y);
            lv_obj_set_size(s_bar_bg, cmd->w, cmd->h);
            int16_t fill_w = ((cmd->w - 2) * cmd->bar.percent) / 100;
            if (fill_w < 1 && cmd->bar.percent > 0) {
                fill_w = 1;
            }
            lv_obj_set_pos(s_bar_fill, cmd->x + 1, cmd->y + 1);
            lv_obj_set_size(s_bar_fill, fill_w, cmd->h > 2 ? cmd->h - 2 : 1);
        }
    }
}

static size_t collect_states(const sh_device_profile_t *profile,
                             sh_feature_state_t *states,
                             size_t max_states)
{
    if (!profile || !states || max_states == 0) {
        return 0;
    }

    size_t count = 0;
    for (uint8_t i = 0; i < profile->feature_count && count < max_states; i++) {
        sh_feature_state_t state = {
            .feature_id = profile->features[i].feature_id,
            .type = profile->features[i].type,
            .value = profile->features[i].default_value,
        };
        ac_server_get_feature(profile->features[i].feature_id, &state);
        states[count++] = state;
    }
    return count;
}

esp_err_t ui_update_node_status(void)
{
    if (!guider_ui.screen_1) {
        ESP_LOGW(TAG, "Screen not available for update");
        return ESP_ERR_INVALID_STATE;
    }

    const sh_device_profile_t *profile = ac_server_get_profile();
    sh_feature_state_t states[SH_MODEL_MAX_FEATURES];
    size_t state_count = collect_states(profile, states, SH_MODEL_MAX_FEATURES);

    sh_ui_context_t context = {
        .view = SH_UI_VIEW_SUMMARY,
        .device_name = profile && profile->display_name ? profile->display_name : "Node",
        .is_online = ac_server_is_connected(),
    };
    sh_ui_frame_t frame;
    esp_err_t err = sh_ui_build_frame(profile, states, state_count, &context, &frame);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to build UI frame: %s", esp_err_to_name(err));
        return err;
    }

    if (!lvgl_port_lock(100)) {
        ESP_LOGE(TAG, "Failed to acquire LVGL mutex");
        return ESP_ERR_TIMEOUT;
    }
    render_frame(&frame);
    lvgl_port_unlock();

    return ESP_OK;
}

esp_err_t ui_update_ac_status(void)
{
    return ui_update_node_status();
}

esp_err_t ui_update_connection_status(bool is_connected)
{
    (void)is_connected;
    return ui_update_node_status();
}

esp_err_t ui_update_device_name(const char *device_name)
{
    (void)device_name;
    return ui_update_node_status();
}

esp_err_t ui_update_init(void)
{
    ESP_LOGI(TAG, "Initializing UI update module");
    esp_err_t err = ui_update_node_status();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Initial UI update deferred: %s", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "UI update module initialized");
    return ESP_OK;
}
