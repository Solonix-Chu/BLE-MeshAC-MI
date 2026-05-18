set(srcs "main.c"
        "board.c"
        "ac_control.c"
        "display.c"
       )

idf_component_register(SRCS "ui_update.c" "${srcs}"
                    INCLUDE_DIRS  "."
                    REQUIRES ui
                             lvgl
                             esp_lvgl_port
                             led_strip
                             smarthome_mesh
                             smarthome_profiles
                             smarthome_ui
                             smarthome_model
                             smarthome_protocol)
