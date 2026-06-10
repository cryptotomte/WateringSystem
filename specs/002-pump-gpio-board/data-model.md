# Data Model: Pump Actuator Layer and Board Abstraction

**Feature**: 002-pump-gpio-board | **Date**: 2026-06-10

No persistent data in this feature (NVS config arrives in PR-06). The model is
in-memory state.

## Entity: Pump (WaterPump)

| Field | Type | Semantics |
|---|---|---|
| `name` | `std::string` | Identity for logs/diagnostics: `"plant"`, `"reservoir"` |
| `state` | enum `Stopped \| Running` | Output state mirror; never diverges from `applyOutput` history |
| `runStartedAtMs` | `int64_t` | Time (monotonic ms) the current run started; valid only in `Running` |
| `runDurationMs` | `int64_t` | Requested duration of the current timed run |
| `maxRunTimeMs` | `int64_t` (constexpr config, 300 000) | Hard cap; enforced for every run mode |
| `accumulatedRunTimeMs` | `int64_t` | Total run time since boot (statistics, status reporting) |
| `lastStopReason` | enum `None \| Commanded \| DurationElapsed \| MaxRuntimeForced` | Observability of why the pump stopped (FR-009) |

### State machine

```
                 runFor(durationS)  [0 < durationS·1000 ≤ maxRunTimeMs]   (API in seconds, internal state in ms)
   ┌─────────┐ ──────────────────────────────────────────► ┌─────────┐
   │ Stopped │                                             │ Running │
   └─────────┘ ◄────────────────────────────────────────── └─────────┘
                 stop()                  → Commanded
                 update(): now-start ≥ duration → DurationElapsed
                 update(): now-start ≥ maxRunTime → MaxRuntimeForced (logged ERROR/WARN)
```

**Transition rules** (from spec FR-007..FR-009 + edge cases):

- `runFor` with `durationS ≤ 0` → rejected (no indefinite runs — deliberate change).
- `runFor` with `durationS·1000 > maxRunTimeMs` → rejected with clear error (no silent clamping).
- `runFor` while `Running` → rejected; running clock NOT restarted/extended.
- `stop` while `Stopped` → success no-op.
- Every transition into `Running` calls `applyOutput(true)` exactly once;
  every transition into `Stopped` calls `applyOutput(false)` exactly once.
- Construction/`init` → `applyOutput(false)` before anything else (boot fail-safe).
- Enforcement is evaluated in `update(nowMs)`; between polls the pump may overrun
  by at most one poll interval (main loop ≥ 10 Hz ⇒ ≤ 100 ms, within SC-003's 1 s).

## Entity: Board profile (compile-time)

| Field | rev1_devkit | rev2 (provisional, TODO(SYNC1)) |
|---|---|---|
| `BOARD_NAME` | `"rev1_devkit"` | `"rev2"` |
| I2C SDA / SCL | 21 / 22 | 21 / 22 |
| RS485 TX / RX | 16 / 17 | 16 / 17 |
| RS485 DE | 25 (`BOARD_HAS_RS485_DE 1`) | — (flag 0, macro undefined) |
| Plant pump / Reservoir pump | 26 / 27 | 26 / 27 |
| Level low / high | 32 / 33 | 32 / 33 |
| `BOARD_LEVEL_SENSOR_ACTIVE_LOW` | 0 | 1 (2N7002 inverter) |
| `BOARD_HAS_INA226` | 0 | 1 |
| Status LED | 2 | 2 |
| Manual button / Config button | 5 / 18 | 5 / 18 |

Exactly one profile active per build (Kconfig choice; `#error` otherwise).
Source of truth for rev1 values: `docs/parity-checklist.md` (NOT `docs/hardware.md`).

## Entity: Time source

| Implementation | Behavior |
|---|---|
| `ITimeProvider` | `int64_t nowMs()` — monotonic, never wraps in device lifetime (int64 ms) |
| `EspTimeProvider` (target) | `esp_timer_get_time() / 1000` |
| `FakeTimeProvider` (host) | starts at arbitrary epoch; `advance(ms)` under test control |

## Relationships

```
app_main ──owns──► GpioWaterPump("plant", BOARD_PIN_PUMP_PLANT)    ──is-a──► WaterPump ──implements──► IWaterPump
         ──owns──► GpioWaterPump("reservoir", BOARD_PIN_PUMP_RESERVOIR) ─┘
         ──owns──► EspTimeProvider ──implements──► ITimeProvider (injected into both pumps)
         ──polls──► pump.update() each loop iteration
diag_console ──uses──► IWaterPump& (both instances, by name)
host tests ──own──► MockWaterPump ──is-a──► WaterPump (logic under test) + FakeTimeProvider
```
