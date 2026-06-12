# storage — persisted configuration (NVS) and data storage (littlefs).
#
# All configuration/data logic builds for every target including the linux
# preview target: host tests run against the real nvs_flash implementation
# (research.md D3) and plain POSIX file I/O (research.md D4).
#
# esp_littlefs has NO linux port, so the littlefs dependency and the
# target-only mount wrapper (src/StorageMount.cpp, user story 4) are
# confined to the non-linux branch below (research.md D4 mechanism).
if(${IDF_TARGET} STREQUAL "linux")
    idf_component_register(
        REQUIRES nvs_flash interfaces
    )
else()
    # User story 4 adds "src/StorageMount.cpp" to SRCS and littlefs to
    # REQUIRES in this branch only — never for linux.
    idf_component_register(
        REQUIRES nvs_flash interfaces
    )
endif()
