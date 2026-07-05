idf_component_register(
    SRCS "test_main.cpp"
         "test_water_pump.cpp"
         "test_config_store.cpp"
         "test_data_storage.cpp"
         "test_soil_sensor.cpp"
         "test_bme280.cpp"
         "test_level_sensor.cpp"
         "test_ina226.cpp"
         "test_wifi.cpp"
         "test_time.cpp"
         "test_event_logger.cpp"
         "test_api_serialize.cpp"
         "test_api_requests.cpp"
         "test_api_routes.cpp"
         # Compile-time board-contract TUs (T016): each defines one board
         # selector before including the REAL board/board.h — passing =
         # compiling (static_asserts only, nothing registers with Unity).
         "test_board_contract_rev1.cpp"
         "test_board_contract_rev2.cpp"
    REQUIRES unity actuators interfaces storage nvs_flash sensors board network time events api cjson
)
