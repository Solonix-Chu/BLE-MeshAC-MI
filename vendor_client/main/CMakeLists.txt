set(SRCS "main.c"
         "board.c"
         "ac_control.c"
         "display.c")

idf_component_register(SRCS ${SRCS}
                    INCLUDE_DIRS "."
                    REQUIRES lvgl ui)
