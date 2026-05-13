set(srcs "main.c"
        "board.c"
        "ac_control.c"
        "display.c"
        "ui_update.c")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS  "."
                    REQUIRES ui
                             lvgl
                             esp_lvgl_port
                             led_strip
                             smarthome_mesh
                             smarthome_profiles
                             smarthome_model
                             smarthome_protocol)
