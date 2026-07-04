# Contract: wall-clock time (`IWallClock` + `TimeService` + `SntpClient`)

## `IWallClock` (interfaces, pure, no IDF/`<ctime>` in the header)

`firmware/components/interfaces/include/interfaces/IWallClock.h`

```cpp
class IWallClock {
public:
    virtual ~IWallClock() = default;
    virtual uint32_t nowEpoch() const = 0;   // wall-clock epoch seconds (low/boot value before sync)
    virtual bool isTimeSet() const = 0;       // true iff nowEpoch >= plausibility threshold
};
```

- Target impl `SystemWallClock`: `nowEpoch()` = `time(nullptr)`; `isTimeSet()` = `nowEpoch() >=
  kMinPlausibleEpoch`.
- Host fake `FakeWallClock`: settable epoch + is-set, deterministic for `EventLogger` tests.

## `TimeService` (pure — host-tested)

`firmware/components/time/include/time/TimeService.h`

| Member | Contract |
|---|---|
| `static bool isPlausibleEpoch(uint32_t e)` | `e >= kMinPlausibleEpoch` (2020-01-01Z). |
| `static std::string formatLocal(uint32_t epoch)` | Renders Swedish local time; requires the process TZ set to `CET-1CEST,M3.5.0,M10.5.0/3`. Winter→CET(+01), summer→CEST(+02). |
| `SyncStatus status() const` / `void onSynced(uint32_t epoch)` | Tracks `synced` + `lastSyncEpoch` for the console `time` line. |

Host tests (`test_time.cpp`): a known winter epoch and summer epoch convert with the correct offsets; the
DST boundary (last Sun Mar / last Sun Oct) flips correctly; `isPlausibleEpoch` false for 0/1970, true for a
2020+ epoch.

## `SntpClient` (target-only, thin)

`firmware/components/time/src/SntpClient.cpp` (excluded from linux build)

| Method | Contract |
|---|---|
| `void applyTimezone()` | `setenv("TZ", CET-1CEST,M3.5.0,M10.5.0/3, 1); tzset();` once at init. |
| `void start()` | `esp_netif_sntp_init` against `CONFIG_WS_SNTP_SERVER` (default `se.pool.ntp.org`), step-set mode, register a sync callback; idempotent. Non-blocking; failure to reach the server is non-fatal and retried by the SNTP service. |
| sync callback | on a plausible time, updates `SyncStatus` (synced + lastSyncEpoch). |

- `start()` is invoked once when the WiFi station first reaches `Connected` (by the `system_observer`).
- The device never blocks waiting for sync; `isTimeSet()` stays false until the first successful sync.
