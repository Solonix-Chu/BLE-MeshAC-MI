idf_component_register(SRCS "smarthome_client.c"
                            "smarthome_node.c"
                            "smarthome_remote.c"
                            "smarthome_controller.c"
                       INCLUDE_DIRS "include"
                       REQUIRES bt esp_hw_support esp_timer nvs_flash smarthome_protocol smarthome_model smarthome_store smarthome_profiles example_init)
