idf_component_register(SRCS "touch_button_sensor_main.c" "led_matrix.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES touch_button_sensor FastLED)

target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-error")
