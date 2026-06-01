idf_component_register(SRCS  "main.cpp" "app_ui.cpp" "app_net.cpp" "app_time_weather.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES esp_bsp esp_lv_port lvgl esp_wifi esp_netif esp_event nvs_flash esp_http_server esp_http_client esp-tls json lwip)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -finput-charset=UTF-8
    -fexec-charset=UTF-8
)
