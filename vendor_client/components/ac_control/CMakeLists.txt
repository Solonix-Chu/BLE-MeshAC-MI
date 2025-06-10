idf_component_register(SRCS "src/ac_control.c" "src/ac_msg_queue.c" "src/ac_storage.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_timer nvs_flash bt log example_nvs) 