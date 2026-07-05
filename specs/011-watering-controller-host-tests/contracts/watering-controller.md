# Contract: `WateringController` (pure)

`firmware/components/control/include/control/WateringController.h` + `src/WateringController.cpp`. Pure C++17
over interfaces + injected clock; NO IDF includes; host-tested over mocks + `FakeTimeProvider`/`FakeWallClock`.

## Construction (references injected; must outlive the controller)
`WateringController(ISoilSensor& soil, IWaterPump& plant, IConfigStore& config, IDataStorage& storage,
ITimeProvider& clock, IWallClock& wallClock, EventLogger& events)` — or the `Locked*` wrappers at the
wiring site. No watering thresholds hard-coded — all read from `config` each evaluation (runtime-tunable).

## Public surface (proposed)
| Member | Contract |
|---|---|
| `void tick()` | one evaluation: `plant.update()` (self-stop/cap) → read soil snapshot → fail-safe → watering decision → periodic data-log. Non-blocking; called at a fixed cadence from the controller task. |
| `bool startManual(int durationS)` | operator override: `plant.runFor(clamp 1..300)`, set `manualRunActive`; returns the pump result. Exempt from automatic fail-safe. |
| `void stop()` | `plant.stop()`; clears `manualRunActive`. Always works, any mode. |
| (status getters as needed for the API/console) | current mode, running, last-burst/last-valid times — for reporting. |

## Behavioral contract (host-test targets — SC-001)
- **Automatic start**: enabled + not running + valid moisture ≤ low + soak elapsed → `runFor(burst)`.
- **Soak gate (FR-003)**: after a burst ends, no new automatic burst until `soakPause` elapsed, even if soil
  ≤ low. Assert: no start at soak-1ms; start at soak. Manual is exempt.
- **Stop at target (FR-002)**: running + moisture ≥ high → `stop()`.
- **Fail-safe (FR-005/006)**: in automatic, running, when soil unavailable OR stale (>30 000 ms / never) OR
  moisture out of 0..100 → immediate `stop()` + `logFailsafe`, no watering decision. Checked BEFORE the soak
  gate — assert a pending soak pause does not delay a safety stop.
- **Gate on read (FR-004)**: before the first successful read, no action on placeholder values.
- **Manual (FR-007/008/009/010)**: a manual run continues despite sensor failure; capped at 300 s;
  auto-started runs count as automatic (fail-safe applies); `stop()` clears the override.
- **Graceful degradation (FR-015)**: constructs + allows manual even if a sensor failed to init (pump +
  storage present).
- **Logging (FR-014)**: every `dataLogInterval`, log env + soil (NPK only ≥ 0) with `nowEpoch()`; gate
  validity on `isTimeSet()`. Fail-safe events via `logFailsafe`; pump transitions NOT logged here
  (owned by `SystemObserver`).

## Isolation (FR-017)
The controller only calls the injected interfaces; it makes no hardware/network calls and no blocking I/O.
On-target it runs on a watchdog-registered task, never blocking or blocked by network/HTTP.
