# Contract: `IWifiDriver` (driver seam)

Location: `firmware/components/interfaces/include/interfaces/IWifiDriver.h` (NEW). Pure C++ header — **no
IDF includes** (mirrors `IModbusClient`, `II2cBus`). Implemented on target by `EspWifiDriver`; a
`MockWifiDriver` under `network/include/network/testing/` backs host tests.

## Types

```cpp
enum class WifiEvent { None, Connected, GotIp, Disconnected, ConnectFailed };
```

`Connected` = association succeeded (L2); `GotIp` = DHCP lease acquired (the "usable" signal the manager
treats as fully connected); `Disconnected`/`ConnectFailed` = drop / association failure.

## Methods (proposed signatures — final naming at implementation)

| Method | Contract |
|---|---|
| `bool staConnect(const std::string& ssid, const std::string& password)` | Configure STA + begin association. Returns false only on a synchronous config error; success/failure of the *attempt* arrives later as an event. Non-blocking. |
| `void staStop()` | Stop STA / disconnect. Idempotent. |
| `bool apStart(const std::string& ssid, const std::string& password)` | Start SoftAP (WPA2) at 192.168.4.1. Non-blocking; returns false on synchronous config error. |
| `void apStop()` | Stop SoftAP. Idempotent. |
| `WifiEvent pollEvent()` | Drain the next queued event, or `None` if the queue is empty. Called once per manager tick; thread-safe (driver enqueues from the esp_event callback). |
| `int8_t rssi() const` | Last known RSSI in dBm; unspecified when not connected. |

## Behavioral contract

- **Non-blocking**: no method blocks on network I/O; results are delivered via `pollEvent()`. This is what
  lets the pure `WifiManager` stay deterministic and the wifi task never stall.
- **Event ordering**: a successful connect delivers `Connected` then `GotIp`. A failure delivers
  `ConnectFailed` or `Disconnected`. The manager treats `GotIp` as "connected", `Disconnected`/
  `ConnectFailed` as "attempt failed".
- **No timing**: the driver owns no timers; all cadence lives in `WifiManager` via `ITimeProvider`.
- **Isolation**: the driver touches only WiFi/netif/event-loop resources — never pump/sensor state.

## Mock (`MockWifiDriver`) requirements

- Scriptable event queue: `queueEvent(WifiEvent)` (or a helper like `scriptConnectSuccess()` /
  `scriptConnectFailure()`), consumed by `pollEvent()`.
- Records calls: counts of `staConnect`/`staStop`/`apStart`/`apStop`, last ssid/password passed (for
  assertions; the mock may store them since it is host-only test code).
- Settable `rssi()` return.
- No real networking. Deterministic under `FakeTimeProvider`.
