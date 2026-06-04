#include "device_controller_ui_integration.h"

#include <stdio.h>
#include <string.h>

#include "device_controller_config.h"
#include "device_controller_state_machine.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "smarthome_profiles.h"
#include "smarthome_ui.h"

static const char *TAG = "DC_UI_INTEGRATION";

static struct {
    lv_ui *ui;
    dc_ui_callbacks_t callbacks;
    dc_ui_screen_t current_screen;
    esp_timer_handle_t boot_timer;
    esp_timer_handle_t message_timer;
    lv_obj_t *message_label;
    lv_obj_t *frame_labels[SH_UI_MAX_COMMANDS];
    lv_obj_t *bar_bg;
    lv_obj_t *bar_fill;
    bool is_initialized;
} s_ui_state;

static char s_pending_message[64];

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

static const char *profile_name_for_index(int32_t index)
{
    const sh_device_profile_t *profile = sh_profiles_get_builtin_by_index((uint8_t)index);
    return profile && profile->display_name ? profile->display_name : "Profile";
}

static const char *current_profile_name(const dc_device_info_t *device)
{
    if (!device || !device->profile) {
        return "None";
    }
    return device->profile->display_name ? device->profile->display_name : "Profile";
}

static bool is_group_target(const dc_device_info_t *device)
{
    return device && device->device_name && strcmp(device->device_name, "All Device") == 0;
}

static void hide_generated_controls(void)
{
    if (!s_ui_state.ui) {
        return;
    }

    lv_obj_t *controls[] = {
        s_ui_state.ui->screen_1_canvas_1,
        s_ui_state.ui->screen_1_TempNum,
        s_ui_state.ui->screen_1_DeviceIndex,
        s_ui_state.ui->screen_1_TempUnit,
        s_ui_state.ui->screen_1_OnOff,
        s_ui_state.ui->screen_1_Mode,
        s_ui_state.ui->screen_1_HeartEmpty,
        s_ui_state.ui->screen_1_HeartReal,
        s_ui_state.ui->screen_1_speed1,
        s_ui_state.ui->screen_1_speed2,
        s_ui_state.ui->screen_1_speed3,
    };

    for (uint8_t i = 0; i < sizeof(controls) / sizeof(controls[0]); i++) {
        if (controls[i]) {
            lv_obj_add_flag(controls[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static lv_obj_t *get_frame_label(uint8_t index)
{
    if (!s_ui_state.ui || !s_ui_state.ui->screen_1 || index >= SH_UI_MAX_COMMANDS) {
        return NULL;
    }

    if (!s_ui_state.frame_labels[index]) {
        s_ui_state.frame_labels[index] = lv_label_create(s_ui_state.ui->screen_1);
        lv_obj_set_style_text_color(s_ui_state.frame_labels[index], lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(s_ui_state.frame_labels[index], LV_OPA_TRANSP, 0);
        lv_obj_set_style_pad_all(s_ui_state.frame_labels[index], 0, 0);
    }
    return s_ui_state.frame_labels[index];
}

static void ensure_bar_objects(void)
{
    if (!s_ui_state.ui || !s_ui_state.ui->screen_1) {
        return;
    }

    if (!s_ui_state.bar_bg) {
        s_ui_state.bar_bg = lv_obj_create(s_ui_state.ui->screen_1);
        lv_obj_set_style_border_color(s_ui_state.bar_bg, lv_color_hex(0x000000), 0);
        lv_obj_set_style_border_width(s_ui_state.bar_bg, 1, 0);
        lv_obj_set_style_bg_color(s_ui_state.bar_bg, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_opa(s_ui_state.bar_bg, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(s_ui_state.bar_bg, 0, 0);
        lv_obj_set_scrollbar_mode(s_ui_state.bar_bg, LV_SCROLLBAR_MODE_OFF);
    }

    if (!s_ui_state.bar_fill) {
        s_ui_state.bar_fill = lv_obj_create(s_ui_state.ui->screen_1);
        lv_obj_set_style_border_width(s_ui_state.bar_fill, 0, 0);
        lv_obj_set_style_bg_color(s_ui_state.bar_fill, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(s_ui_state.bar_fill, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(s_ui_state.bar_fill, 0, 0);
        lv_obj_set_scrollbar_mode(s_ui_state.bar_fill, LV_SCROLLBAR_MODE_OFF);
    }
}

static void hide_frame_objects(void)
{
    for (uint8_t i = 0; i < SH_UI_MAX_COMMANDS; i++) {
        if (s_ui_state.frame_labels[i]) {
            lv_obj_add_flag(s_ui_state.frame_labels[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (s_ui_state.bar_bg) {
        lv_obj_add_flag(s_ui_state.bar_bg, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_ui_state.bar_fill) {
        lv_obj_add_flag(s_ui_state.bar_fill, LV_OBJ_FLAG_HIDDEN);
    }
}

static void render_frame(const sh_ui_frame_t *frame)
{
    if (!frame || !s_ui_state.ui || !s_ui_state.ui->screen_1) {
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
            if (!s_ui_state.bar_bg || !s_ui_state.bar_fill) {
                continue;
            }

            lv_obj_clear_flag(s_ui_state.bar_bg, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_ui_state.bar_fill, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_pos(s_ui_state.bar_bg, cmd->x, cmd->y);
            lv_obj_set_size(s_ui_state.bar_bg, cmd->w, cmd->h);

            int16_t fill_w = ((cmd->w - 2) * cmd->bar.percent) / 100;
            if (fill_w < 1 && cmd->bar.percent > 0) {
                fill_w = 1;
            }
            lv_obj_set_pos(s_ui_state.bar_fill, cmd->x + 1, cmd->y + 1);
            lv_obj_set_size(s_ui_state.bar_fill, fill_w, cmd->h > 2 ? cmd->h - 2 : 1);
        }
    }

    if (s_ui_state.message_label && lv_obj_is_valid(s_ui_state.message_label)) {
        lv_obj_move_foreground(s_ui_state.message_label);
    }
}

static esp_err_t build_and_render(const dc_context_t *context)
{
    if (!context || !s_ui_state.ui || !s_ui_state.ui->screen_1) {
        return ESP_ERR_INVALID_ARG;
    }

    const dc_device_info_t *device = dc_state_machine_get_device_info(context->current_device_idx);
    if (!device) {
        return ESP_ERR_NOT_FOUND;
    }

    sh_ui_context_t ui_context = {
        .view = SH_UI_VIEW_SUMMARY,
        .device_name = device->device_name,
        .is_online = device->is_online,
        .selected_index = context->current_selection,
        .editing_index = (uint8_t)context->selected_parameter,
        .editing_value = context->editing_value,
        .aux_item_enabled = !is_group_target(device),
        .aux_item_name = "Device Type",
        .aux_item_value = current_profile_name(device),
    };

    if (context->current_state == DC_STATE_MENU_NAVIGATE) {
        ui_context.view = SH_UI_VIEW_MENU;
    } else if (context->current_state == DC_STATE_VALUE_ADJUST) {
        ui_context.view = SH_UI_VIEW_EDIT;
        if (context->selected_parameter == DC_PARAM_DEVICE_TYPE) {
            ui_context.editing_index = SH_UI_AUX_INDEX;
            ui_context.aux_item_value = profile_name_for_index(context->editing_value);
        }
    }

    sh_ui_frame_t frame;
    esp_err_t ret = sh_ui_build_frame(device->profile,
                                      device->feature_states,
                                      device->feature_state_count,
                                      &ui_context,
                                      &frame);
    if (ret != ESP_OK) {
        return ret;
    }

    render_frame(&frame);
    return ESP_OK;
}

static void hide_message_label_now(void)
{
    if (s_ui_state.message_label && lv_obj_is_valid(s_ui_state.message_label)) {
        lv_obj_add_flag(s_ui_state.message_label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void message_hide_async_callback(void *user_data)
{
    (void)user_data;
    hide_message_label_now();
}

static void message_timer_callback(void *arg)
{
    (void)arg;
    lv_async_call(message_hide_async_callback, NULL);
}

static void show_message_async_callback(void *user_data)
{
    (void)user_data;

    if (!s_ui_state.is_initialized) {
        return;
    }

    if (!s_ui_state.message_label || !lv_obj_is_valid(s_ui_state.message_label)) {
        s_ui_state.message_label = lv_label_create(lv_layer_top());
        if (!s_ui_state.message_label) {
            ESP_LOGW(TAG, "Failed to create message label");
            return;
        }

        lv_obj_set_style_text_color(s_ui_state.message_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_bg_color(s_ui_state.message_label, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(s_ui_state.message_label, LV_OPA_80, 0);
        lv_obj_set_style_pad_all(s_ui_state.message_label, 6, 0);
    }

    lv_label_set_text(s_ui_state.message_label, s_pending_message);
    lv_obj_clear_flag(s_ui_state.message_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_center(s_ui_state.message_label);
    lv_obj_move_foreground(s_ui_state.message_label);
}

static void boot_timer_callback(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Boot timeout reached, switching to main screen");
    dc_ui_integration_show_main_screen();

    if (s_ui_state.callbacks.on_boot_complete) {
        s_ui_state.callbacks.on_boot_complete();
    }
}

static esp_err_t show_boot_screen(void)
{
    if (!s_ui_state.ui) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_ui_state.ui->screen) {
        setup_scr_screen(s_ui_state.ui);
    }
    if (!s_ui_state.ui->screen) {
        return ESP_ERR_INVALID_STATE;
    }

    if (lv_scr_act() != s_ui_state.ui->screen) {
        lv_scr_load_anim(s_ui_state.ui->screen, LV_SCR_LOAD_ANIM_FADE_IN, 300, 0, false);
    }
    s_ui_state.current_screen = DC_UI_SCREEN_BOOT;

    if (s_ui_state.boot_timer) {
        esp_timer_stop(s_ui_state.boot_timer);
        esp_timer_delete(s_ui_state.boot_timer);
        s_ui_state.boot_timer = NULL;
    }

    const esp_timer_create_args_t boot_timer_args = {
        .callback = boot_timer_callback,
        .name = "boot_timer",
    };
    esp_err_t ret = esp_timer_create(&boot_timer_args, &s_ui_state.boot_timer);
    if (ret != ESP_OK) {
        return ret;
    }
    return esp_timer_start_once(s_ui_state.boot_timer,
                                DEVICE_CONTROLLER_BOOT_DISPLAY_TIME_MS * 1000);
}

esp_err_t dc_ui_integration_init(lv_ui *ui, const dc_ui_callbacks_t *callbacks)
{
    if (!ui) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_ui_state.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(&s_ui_state, 0, sizeof(s_ui_state));
    s_ui_state.ui = ui;
    if (callbacks) {
        s_ui_state.callbacks = *callbacks;
    }
    s_ui_state.current_screen = DC_UI_SCREEN_BOOT;
    s_ui_state.is_initialized = true;
    ESP_LOGI(TAG, "UI integration initialized");
    return ESP_OK;
}

esp_err_t dc_ui_integration_start(void)
{
    if (!s_ui_state.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    return show_boot_screen();
}

esp_err_t dc_ui_integration_update_display(const dc_context_t *context)
{
    if (!s_ui_state.is_initialized || !context) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ui_state.ui->screen_1) {
        return ESP_OK;
    }

    if (context->current_state == DC_STATE_IDLE) {
        s_ui_state.current_screen = DC_UI_SCREEN_MAIN;
    } else {
        s_ui_state.current_screen = DC_UI_SCREEN_MENU;
    }
    return build_and_render(context);
}

esp_err_t dc_ui_integration_show_message(const char *message, uint32_t duration_ms)
{
    if (!s_ui_state.is_initialized || !message) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_ui_state.message_timer) {
        esp_timer_stop(s_ui_state.message_timer);
        esp_timer_delete(s_ui_state.message_timer);
        s_ui_state.message_timer = NULL;
    }

    strncpy(s_pending_message, message, sizeof(s_pending_message) - 1);
    s_pending_message[sizeof(s_pending_message) - 1] = '\0';
    lv_async_call(show_message_async_callback, NULL);
    s_ui_state.current_screen = DC_UI_SCREEN_MESSAGE;

    if (duration_ms > 0) {
        const esp_timer_create_args_t timer_args = {
            .callback = message_timer_callback,
            .name = "message_timer",
        };
        esp_err_t ret = esp_timer_create(&timer_args, &s_ui_state.message_timer);
        if (ret == ESP_OK) {
            ret = esp_timer_start_once(s_ui_state.message_timer, duration_ms * 1000);
        }
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to start message timer: %s", esp_err_to_name(ret));
        }
    }

    return ESP_OK;
}

esp_err_t dc_ui_integration_show_main_screen(void)
{
    if (!s_ui_state.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_ui_state.boot_timer) {
        esp_timer_stop(s_ui_state.boot_timer);
        esp_timer_delete(s_ui_state.boot_timer);
        s_ui_state.boot_timer = NULL;
    }

    if (!s_ui_state.ui->screen_1) {
        setup_scr_screen_1(s_ui_state.ui);
    }
    if (!s_ui_state.ui->screen_1) {
        return ESP_ERR_INVALID_STATE;
    }

    const dc_context_t *context = dc_state_machine_get_context();
    if (context) {
        build_and_render(context);
    } else {
        hide_generated_controls();
    }

    if (lv_scr_act() != s_ui_state.ui->screen_1) {
        lv_scr_load(s_ui_state.ui->screen_1);
    }
    s_ui_state.current_screen = DC_UI_SCREEN_MAIN;

    if (s_ui_state.callbacks.on_screen_changed) {
        s_ui_state.callbacks.on_screen_changed(DC_UI_SCREEN_MAIN);
    }
    return ESP_OK;
}

esp_err_t dc_ui_integration_show_device_switch(uint8_t old_device_idx, uint8_t new_device_idx)
{
    (void)old_device_idx;
    (void)new_device_idx;

    const dc_context_t *context = dc_state_machine_get_context();
    return context ? build_and_render(context) : ESP_OK;
}

esp_err_t dc_ui_integration_show_menu_navigation(dc_parameter_t selected_param, bool is_blinking)
{
    (void)selected_param;
    (void)is_blinking;
    if (!s_ui_state.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    s_ui_state.current_screen = DC_UI_SCREEN_MENU;
    const dc_context_t *context = dc_state_machine_get_context();
    return context ? build_and_render(context) : ESP_OK;
}

esp_err_t dc_ui_integration_show_value_adjustment(dc_parameter_t param, int32_t value, bool is_blinking)
{
    (void)param;
    (void)value;
    (void)is_blinking;
    if (!s_ui_state.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    s_ui_state.current_screen = DC_UI_SCREEN_MENU;
    const dc_context_t *context = dc_state_machine_get_context();
    return context ? build_and_render(context) : ESP_OK;
}

esp_err_t dc_ui_integration_update_device_info(const dc_device_info_t *device_info)
{
    (void)device_info;
    const dc_context_t *context = dc_state_machine_get_context();
    return context ? build_and_render(context) : ESP_OK;
}

esp_err_t dc_ui_integration_deinit(void)
{
    if (!s_ui_state.is_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_ui_state.boot_timer) {
        esp_timer_stop(s_ui_state.boot_timer);
        esp_timer_delete(s_ui_state.boot_timer);
    }
    if (s_ui_state.message_timer) {
        esp_timer_stop(s_ui_state.message_timer);
        esp_timer_delete(s_ui_state.message_timer);
    }

    memset(&s_ui_state, 0, sizeof(s_ui_state));
    ESP_LOGI(TAG, "UI integration deinitialized");
    return ESP_OK;
}

dc_ui_screen_t dc_ui_integration_get_current_screen(void)
{
    return s_ui_state.current_screen;
}
