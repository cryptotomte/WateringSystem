# network — WiFi station management + first-boot SoftAP provisioning.
#
# Same split as sensors/actuators/storage: all timing and state-machine
# logic lives in pure C++ (WifiManager, decideBootMode, credential
# validation) driven by ITimeProvider + the IWifiDriver seam, so it builds
# and is unit-tested on the linux preview target. The IDF-touching classes
# (EspWifiDriver over esp_wifi/esp_netif/esp_event, ProvisioningPortal over
# esp_http_server) carry no business logic and are excluded from the linux
# build (research.md, same mechanism as the other components).
#
# Phase 1/2 scaffolding (tasks T001–T007): no pure sources exist yet —
# WifiManager.cpp and the hardware sources arrive with US1/US2. The
# component therefore registers header-only for now (INCLUDE_DIRS only) and
# still compiles cleanly on both targets. When pure sources land, add them
# to SRCS on BOTH branches; hardware sources go to SRCS on the target
# branch only.
if(${IDF_TARGET} STREQUAL "linux")
    # Host build: pure sources only (none yet — header-only for now).
    idf_component_register(
        INCLUDE_DIRS "include"
        REQUIRES interfaces board
    )
else()
    # Target build: pure sources + the hardware touchpoints (none yet). The
    # WiFi/HTTP-server IDF deps are private — they appear only in the
    # EspWifiDriver / ProvisioningPortal .cpp files, never in this
    # component's public headers (same rule as storage's littlefs and
    # sensors' esp-modbus).
    idf_component_register(
        INCLUDE_DIRS "include"
        REQUIRES interfaces board
        PRIV_REQUIRES esp_wifi esp_netif esp_event esp_http_server
    )
endif()
