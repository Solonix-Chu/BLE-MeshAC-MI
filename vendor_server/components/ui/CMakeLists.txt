set(SRCS
    "gui_guider.c"
    "setup_scr_screen.c"
    "setup_scr_screen_1.c"
    "widgets_init.c"
    "events_init.c"

    # Fonts from guider_fonts
    "guider_fonts/lv_font_montserratMedium_16.c"
    "guider_fonts/lv_font_Tanker_55.c"
    "guider_fonts/lv_font_Tanker_18.c"
    "guider_fonts/lv_font_Tanker_16.c"

    # Images from images directory
    "images/_Logo_alpha_135x135.c"
    "images/_speed1_alpha_15x15.c"
    "images/_speed2_alpha_15x15.c"
    "images/_speed3_alpha_15x15.c"
    "images/_heart_real_alpha_18x18.c"
    "images/_heart_empty_alpha_18x18.c"

    # Add any .c files from guider_customer_fonts if they exist
    # For example: "guider_customer_fonts/my_custom_font.c"
)

# Define include directories for the component itself and any subdirectories containing .h files
set(COMPONENT_INCLUDE_DIRS ".")
list(APPEND COMPONENT_INCLUDE_DIRS
    "guider_fonts"
    "guider_customer_fonts"
    "images"
)

idf_component_register(SRCS ${SRCS}
                       INCLUDE_DIRS ${COMPONENT_INCLUDE_DIRS}
                       REQUIRES lvgl
                      )

target_compile_definitions(${COMPONENT_LIB} PUBLIC LV_LVGL_H_INCLUDE_SIMPLE)
