idf_component_register(
    SRCS "app_main.cpp" "diag_console.cpp" "sensor_task.cpp" "wifi_task.cpp"
         "system_observer.cpp" "task_watchdog.cpp" "watering_task.cpp"
    # esp_task_wdt (task watchdog, feature 008 US3) lives in esp_system, already
    # required below — no new provider needed.
    PRIV_REQUIRES board esp_driver_gpio esp_app_format
                  actuators interfaces console esp_timer
                  storage nvs_flash sensors esp_netif esp_event
                  network time events esp_system api control
)

# Build-time littlefs image of the committed seed directory
# (firmware/storage_image), flashed to the `storage` partition — research.md
# D1. The function comes from the managed joltwallet/littlefs component's
# project_include.cmake and registers an ALL target, so build/storage.bin is
# produced on every `idf.py build` (CI verifies the file exists);
# FLASH_IN_PROJECT attaches the image to `idf.py flash` for a deterministic
# fresh-flash filesystem. Canonical placement per the component's example:
# a component CMakeLists, after idf_component_register (the relative path
# resolves against this directory).
littlefs_create_partition_image(storage ../storage_image FLASH_IN_PROJECT)
