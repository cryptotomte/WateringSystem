# control — pure watering application logic (feature 011).
#
# Holds the pure, host-tested WateringController and ReservoirController
# (arriving in US1/US2). Both depend ONLY on the header-only `interfaces`
# component and the pure `events` EventLogger — no IDF/esp_* code, no
# esp_timer (time is injected via ITimeProvider/IWallClock). The component
# therefore compiles identically on the linux host (host-tested against the
# mocks + fakes) and on both device targets. No linux guard needed.
#
# TODO(US1/US2): add SRCS "src/WateringController.cpp"
#                         "src/ReservoirController.cpp"
# once those pure sources land. Until then the component registers as a
# header-only/interface component (INCLUDE_DIRS only) so it configures
# cleanly on host + both targets.
idf_component_register(
    INCLUDE_DIRS "include"
    REQUIRES interfaces events
)
