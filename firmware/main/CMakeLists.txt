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

# Build-time littlefs image of the `storage` partition (research.md D1),
# flashed via FLASH_IN_PROJECT. Feature 010 (PR-10) adds the frontend: the
# image is now a build-time STAGING dir = the committed seed (firmware/
# storage_image) PLUS the gzipped web assets (firmware/web -> *.gz), produced
# by tools/gzip_assets.py. This keeps `data/` frozen (the adapted copies live
# in firmware/web) and generates the .gz at build time (no committed gzip),
# with no node/Tailwind toolchain in CI (only Python gzip). The staged dir is
# regenerated on every build so it always reflects the current web sources.
set(WEB_IMAGE_DIR "${CMAKE_CURRENT_BINARY_DIR}/web_image")
set(WEB_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../web")
set(SEED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../storage_image")
set(GZIP_TOOL "${CMAKE_CURRENT_SOURCE_DIR}/../tools/gzip_assets.py")

# The Python interpreter IDF resolved for this build (same one littlefs uses).
idf_build_get_property(python PYTHON)

# Collect the web sources so the stage target re-runs when any of them change.
# CONFIGURE_DEPENDS re-globs on every build so an ADDED/REMOVED asset is picked
# up (not just edits to existing files).
file(GLOB_RECURSE WEB_SRC_FILES CONFIGURE_DEPENDS "${WEB_SRC_DIR}/*")

add_custom_command(
    OUTPUT "${WEB_IMAGE_DIR}/.stamp"
    COMMENT "Staging littlefs image (seed + gzipped web assets)"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${WEB_IMAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${WEB_IMAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${SEED_DIR}" "${WEB_IMAGE_DIR}"
    COMMAND ${python} "${GZIP_TOOL}" --src "${WEB_SRC_DIR}" --dst "${WEB_IMAGE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E touch "${WEB_IMAGE_DIR}/.stamp"
    DEPENDS ${WEB_SRC_FILES} "${GZIP_TOOL}"
    VERBATIM
)
add_custom_target(web_image_stage ALL DEPENDS "${WEB_IMAGE_DIR}/.stamp")

# Pack the staged dir. littlefs_create_partition_image registers its own ALL
# target; make it wait for the staging to finish.
littlefs_create_partition_image(storage "${WEB_IMAGE_DIR}" FLASH_IN_PROJECT
                                DEPENDS web_image_stage)
