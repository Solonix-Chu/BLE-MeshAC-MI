#include "smarthome_ui.h"
#include <stdio.h>
#include <string.h>

static void frame_reset(sh_ui_frame_t *frame)
{
    memset(frame, 0, sizeof(*frame));
}

static void add_text(sh_ui_frame_t *frame,
                     int16_t x,
                     int16_t y,
                     int16_t w,
                     int16_t h,
                     sh_ui_font_t font,
                     sh_ui_align_t align,
                     const char *text)
{
    if (!frame || frame->command_count >= SH_UI_MAX_COMMANDS) {
        return;
    }
    sh_ui_draw_cmd_t *cmd = &frame->commands[frame->command_count++];
    cmd->type = SH_UI_CMD_TEXT;
    cmd->x = x;
    cmd->y = y;
    cmd->w = w;
    cmd->h = h;
    cmd->text.font = font;
    cmd->text.align = align;
    snprintf(cmd->text.text, sizeof(cmd->text.text), "%s", text ? text : "");
}

static void add_bar(sh_ui_frame_t *frame,
                    int16_t x,
                    int16_t y,
                    int16_t w,
                    int16_t h,
                    uint8_t percent)
{
    if (!frame || frame->command_count >= SH_UI_MAX_COMMANDS) {
        return;
    }
    sh_ui_draw_cmd_t *cmd = &frame->commands[frame->command_count++];
    cmd->type = SH_UI_CMD_BAR;
    cmd->x = x;
    cmd->y = y;
    cmd->w = w;
    cmd->h = h;
    cmd->bar.percent = percent > 100 ? 100 : percent;
}

static const sh_feature_def_t *feature_by_index(const sh_device_profile_t *profile, uint8_t index)
{
    if (!profile || !profile->features || index >= profile->feature_count) {
        return NULL;
    }
    return &profile->features[index];
}

static const sh_feature_state_t *find_state(const sh_feature_state_t *states,
                                            size_t state_count,
                                            uint16_t feature_id)
{
    return sh_model_find_const_state(states, state_count, feature_id);
}

static int32_t feature_value(const sh_feature_def_t *feature,
                             const sh_feature_state_t *states,
                             size_t state_count)
{
    if (!feature) {
        return 0;
    }
    const sh_feature_state_t *state = find_state(states, state_count, feature->feature_id);
    return state ? state->value : feature->default_value;
}

static void format_value(const sh_feature_def_t *feature,
                         int32_t value,
                         char *buf,
                         size_t buf_len,
                         bool with_unit)
{
    if (!feature || !buf || buf_len == 0) {
        return;
    }

    switch (feature->type) {
    case SH_FEATURE_TYPE_BOOL:
        snprintf(buf, buf_len, "%s", value ? "ON" : "OFF");
        return;
    case SH_FEATURE_TYPE_ENUM:
        if (value >= 0 && value < feature->constraints.enum_count &&
            feature->constraints.enum_labels) {
            snprintf(buf, buf_len, "%s", feature->constraints.enum_labels[value]);
            return;
        }
        break;
    case SH_FEATURE_TYPE_INT:
    default:
        break;
    }

    const char *unit = with_unit ? sh_model_feature_unit(feature) : "";
    snprintf(buf, buf_len, "%ld%s", value, unit ? unit : "");
}

static uint8_t value_percent(const sh_feature_def_t *feature, int32_t value)
{
    if (!feature || feature->type != SH_FEATURE_TYPE_INT ||
        feature->constraints.max <= feature->constraints.min) {
        return 0;
    }
    if (value < feature->constraints.min) {
        value = feature->constraints.min;
    }
    if (value > feature->constraints.max) {
        value = feature->constraints.max;
    }
    int32_t span = feature->constraints.max - feature->constraints.min;
    return (uint8_t)(((value - feature->constraints.min) * 100) / span);
}

static const sh_feature_def_t *find_power_feature(const sh_device_profile_t *profile)
{
    return sh_model_find_feature(profile, SH_FEATURE_ID_POWER);
}

static const sh_feature_def_t *find_primary_feature(const sh_device_profile_t *profile)
{
    static const sh_feature_role_t preferred[] = {
        SH_FEATURE_ROLE_TEMPERATURE,
        SH_FEATURE_ROLE_BRIGHTNESS,
        SH_FEATURE_ROLE_POSITION,
        SH_FEATURE_ROLE_VOLUME,
        SH_FEATURE_ROLE_CHANNEL,
        SH_FEATURE_ROLE_ANALOG,
        SH_FEATURE_ROLE_MODE,
        SH_FEATURE_ROLE_INPUT_SOURCE,
    };

    if (!profile || !profile->features) {
        return NULL;
    }

    for (uint8_t r = 0; r < sizeof(preferred) / sizeof(preferred[0]); r++) {
        for (uint8_t i = 0; i < profile->feature_count; i++) {
            const sh_feature_def_t *feature = &profile->features[i];
            if (feature->feature_id != SH_FEATURE_ID_POWER &&
                sh_model_infer_feature_role(feature) == preferred[r]) {
                return feature;
            }
        }
    }

    for (uint8_t i = 0; i < profile->feature_count; i++) {
        if (profile->features[i].feature_id != SH_FEATURE_ID_POWER) {
            return &profile->features[i];
        }
    }
    return profile->feature_count > 0 ? &profile->features[0] : NULL;
}

static void add_header(const sh_device_profile_t *profile,
                       const sh_feature_state_t *states,
                       size_t state_count,
                       const sh_ui_context_t *context,
                       sh_ui_frame_t *frame)
{
    const char *name = context && context->device_name ? context->device_name :
        (profile && profile->display_name ? profile->display_name : "Device");
    add_text(frame, 0, 0, 78, 14, SH_UI_FONT_SMALL, SH_UI_ALIGN_LEFT, name);

    const sh_feature_def_t *power = find_power_feature(profile);
    char power_text[SH_UI_TEXT_MAX] = "--";
    if (power) {
        format_value(power, feature_value(power, states, state_count),
                     power_text, sizeof(power_text), false);
    }
    add_text(frame, 98, 0, 30, 14, SH_UI_FONT_SMALL, SH_UI_ALIGN_RIGHT, power_text);
    add_text(frame, 80, 0, 16, 14, SH_UI_FONT_SMALL, SH_UI_ALIGN_CENTER,
             context && context->is_online ? "*" : "-");
}

static void build_summary(const sh_device_profile_t *profile,
                          const sh_feature_state_t *states,
                          size_t state_count,
                          const sh_ui_context_t *context,
                          sh_ui_frame_t *frame)
{
    add_header(profile, states, state_count, context, frame);

    const sh_feature_def_t *primary = find_primary_feature(profile);
    if (!primary) {
        add_text(frame, 0, 22, 128, 24, SH_UI_FONT_MEDIUM, SH_UI_ALIGN_CENTER, "No Profile");
        return;
    }

    int32_t primary_value = feature_value(primary, states, state_count);
    char value_text[SH_UI_TEXT_MAX];
    format_value(primary, primary_value, value_text, sizeof(value_text), true);
    add_text(frame, 0, 16, 62, 24, SH_UI_FONT_LARGE, SH_UI_ALIGN_CENTER, value_text);
    add_text(frame, 66, 18, 62, 16, SH_UI_FONT_SMALL, SH_UI_ALIGN_LEFT,
             primary->name ? primary->name : sh_model_feature_role_name(sh_model_infer_feature_role(primary)));

    if (primary->type == SH_FEATURE_TYPE_INT) {
        add_bar(frame, 4, 39, 120, 4, value_percent(primary, primary_value));
    }

    char summary[SH_UI_TEXT_MAX] = {0};
    bool first = true;
    for (uint8_t i = 0; profile && i < profile->feature_count; i++) {
        const sh_feature_def_t *feature = &profile->features[i];
        if (feature == primary || feature->feature_id == SH_FEATURE_ID_POWER) {
            continue;
        }
        char value[12];
        format_value(feature, feature_value(feature, states, state_count), value, sizeof(value), true);
        size_t used = strlen(summary);
        if (used >= sizeof(summary) - 1) {
            break;
        }
        snprintf(summary + used, sizeof(summary) - used, "%s%s %s",
                 first ? "" : " ",
                 feature->name ? feature->name : sh_model_feature_role_name(sh_model_infer_feature_role(feature)),
                 value);
        first = false;
    }
    if (summary[0] == '\0') {
        snprintf(summary, sizeof(summary), "%s",
                 profile && profile->device_type ? profile->device_type : "Ready");
    }
    add_text(frame, 0, 47, 128, 16, SH_UI_FONT_SMALL, SH_UI_ALIGN_CENTER, summary);
}

static void build_menu(const sh_device_profile_t *profile,
                       const sh_feature_state_t *states,
                       size_t state_count,
                       const sh_ui_context_t *context,
                       sh_ui_frame_t *frame)
{
    add_header(profile, states, state_count, context, frame);
    uint8_t feature_count = profile ? profile->feature_count : 0;
    uint8_t count = feature_count + ((context && context->aux_item_enabled) ? 1 : 0);
    uint8_t selected = context ? context->selected_index : 0;
    uint8_t first = 0;
    if (selected > 1) {
        first = selected - 1;
    }
    if (first + 3 > count && count > 3) {
        first = count - 3;
    }

    for (uint8_t row = 0; row < 3 && first + row < count; row++) {
        uint8_t idx = first + row;
        const sh_feature_def_t *feature = idx < feature_count ? feature_by_index(profile, idx) : NULL;
        const char *name = feature && feature->name ? feature->name :
            (feature ? sh_model_feature_role_name(sh_model_infer_feature_role(feature)) :
             (context && context->aux_item_name ? context->aux_item_name : "Option"));
        char value[12];
        if (feature) {
            format_value(feature, feature_value(feature, states, state_count), value, sizeof(value), true);
        } else {
            snprintf(value, sizeof(value), "%s",
                     context && context->aux_item_value ? context->aux_item_value : "");
        }
        char line[SH_UI_TEXT_MAX];
        snprintf(line, sizeof(line), "%c%s %s",
                 idx == selected ? '>' : ' ',
                 name,
                 value);
        add_text(frame, 0, 16 + row * 15, 128, 14,
                 idx == selected ? SH_UI_FONT_MEDIUM : SH_UI_FONT_SMALL,
                 SH_UI_ALIGN_LEFT, line);
    }
}

static void build_edit(const sh_device_profile_t *profile,
                       const sh_ui_context_t *context,
                       sh_ui_frame_t *frame)
{
    add_header(profile, NULL, 0, context, frame);
    if (context && context->editing_index == SH_UI_AUX_INDEX) {
        add_text(frame, 0, 16, 128, 13, SH_UI_FONT_SMALL, SH_UI_ALIGN_CENTER,
                 context->aux_item_name ? context->aux_item_name : "Option");
        add_text(frame, 0, 29, 128, 21, SH_UI_FONT_LARGE, SH_UI_ALIGN_CENTER,
                 context->aux_item_value ? context->aux_item_value : "--");
        add_text(frame, 0, 51, 128, 12, SH_UI_FONT_SMALL, SH_UI_ALIGN_CENTER, "CENTER OK");
        return;
    }

    const sh_feature_def_t *feature = feature_by_index(profile, context ? context->editing_index : 0);
    if (!feature) {
        add_text(frame, 0, 22, 128, 20, SH_UI_FONT_MEDIUM, SH_UI_ALIGN_CENTER, "No Feature");
        return;
    }

    char value[SH_UI_TEXT_MAX];
    format_value(feature, context ? context->editing_value : feature->default_value,
                 value, sizeof(value), true);
    add_text(frame, 0, 16, 128, 13, SH_UI_FONT_SMALL, SH_UI_ALIGN_CENTER,
             feature->name ? feature->name : "Feature");
    add_text(frame, 0, 29, 128, 21, SH_UI_FONT_LARGE, SH_UI_ALIGN_CENTER, value);
    if (feature->type == SH_FEATURE_TYPE_INT) {
        add_bar(frame, 4, 53, 120, 4,
                value_percent(feature, context ? context->editing_value : feature->default_value));
    } else {
        add_text(frame, 0, 51, 128, 12, SH_UI_FONT_SMALL, SH_UI_ALIGN_CENTER, "CENTER OK");
    }
}

esp_err_t sh_ui_build_frame(const sh_device_profile_t *profile,
                            const sh_feature_state_t *states,
                            size_t state_count,
                            const sh_ui_context_t *context,
                            sh_ui_frame_t *frame)
{
    if (!frame) {
        return ESP_ERR_INVALID_ARG;
    }

    frame_reset(frame);
    if (!profile || !profile->features || profile->feature_count == 0) {
        add_text(frame, 0, 0, 128, 16, SH_UI_FONT_SMALL, SH_UI_ALIGN_CENTER, "Unconfigured");
        add_text(frame, 0, 24, 128, 18, SH_UI_FONT_MEDIUM, SH_UI_ALIGN_CENTER, "No Profile");
        return ESP_OK;
    }

    switch (context ? context->view : SH_UI_VIEW_SUMMARY) {
    case SH_UI_VIEW_MENU:
        build_menu(profile, states, state_count, context, frame);
        break;
    case SH_UI_VIEW_EDIT:
        build_edit(profile, context, frame);
        break;
    case SH_UI_VIEW_SUMMARY:
    default:
        build_summary(profile, states, state_count, context, frame);
        break;
    }
    return ESP_OK;
}
