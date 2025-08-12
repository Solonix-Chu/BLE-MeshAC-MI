set(SRCS "test_group_control.c" "main.c"
         "board.c"
         "ac_control.c"
          "web_server.c")

idf_component_register(SRCS ${SRCS}
                       INCLUDE_DIRS "."
                       REQUIRES 
                            bt 
                            esp_wifi 
                            esp_http_server 
                            esp_netif 
                            spiffs 
                            # mdns
                            json)

# Conditionally add UI sources and dependencies when not WEB_ONLY
if(NOT CONFIG_WEB_ONLY)
    target_sources(${COMPONENT_LIB} PRIVATE "display.c")
    idf_component_get_property(LVGL_TARGET lvgl COMPONENT_LIB)
    idf_component_get_property(UI_TARGET ui COMPONENT_LIB)
    idf_component_get_property(DC_TARGET device_controller COMPONENT_LIB)
    target_link_libraries(${COMPONENT_LIB} PRIVATE ${LVGL_TARGET} ${UI_TARGET} ${DC_TARGET})
endif()

# 打包前端到 SPIFFS 分区 "www"
set(WEB_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../front/web-demo")
if(EXISTS ${WEB_SRC_DIR}/dist)
    spiffs_create_partition_image(www ${WEB_SRC_DIR}/dist FLASH_IN_PROJECT)
else()
    message(FATAL_ERROR "${WEB_SRC_DIR}/dist doesn't exist. Please run 'npm ci && npm run build' in ${WEB_SRC_DIR}")
endif()
