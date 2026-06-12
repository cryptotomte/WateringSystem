idf_component_register(
    SRCS "test_main.cpp"
         "test_water_pump.cpp"
         "test_config_store.cpp"
         "test_data_storage.cpp"
    REQUIRES unity actuators interfaces nvs_flash
)
