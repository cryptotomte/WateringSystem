# time — wall-clock time: pure conversion/plausibility (TimeService), the
# IWallClock target implementation (SystemWallClock) and the SNTP starter
# (SntpClient).
#
# TimeService.cpp is pure C++ (epoch plausibility + Swedish local-time
# formatting via <ctime>/localtime_r, which both the host and target provide)
# and builds on the linux preview target used by the host test suite.
# SystemWallClock.cpp is also pure POSIX (time(nullptr)); it is kept target-side
# by convention only (host tests inject FakeWallClock instead). SntpClient.cpp
# (esp_netif_sntp against CONFIG_WS_SNTP_SERVER + TZ/DST setup) is the only
# genuine IDF touchpoint. Both target-only .cpp files are excluded from the linux
# build, same mechanism as storage/sensors/network; esp_netif provides
# esp_netif_sntp.h. Those esp_* headers appear only in SntpClient.cpp, never in
# public headers.
if(${IDF_TARGET} STREQUAL "linux")
    idf_component_register(
        SRCS "src/TimeService.cpp"
        INCLUDE_DIRS "include"
        REQUIRES interfaces
    )
else()
    idf_component_register(
        SRCS "src/TimeService.cpp"
             "src/SystemWallClock.cpp"
             "src/SntpClient.cpp"
        INCLUDE_DIRS "include"
        REQUIRES interfaces
        PRIV_REQUIRES esp_netif
    )
endif()
