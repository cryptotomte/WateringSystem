# api — versioned /api/v1/ REST/JSON API (feature 009 / PR-09).
#
# Same pure/target split as the sensor and network components: the JSON
# response envelope, the serializers, the request parsers/validators and the
# route table are PURE C++ over cJSON (the espressif/cjson managed component, which builds on
# the linux preview target), so they are host-tested with NO esp_http_server
# dependency. The esp_http_server plumbing lives in ApiServer.cpp — the only
# IDF/hardware touchpoint — and is excluded from the linux build (research.md,
# same mechanism as ProvisioningPortal / EspModbusClient / StorageMount).
#
# The component configures on both board targets AND on linux:
#   pure (both branches): ApiEnvelope.cpp, ApiRoutes.cpp, ApiSerialize.cpp,
#     ApiRequests.cpp.
#   target-only:          ApiServer.cpp.
if(${IDF_TARGET} STREQUAL "linux")
    # Host build: pure sources only (envelope, route table, serializers and
    # request parsers) — no esp_http_server touchpoint.
    idf_component_register(
        SRCS "src/ApiEnvelope.cpp"
             "src/ApiRoutes.cpp"
             "src/ApiSerialize.cpp"
             "src/ApiRequests.cpp"
             "src/ApiStatic.cpp"
        INCLUDE_DIRS "include"
        REQUIRES interfaces cjson network time events
    )
else()
    # Target build: pure sources + the esp_http_server touchpoint. The HTTP /
    # netif / app-format IDF deps are PRIVATE — they appear only inside
    # ApiServer.cpp, never in this component's public headers (the server
    # header holds its handle as an opaque void*, per the ProvisioningPortal
    # precedent).
    idf_component_register(
        SRCS "src/ApiEnvelope.cpp"
             "src/ApiRoutes.cpp"
             "src/ApiSerialize.cpp"
             "src/ApiServer.cpp"
             "src/ApiRequests.cpp"
             "src/ApiStatic.cpp"
        INCLUDE_DIRS "include"
        REQUIRES interfaces cjson network time events
        PRIV_REQUIRES esp_http_server esp_netif esp_app_format
    )
endif()
