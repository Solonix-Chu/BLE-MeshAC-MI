set(srcs "main.c"
        "board.c")

idf_component_register(SRCS "ac_control.c" "${srcs}"
                    INCLUDE_DIRS  ".")
