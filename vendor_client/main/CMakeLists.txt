set(SRCS "main.c"
         "board.c"
         "display.c")

idf_component_register(SRCS ${SRCS}
                       INCLUDE_DIRS "."
                    REQUIRES lvgl ui device_controller bt ac_control)
