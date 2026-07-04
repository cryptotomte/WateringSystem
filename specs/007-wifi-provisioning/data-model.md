# Phase 1 Data Model: WiFi Provisioning & Station Management

Entities are in-memory firmware state (no schema/DB). Credentials persist via `IConfigStore` (PR-06).

## Entities

### WifiCredentials (persisted, reused from PR-06)

| Field | Type | Rules |
|---|---|---|
| ssid | string | 1–32 chars to be valid; **empty string = unconfigured (factory state)** |
| password | string | empty (open network) OR ≥ 8 chars; max 64 (`kWifiPasswordMaxLen`) |

- Source of truth: `IConfigStore` (NVS namespace `wscfg`, keys `wifi_ssid`/`wifi_pass`). Values never
  logged. Written atomically by `setWifiCredentials()` (both keys + one commit); cleared by
  `clearWifiCredentials()`. Wiped by `factoryReset()` (whole default NVS partition).

### WifiConnectionState (in-memory, exposed via snapshot)

| Field | Type | Notes |
|---|---|---|
| state | enum `WifiState` | see state machine below |
| rssi | int8 (dBm) | valid only in `Connected`; weak-signal note < −80 dBm (parity) |
| consecutiveFailures | uint8 | 0–5; drives the pause transition |
| disconnectCount | uint32 | monotonic, for diagnostics/status |
| ipAcquired | bool | true after `GotIp` |

- Exposed to consumers as an immutable **snapshot** (single mutex acquisition), per research D9.

### ProvisioningApConfig (in-memory, from Kconfig + board)

| Field | Source | Value |
|---|---|---|
| ssid | Kconfig / constant | fixed setup SSID (e.g. `WateringSystem-Setup`), confirmed at impl |
| ip | fixed | 192.168.4.1 (ESP32 softAP default) |
| authmode | fixed | WPA2 |
| password | `CONFIG_WS_PROV_AP_PASSWORD` | documented non-secret; brand-new; legacy never reused |

### ReconnectPolicy (construction parameters, defaults = parity)

| Param | Default | Meaning |
|---|---|---|
| retryIntervalMs | 10 000 | delay between STA connect attempts while reconnecting |
| failuresBeforePause | 5 | consecutive failures that trigger the long pause |
| pauseMs | 60 000 | extra wait after `failuresBeforePause` before next round |
| monitorIntervalMs | 5 000 | connection-health check cadence while connected (suspended in AP) |

## WifiManager state machine

States (`enum class WifiState`): `Provisioning`, `Connecting`, `Connected`, `Reconnecting`,
`ReconnectPaused`.

```text
                 decideBootMode == Provisioning
   [boot] ───────────────────────────────────────────────► Provisioning
      │                                                        │ (credentials submitted+persisted → restart)
      │ decideBootMode == Station                              └───► [restart] → [boot]
      ▼
  Connecting ──── GotIp ─────────────────────────────────► Connected
      │  ▲                                                     │
      │  │ retryIntervalMs elapsed                             │ monitor(5s): link lost / Disconnected event
      │  │                                                     ▼
      │  └───────────────────────── Reconnecting ◄────────────┘
      │                                 │  ▲
      │ ConnectFailed / Disconnected    │  │ retryIntervalMs elapsed (attempt++)
      └─────────────────────────────────┘  │
                                            │ consecutiveFailures == failuresBeforePause
                                            ▼
                                     ReconnectPaused ── pauseMs elapsed ──► Reconnecting (failures reset)
```

Transition rules:

- **boot → Provisioning**: `decideBootMode(credentialsPresent, buttonHeld)` returns Provisioning when
  credentials absent OR config button held at boot. On button-forced provisioning, credentials are cleared
  first. In Provisioning: AP up + portal serving; STA monitoring suspended.
- **Provisioning → restart**: a valid credential submission persists to NVS and schedules a restart
  (~3 s) so the HTTP success response is delivered first. There is no timed abandonment of provisioning
  (device stays provisionable indefinitely — edge case in spec).
- **boot → Connecting**: Station mode; issue STA connect with stored credentials.
- **Connecting → Connected**: on `GotIp`. Resets `consecutiveFailures` to 0.
- **Connecting/Reconnecting → Reconnecting**: on `ConnectFailed`/`Disconnected`, increment
  `consecutiveFailures`; schedule next attempt after `retryIntervalMs`.
- **Reconnecting → ReconnectPaused**: when `consecutiveFailures` reaches `failuresBeforePause` (5).
- **ReconnectPaused → Reconnecting**: after `pauseMs` (60 s); reset `consecutiveFailures` to 0 and begin a
  new round. Never transitions to a reboot (FR-013 — no boot loop).
- **Connected → Reconnecting**: monitor tick (every 5 s) detects link loss, or a `Disconnected` event
  arrives; `disconnectCount++`.

Invariants:

- The manager only ever calls `IWifiDriver` and reads `ITimeProvider`/`IConfigStore`. It holds **no
  reference to any watering/pump/sensor object** (FR-014, Constitution I) — enforced by its constructor
  signature and asserted structurally in host tests.
- All timers are computed from `ITimeProvider::nowMs()` deltas; the manager never sleeps or blocks.
- A driver that never reports `GotIp` (wrong password) keeps the machine cycling
  Reconnecting↔ReconnectPaused forever — bounded memory, no reboot.

## Validation rules (pure, host-tested)

- `validateWifiCredentials(ssid, password)`: reject if `ssid.empty()` or `ssid.size() > 32`; reject if
  `!password.empty() && password.size() < 8`; reject if `password.size() > 64`; else accept.
- `decideBootMode(credentialsPresent, buttonHeld)`: `buttonHeld || !credentialsPresent ⇒ Provisioning`,
  else `Station`. (Truth table is a host-test target — all four rows.)
