# main/CMakeLists.txt
idf_build_get_property(component_targets __COMPONENT_TARGETS)
if ("___idf_espressif__esp-dl" IN_LIST component_targets)
    idf_component_get_property(espdl_dir espressif__esp-dl COMPONENT_DIR)
elseif("___idf_esp-dl" IN_LIST component_targets)
    idf_component_get_property(espdl_dir esp-dl COMPONENT_DIR)
endif()
set(cmake_dir ${espdl_dir}/fbs_loader/cmake)
include(${cmake_dir}/utilities.cmake)

idf_component_register(SRCS "main.cpp" 
                            "app_camera.cpp" 
                            "app_storage.cpp" 
                            "app_face.cpp"
                            "app_graphics.cpp"
                            "app_button.cpp"
                            "app_ota.cpp"
                            "app_web.cpp"
                            "app_lcd.cpp"
                            "app_console.cpp"
                            "app_discovery.cpp"
                            "app_espnow.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES human_face_recognition human_face_detect esp-dl esp32-camera esp_lcd esp_http_server nvs_flash joltwallet__littlefs esp_wifi esp_netif esp_event console esp_https_ota app_update esp_http_client json)

if(CONFIG_MODEL_TYPE_CUSTOM_MOBILENETV2)
    target_add_aligned_binary_data(${COMPONENT_LIB} "models/mobilenetv2_128d_v3.espdl" BINARY)
endif()
