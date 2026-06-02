idf_component_register(SRCS  "main.cpp" "app_ui.cpp" "app_net.cpp" "app_time_weather.cpp" "app_radio.cpp" "app_audio.cpp" "app_xiaozhi.cpp" "font_radio_cn_18.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_bsp esp_lv_port lvgl esp_wifi esp_netif esp_event nvs_flash esp_http_server esp_http_client esp-tls json lwip esp_app_format app_update spi_flash 78__esp-ml307 78__esp-opus 78__esp-opus-encoder)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -finput-charset=UTF-8
    -fexec-charset=UTF-8
)
