idf_component_register(
    SRCS "app_main.cpp" "diag_console.cpp"
    PRIV_REQUIRES board esp_driver_gpio esp_app_format
                  actuators interfaces console esp_timer
)
