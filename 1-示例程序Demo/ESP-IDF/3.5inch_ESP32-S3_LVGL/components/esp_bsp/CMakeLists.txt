file(GLOB_RECURSE SRC_FILES  "*.c" "*.cpp")

idf_component_register(SRCS ${SRC_FILES}
                    INCLUDE_DIRS "."
                    REQUIRES "esp_lcd" "driver" "espressif__esp_lcd_st77922")
