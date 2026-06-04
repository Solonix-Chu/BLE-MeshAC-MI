idf_component_register(SRCS "smarthome_ble_bridge.c"
                       INCLUDE_DIRS "include"
                       REQUIRES bt smarthome_mesh smarthome_model smarthome_protocol)
