# time — wall-clock time: pure conversion/plausibility (TimeService), the
# IWallClock target implementation (SystemWallClock) and the SNTP starter
# (SntpClient).
#
# TimeService.cpp is pure C++ (epoch plausibility + Swedish local-time
# formatting via <ctime>/localtime_r, which both the host and target
# provide) and builds on the linux preview target used by the host test
# suite. SystemWallClock.cpp (time(nullptr) wall clock) and SntpClient.cpp
# (esp_netif_sntp against CONFIG_WS_SNTP_SERVER + TZ/DST setup) are the only
# hardware/IDF touchpoints and are excluded from the linux build, same
# mechanism as storage/sensors/network.
#
# TODO(T008-T010): add the SRCS below as the sources are implemented, and
# uncomment the PRIV_REQUIRES on the target branch. Until then the component
# is header-only (SyncStatus.h + testing/FakeWallClock.h) so it configures
# and builds empty on both the linux and target builds.
if(${IDF_TARGET} STREQUAL "linux")
    idf_component_register(
        # TODO(T008): SRCS "src/TimeService.cpp"
        INCLUDE_DIRS "include"
        REQUIRES interfaces
    )
else()
    # Target build only: SystemWallClock (IWallClock over time(nullptr)) and
    # the SNTP starter. PRIV: esp_netif provides esp_netif_sntp.h, lwip the
    # underlying SNTP module; esp_timer/esp_system for scheduling and reset
    # reason. These headers appear only in the target-only .cpp files, never
    # in this component's public headers.
    idf_component_register(
        # TODO(T008-T010): SRCS "src/TimeService.cpp" "src/SystemWallClock.cpp"
        #                       "src/SntpClient.cpp"
        INCLUDE_DIRS "include"
        REQUIRES interfaces
        # TODO(T009-T010): PRIV_REQUIRES esp_netif lwip esp_timer esp_system
    )
endif()
