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
# Sources are registered as they land across the phased tasks; only files that
# already exist are listed, so the component configures on both board targets
# AND on linux from this phase onward:
#   pure (both branches): ApiEnvelope.cpp (T003), ApiRoutes.cpp (T004),
#     ApiSerialize.cpp (T007), ApiRequests.cpp (TODO US2).
#   target-only:          ApiServer.cpp (T010).
if(${IDF_TARGET} STREQUAL "linux")
    # Host build: pure sources only (envelope + route table for now; the
    # serializers/parsers join the same SRCS list in US1/US2).
    idf_component_register(
        SRCS "src/ApiEnvelope.cpp"
             "src/ApiRoutes.cpp"
             "src/ApiSerialize.cpp"
             # TODO(US2): "src/ApiRequests.cpp"
        INCLUDE_DIRS "include"
        REQUIRES interfaces cjson network time events
    )
else()
    # Target build: pure sources + the esp_http_server touchpoint. The HTTP /
    # netif / app-format IDF deps are PRIVATE — they appear only inside
    # ApiServer.cpp, never in this component's public headers (the server
    # header holds its handle as an opaque void*, per the ProvisioningPortal
    # precedent). ApiServer.cpp is added to SRCS when it lands in US1.
    idf_component_register(
        SRCS "src/ApiEnvelope.cpp"
             "src/ApiRoutes.cpp"
             "src/ApiSerialize.cpp"
             "src/ApiServer.cpp"
             # TODO(US2): "src/ApiRequests.cpp"
        INCLUDE_DIRS "include"
        REQUIRES interfaces cjson network time events
        PRIV_REQUIRES esp_http_server esp_netif esp_app_format
    )
endif()
