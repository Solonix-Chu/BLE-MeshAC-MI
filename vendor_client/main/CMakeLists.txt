set(SRCS "test_group_control.c" "main.c"
         "board.c"
         "ac_control.c"
         "display.c")

idf_component_register(SRCS ${SRCS}
                       INCLUDE_DIRS "."
                    REQUIRES lvgl ui device_controller bt)
