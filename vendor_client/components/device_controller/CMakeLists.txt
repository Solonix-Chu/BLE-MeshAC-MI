idf_component_register(SRCS "device_controller_ui_integration.c" "device_controller.c"
                            "device_controller_state_machine.c"
                            "device_controller_buttons.c"
                            "device_controller_display.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_timer
                                ui
                                button
                                smarthome_mesh
                                smarthome_model
                                smarthome_profiles
                                smarthome_ui
                       PRIV_REQUIRES main)
