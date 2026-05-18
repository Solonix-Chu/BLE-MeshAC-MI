#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "smarthome_model.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SH_UI_SCREEN_WIDTH      128
#define SH_UI_SCREEN_HEIGHT     64
#define SH_UI_MAX_COMMANDS      10
#define SH_UI_TEXT_MAX          32
#define SH_UI_AUX_INDEX         0xFF

typedef enum {
    SH_UI_VIEW_SUMMARY = 0,
    SH_UI_VIEW_MENU,
    SH_UI_VIEW_EDIT,
} sh_ui_view_t;

typedef enum {
    SH_UI_CMD_TEXT = 0,
    SH_UI_CMD_BAR,
} sh_ui_cmd_type_t;

typedef enum {
    SH_UI_FONT_SMALL = 0,
    SH_UI_FONT_MEDIUM,
    SH_UI_FONT_LARGE,
} sh_ui_font_t;

typedef enum {
    SH_UI_ALIGN_LEFT = 0,
    SH_UI_ALIGN_CENTER,
    SH_UI_ALIGN_RIGHT,
} sh_ui_align_t;

typedef struct {
    sh_ui_cmd_type_t type;
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
    union {
        struct {
            sh_ui_font_t font;
            sh_ui_align_t align;
            char text[SH_UI_TEXT_MAX];
        } text;
        struct {
            uint8_t percent;
        } bar;
    };
} sh_ui_draw_cmd_t;

typedef struct {
    sh_ui_draw_cmd_t commands[SH_UI_MAX_COMMANDS];
    uint8_t command_count;
} sh_ui_frame_t;

typedef struct {
    sh_ui_view_t view;
    const char *device_name;
    bool is_online;
    uint8_t selected_index;
    uint8_t editing_index;
    int32_t editing_value;
    bool aux_item_enabled;
    const char *aux_item_name;
    const char *aux_item_value;
} sh_ui_context_t;

esp_err_t sh_ui_build_frame(const sh_device_profile_t *profile,
                            const sh_feature_state_t *states,
                            size_t state_count,
                            const sh_ui_context_t *context,
                            sh_ui_frame_t *frame);

#ifdef __cplusplus
}
#endif
