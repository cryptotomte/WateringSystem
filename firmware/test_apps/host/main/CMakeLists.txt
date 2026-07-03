idf_component_register(
    SRCS "test_main.cpp"
         "test_water_pump.cpp"
         "test_config_store.cpp"
         "test_data_storage.cpp"
         "test_soil_sensor.cpp"
         "test_bme280.cpp"
         "test_level_sensor.cpp"
         "test_ina226.cpp"
    REQUIRES unity actuators interfaces storage nvs_flash sensors
)
