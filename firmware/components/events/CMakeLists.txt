# events — persistent event logging (feature 008 US2).
#
# EventLogger is PURE C++: it composes an IDataStorage& and an IWallClock&
# (both from the header-only `interfaces` component) and builds the category +
# detail string for each typed producer, then calls storeEvent(). It contains
# NO IDF/esp_* code — producers take primitive args (int reset reason, const
# char* state/cause strings), so this component stays free of any network or
# IDF coupling and compiles identically on the linux host (host-tested against
# MockDataStorage + FakeWallClock) and on the target. No linux guard needed.
idf_component_register(
    SRCS "src/EventLogger.cpp"
    INCLUDE_DIRS "include"
    REQUIRES interfaces
)
