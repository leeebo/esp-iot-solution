idf_component_register(SRCS "touch_button_sensor_main.c" "led_matrix.cpp" "mcp_server_init.c"
                    INCLUDE_DIRS "."
                    REQUIRES touch_button_sensor FastLED mcp-c-sdk nvs_flash esp_netif mdns json)

target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-error")
