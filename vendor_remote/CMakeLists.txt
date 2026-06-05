# The following lines of boilerplate have to be in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "../shared_components/smarthome_protocol"
    "../shared_components/smarthome_model"
    "../shared_components/smarthome_store"
    "../shared_components/smarthome_profiles"
    "../shared_components/smarthome_ui"
    "../shared_components/smarthome_mesh"
    "../shared_components/smarthome_ble_bridge"
    "../vendor_client/components/ui"
    "../vendor_client/components/device_controller"
    "../vendor_client/components/lvgl")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(vendor_remote)
