# Phase 0 Research: WiFi Provisioning & Station Management

All decisions grounded in the verified codebase map (origin/main) and the pre-made project decisions
recorded in `docs/prd/PR-07-wifi-provisioning.md`, `docs/parity-checklist.md` §7, and the Fable→Opus
handover. No open `NEEDS CLARIFICATION` items remain.

## D1 — Provisioning mechanism: custom SoftAP portal

- **Decision**: Custom SoftAP portal (AP at 192.168.4.1, WPA2, minimal setup page + one POST handler on a
  standalone `esp_http_server`).
- **Rationale**: Pre-decided by Paul (2026-06-10). Feature parity with the Arduino unit; works from any
  phone browser; no dependency on Espressif provisioning apps. IDF `wifi_provisioning` is a documented
  fallback ONLY if the portal proves fragile during implementation — in which case re-escalate to Paul.
- **Alternatives considered**: IDF `wifi_provisioning` (SoftAP/BLE transport) — deferred fallback; BLE
  rejected earlier (NimBLE ~300–400 KB would not fit the 1.5 MiB slot budget).

## D2 — Host-testable seam: pure WifiManager + IWifiDriver

- **Decision**: Put all timing/state logic in a pure `WifiManager` (compiled on host + target) behind a
  new `IWifiDriver` interface in the `interfaces` component; keep `esp_wifi`/`esp_netif`/`esp_event` in a
  thin `EspWifiDriver` excluded from the linux build.
- **Rationale**: Mirrors the enforced triad already used by `ModbusSoilSensor`/`IModbusClient` and
  `Bme280Sensor`/`II2cBus`. Satisfies Constitution II (host-testability) and lets the reconnect schedule
  be tested deterministically with `FakeTimeProvider` instead of wall-clock waits.
- **Alternatives considered**: Driving `esp_wifi` directly from a task with inline timing — rejected: not
  host-testable, violates the interface-layer rule, and hard to assert the 10 s/60 s schedule in CI.

## D3 — Reconnection strategy = parity fixed-interval (NOT exponential backoff)

- **Decision**: On loss, retry every **10 s**; after **5** consecutive failures, wait an extra **60 s**
  before the next round; monitor connection health every **5 s** while in STA; monitoring suspended in AP
  mode.
- **Rationale**: This is the *actual* Arduino behavior (`docs/parity-checklist.md` §7,
  `src/main.cpp:102-105, 922-968`). The "exponential backoff" phrasing in older docs is a myth from a code
  comment. Constants are runtime-relevant and captured as `WifiManager` construction parameters (defaults
  = parity values) so they can be tuned without touching logic.
- **Alternatives considered**: Exponential backoff — would be a deliberate divergence; rejected to hold
  parity unless a concrete reason emerges (would be flagged explicitly if chosen).

## D4 — Event-driven driver, tick-driven manager

- **Decision**: `esp_wifi` is event-driven; `EspWifiDriver` translates `esp_event` callbacks
  (`WIFI_EVENT_STA_*`, `IP_EVENT_STA_GOT_IP`) into discrete `WifiEvent`s that the pure `WifiManager`
  drains on each `tick(nowMs)`. The manager owns all timers via `ITimeProvider`; the driver owns no
  timing.
- **Rationale**: Keeps the pure/hardware split clean and the state machine fully deterministic in host
  tests (the mock driver scripts events; `FakeTimeProvider.advance()` drives timers). The default event
  loop + netif are initialized once in `app_main` (currently absent — see D7).
- **Alternatives considered**: Blocking `esp_wifi_connect()` + polling status — rejected; less faithful to
  IDF, harder to isolate.

## D5 — Boot-mode decision is pure

- **Decision**: A pure `decideBootMode(credentialsPresent, configButtonHeld) -> {Station, Provisioning}`
  (config button OR empty credentials ⇒ Provisioning; else Station). The GPIO read of
  `BOARD_PIN_BTN_CONFIG` and the credential presence check (`!IConfigStore::getWifiSsid().empty()`) feed
  it from `app_main`; forcing provisioning on a configured device clears credentials
  (`clearWifiCredentials()`) then enters AP mode.
- **Rationale**: The decision table is the part worth host-testing (all four combinations); the GPIO read
  itself is trivial hardware. Matches parity entry conditions (unconfigured→AP; button-held-at-boot→
  emergency provisioning).
- **Alternatives considered**: Embedding the decision inside the driver — rejected (not host-testable).

## D6 — Credential validation is pure and layered over IConfigStore

- **Decision**: A pure `validateWifiCredentials(ssid, password)` enforces SSID length 1–32 and password
  empty-or-≥8, returning a typed result; the portal calls it before `IConfigStore::setWifiCredentials()`.
- **Rationale**: `IConfigStore` enforces only max lengths (`kWifiSsidMaxLen=32`, `kWifiPasswordMaxLen=64`)
  and never the WPA2 min-8 rule or the SSID-non-empty rule, so the feature owns the min bounds. Pure ⇒
  host-tested (accept/reject table incl. empty-password open-network case and 1–7-char reject).
- **Alternatives considered**: Relying solely on `setWifiCredentials()` validation — rejected; it does not
  reject a 1–7-char password or an empty SSID submit.

## D7 — app_main must add event loop + netif (NVS already present)

- **Decision**: Add `esp_netif_init()` and `esp_event_loop_create_default()` in `app_main` after the
  existing `nvs_flash_init()` and before constructing the WiFi driver; create the default STA and AP
  netifs inside `EspWifiDriver`.
- **Rationale**: The research confirmed neither is called anywhere today, but both are prerequisites for
  `esp_wifi`. NVS is already initialized (`app_main.cpp:~173`) and can be relied upon. `pumps_force_off()`
  stays the very first action (safety invariant untouched).
- **Alternatives considered**: Initializing netif/event-loop lazily inside the driver — acceptable, but
  centralizing one-time system init in `app_main` matches the existing I2C-bus "one owner" precedent.

## D8 — Status exposure & LED scope

- **Decision**: Expose the current `WifiConnectionState` via a thread-safe snapshot getter on the WiFi
  manager (single read, no cross-call gap), consumable by status reporting and the status LED. PR-07 owns
  only the **WiFi/provisioning-related** LED behavior on `BOARD_PIN_STATUS_LED`: connect-attempt toggle
  (~500 ms) and config-button-hold blink (~100 ms), per parity §7/§9. Any non-WiFi LED semantics are left
  to later PRs.
- **Rationale**: FR-015 requires state exposure, not a full LED subsystem. Scoping the LED to WiFi events
  avoids gold-plating and keeps the parity §9 non-WiFi LED items with their owning features. LED patterns
  are HIL-verified (not host-testable).
- **Alternatives considered**: A general LED/indicator abstraction now — rejected as premature; no other
  consumer exists yet (this would be the first).

## D9 — Concurrency: snapshot over Locked decorator for status reads

- **Decision**: The WiFi manager is owned and mutated by exactly one task (the wifi task). Status readers
  (diag console / future status API) read a **snapshot** guarded by a single mutex acquisition, avoiding
  the documented cross-call atomicity gap of the `Locked*` per-call decorators.
- **Rationale**: Handover explicitly flags that `Locked*` wrappers are per-call atomic only; a snapshot
  getter is the load-bearing pattern for consumers. Single-writer/many-reader with a snapshot avoids a
  read-modify-write race.
- **Alternatives considered**: `LockedWifiManager` per-call decorator — insufficient for a consistent
  multi-field status read; snapshot chosen instead.

## D10 — Portal ↔ existing console overlap (bookkeeping)

- **Decision**: Note that the diag console already exposes `config wifi <ssid> <pass>` / `wifi-clear` and
  reports `wifi=configured|unconfigured` without echoing values (`diag_console.cpp`). The portal is an
  additional path to the same `IConfigStore` credentials; both must agree. Flag for the HIL checklist that
  console-set and portal-set credentials are equivalent.
- **Rationale**: Avoids a surprise at review/HIL where two provisioning paths exist. No code conflict —
  both funnel through `setWifiCredentials()`.

## Open risks carried to implementation

- **R1 — Portal fragility**: if the SoftAP portal proves unreliable during HIL, the fallback is IDF
  `wifi_provisioning`; that is a re-escalation to Paul, not an autonomous switch (per D1).
- **R2 — Binary size**: confirm the portal + WiFi stack still fit the 1.5 MiB OTA slot after
  implementation (`idf.py size`); flag if margin is tight.
- **R3 — Brownout**: parity QUIRK 4 (Arduino disables brownout to mask WiFi power spikes). Target keeps
  brownout enabled; watch for spike-induced resets on the rev1 rig during the AP-power-cycle HIL test.
- **R4 — rev2 pins provisional**: `BOARD_PIN_STATUS_LED`/`BTN_CONFIG` on rev2 are `TODO(SYNC1)`; rev2 LED
  behavior is not finalized until the pin map freezes.
