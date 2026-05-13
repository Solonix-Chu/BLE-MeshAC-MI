idf_component_register(SRCS "device_controller.c"
                            "device_controller_state_machine.c"
                            "device_controller_buttons.c"
                            "device_controller_display.c"
                            "device_controller_ui_integration.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_timer
                                ui
                                button
                                smarthome_mesh
                                smarthome_model
                                smarthome_profiles
                       PRIV_REQUIRES main)
