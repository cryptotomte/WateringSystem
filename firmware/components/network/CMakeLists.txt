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
# US1 (feature 007): the pure credential-validation and boot-mode logic is
# header-only (WifiCredentialValidation.h, WifiBootMode.h — no .cpp), so the
# host branch stays INCLUDE_DIRS-only. The provisioning HTTP portal
# (ProvisioningPortal.cpp) is the first hardware touchpoint and is target-
# only. WifiManager.cpp and EspWifiDriver.cpp arrive with US2 — pure sources
# then go to SRCS on BOTH branches, hardware sources on the target branch
# only.
if(${IDF_TARGET} STREQUAL "linux")
    # Host build: pure sources only (validation + boot-mode are header-only).
    idf_component_register(
        INCLUDE_DIRS "include"
        REQUIRES interfaces board
    )
else()
    # Target build: pure sources + the hardware touchpoints. The WiFi/HTTP-
    # server IDF deps are private — they appear only in the EspWifiDriver /
    # ProvisioningPortal .cpp files, never in this component's public headers
    # (same rule as storage's littlefs and sensors' esp-modbus): the portal
    # header holds the server handle as an opaque pointer.
    idf_component_register(
        SRCS "src/ProvisioningPortal.cpp"
        INCLUDE_DIRS "include"
        REQUIRES interfaces board
        PRIV_REQUIRES esp_wifi esp_netif esp_event esp_http_server
    )
endif()
