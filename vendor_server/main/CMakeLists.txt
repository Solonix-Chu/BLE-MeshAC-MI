set(srcs "main.c"
        "board.c"
        "ac_control.c"
        "display.c"
        "ui_update.c")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS  ".")
