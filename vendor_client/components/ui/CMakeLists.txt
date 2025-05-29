set(SRCS
    "gui_guider.c"
    "setup_scr_screen.c"
    "widgets_init.c"
    "events_init.c"
    "guider_fonts/lv_font_montserratMedium_16.c"
    "images/_Logo_alpha_135x135.c"
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