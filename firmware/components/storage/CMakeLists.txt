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
        SRCS "src/NvsConfigStore.cpp"
             "src/LittleFsDataStorage.cpp"
        INCLUDE_DIRS "include"
        REQUIRES nvs_flash interfaces
    )
else()
    # Target build only: the littlefs mount/format/stats wrapper. littlefs
    # is the managed joltwallet/littlefs component (pinned in
    # main/idf_component.yml + dependencies.lock; registered under its
    # namespaced component name). PRIV: littlefs headers appear only in
    # src/StorageMount.cpp, never in this component's public headers.
    idf_component_register(
        SRCS "src/NvsConfigStore.cpp"
             "src/LittleFsDataStorage.cpp"
             "src/StorageMount.cpp"
        INCLUDE_DIRS "include"
        REQUIRES nvs_flash interfaces
        PRIV_REQUIRES joltwallet__littlefs
    )
endif()
