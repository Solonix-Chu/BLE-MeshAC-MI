set(VENDOR_CLIENT_MAIN_DIR "${CMAKE_CURRENT_LIST_DIR}/../../vendor_client/main")

set(SRCS
    "${VENDOR_CLIENT_MAIN_DIR}/test_group_control.c"
    "${VENDOR_CLIENT_MAIN_DIR}/main.c"
    "${VENDOR_CLIENT_MAIN_DIR}/board.c"
    "${VENDOR_CLIENT_MAIN_DIR}/ac_control.c"
    "${VENDOR_CLIENT_MAIN_DIR}/display.c")

idf_component_register(SRCS ${SRCS}
                       INCLUDE_DIRS "."
                                    "${VENDOR_CLIENT_MAIN_DIR}"
                       REQUIRES lvgl ui device_controller bt smarthome_mesh smarthome_ble_bridge smarthome_profiles smarthome_model smarthome_protocol)
