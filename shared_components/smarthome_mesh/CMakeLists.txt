idf_component_register(SRCS "smarthome_client.c"
                            "smarthome_node.c"
                       INCLUDE_DIRS "include"
                       REQUIRES bt esp_timer nvs_flash smarthome_protocol smarthome_model smarthome_store smarthome_profiles example_init)
