# control — pure watering application logic (feature 011).
#
# Holds the pure, host-tested WateringController (US1) and ReservoirController
# (US2). Both depend ONLY on the header-only `interfaces` component and the
# pure `events` EventLogger — no IDF/esp_* code, no esp_timer (time is injected
# via ITimeProvider/IWallClock). The component therefore compiles identically
# on the linux host (host-tested against the mocks + fakes) and on both device
# targets. No linux guard needed.
idf_component_register(
    SRCS "src/WateringController.cpp"
         "src/ReservoirController.cpp"
    INCLUDE_DIRS "include"
    REQUIRES interfaces events
)
