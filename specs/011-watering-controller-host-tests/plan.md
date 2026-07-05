# Implementation Plan: Watering Controller (host-tested application logic)

**Branch**: `011-watering-controller-host-tests` | **Date**: 2026-07-05 | **Spec**: [spec.md](./spec.md)

**Input**: `specs/011-watering-controller-host-tests/spec.md`; PR brief
`docs/prd/PR-11-watering-controller-host-tests.md`; parity `docs/parity-checklist.md` §1–§3 (+ QUIRK 1/2).

## Summary

Port the watering application logic to a **pure, host-tested** `WateringController` (+ a pure reservoir
state machine) in a new `control` component, consuming only the merged interfaces + an injected clock — zero
IDF includes, 100 % host-tested in CI. The controller adds the **soak-pause gate** (enforced
minimum-watering-interval, measured from burst end — the deliberate divergence), the **fail-safe stops**
(unavailable / stale > 30 s / invalid moisture in automatic mode, never delayed by the gate), **manual
override** (= mode flag, capped 300 s), the **reservoir truth table** (with the invalid + implausible
"do-not-act" rows), and **data logging** (5 min, epoch, time-not-set aware). On-target, a controller task
(watchdog-registered) plus a periodic soil reader replace PR-09's interim direct-drive path and make soil
data live. Enablers land alongside: consistent-snapshot helpers on the locked sensor wrappers (the booked
`TODO(PR-11)`), coherent soil-mock helpers, and routing `rs485test` through a locked path.

## Technical Context

**Language/Version**: C++17, ESP-IDF v6.0.1 (Docker), target esp32. The controller + reservoir logic are
pure (host-buildable on the linux preview target); no `esp_*`/`esp_timer` — time is injected.

**Primary Dependencies**: project interfaces only — `ISoilSensor`, `IEnvironmentalSensor`, `ILevelSensor`
(×2), `IWaterPump`, `IConfigStore`, `IDataStorage`, `ITimeProvider`, `IWallClock`; `EventLogger` (pure).
Their `Locked*` wrappers + mocks + `FakeTimeProvider`/`FakeWallClock` back the tests. No new managed deps.

**Storage**: reads config via `IConfigStore` (thresholds/duration/soak/enabled/intervals); logs history +
events via `IDataStorage` (epoch from `IWallClock`).

**Testing**: Unity host suite — `test_watering_controller.cpp` (+ `test_reservoir.cpp`) over the mocks +
fakes; every decision/safety branch covered (CI gate, master-PRD 100 %-host-tested criterion). On-target
wiring (controller task, periodic soil reader, snapshot helpers, rs485test) is HIL-verified.

**Target Platform**: ESP32-WROOM-32E; `BOARD_REV1_DEVKIT` (plant + reservoir) / `BOARD_REV2` (plant only;
level sensors status-only). Reservoir logic host-tested on both; wiring gated by `BOARD_HAS_RESERVOIR_PUMP`.

**Project Type**: Embedded firmware component (`control`) + `main/` task wiring.

**Performance Goals**: controller cadence well under the 20 s watchdog; fail-safe stop within the 30 s
staleness window; watering fully independent of network/HTTP (Constitution I / FR-017).

**Constraints**: `pumps_force_off()` stays first in `app_main`; controller makes no hardware calls except
through interfaces; manual/burst capped at 300 s (pump's own cap); soak gate never delays a safety stop;
both targets build; controller must not double-log pump transitions with `SystemObserver`.

**Scale/Scope**: one pure `WateringController` + one pure `ReservoirController` (state machine), snapshot
helpers on 4 locked wrappers, soil-mock helpers, a `soil_reader` task (or extend `sensor_task`), controller
task wiring in `app_main`, `rs485test` fix, 2 host suites. This is an L PR.

## Constitution Check

*GATE: evaluated before Phase 0 and re-checked after Phase 1. Result: PASS (no violations).*

- **I. Safety First (NON-NEGOTIABLE)** — PASS and central: the fail-safe stops (FR-005/006), the manual
  bypass (FR-007), the reservoir caps + invalid-do-not-act (FR-011/012), and "safety stop never delayed by
  the soak gate" (FR-006) are the core deliverable, ALL host-tested (SC-001). `pumps_force_off()` stays the
  first `app_main` action; the controller runs on a watchdog-registered task isolated from network (FR-017).
- **II. Host-Testability** — PASS, maximally: the entire controller + reservoir state machine are pure over
  interfaces + an injected clock, giving the master-PRD "100 % host-tested watering logic". No IDF includes;
  hardware stays behind the existing drivers/wrappers.
- **III. Reproducible Builds** — PASS. No new managed deps; both targets build; `dependencies.lock`
  untouched.
- **IV. Frozen Legacy** — PASS. `src/`/`include/`/`data/`/`test/`/`platformio.ini` are read-only reference
  (the legacy `WateringController.cpp`/`main.cpp` behavior is *ported*, not modified).
- **V. Checkpoint-Gated Workflow** — PASS. Implementation delegated to `implementer` after CP2.
- **VI. English Outward** — PASS.

**Deliberate-divergence notes (recorded so they don't surface as review findings):** the soak gate is
enforced (frozen code doesn't), measured from burst END; manual runs are capped at 300 s (frozen code is
uncapped); "manual" is the mode flag (`wateringEnabled==false`), not a pump flag (the new `IWaterPump` has
no `isManualMode()`); QUIRK-1 mode semantics are corrected (auto-started = automatic/fail-safe-subject,
operator-started = manual/bypass). The reservoir state machine adds the "sensor invalid → do not act" row
the frozen code lacks.

## Project Structure

### Documentation (this feature)

```text
specs/011-watering-controller-host-tests/
├── plan.md · research.md · data-model.md · quickstart.md
├── contracts/{watering-controller.md, reservoir-controller.md, snapshot-helpers.md}
├── checklists/requirements.md
└── tasks.md            # /speckit-tasks output (not created here)
```

### Source Code (repository root)

```text
firmware/
├── components/
│   ├── control/                      # NEW component (pure, host + target)
│   │   ├── CMakeLists.txt            # REQUIRES interfaces events; pure — no linux guard needed
│   │   ├── include/control/
│   │   │   ├── WateringController.h   # pure: automatic decision + soak gate + fail-safe + manual + logging
│   │   │   ├── ReservoirController.h  # pure: level truth table + fill timer (board-independent)
│   │   │   └── ControllerConfig.h?    # (optional) snapshot/param structs
│   │   └── src/{WateringController.cpp, ReservoirController.cpp}
│   ├── sensors/include/sensors/
│   │   ├── LockedSoilSensor.h         # EDIT — add consistent-snapshot helper (read+values+validity, one lock)
│   │   ├── LockedEnvironmentalSensor.h# EDIT — snapshot helper (resolve TODO(PR-11))
│   │   ├── LockedLevelSensor.h        # EDIT — snapshot helper (resolve TODO(PR-11))
│   │   └── testing/MockSoilSensor.h   # EDIT — add scriptSuccessfulRead/scriptFailedRead (coherence)
│   └── (LockedPowerSensor.h snapshot TODO — optional, power not used by the controller)
├── main/
│   ├── watering_task.{h,cpp}          # NEW — controller task (watchdog-registered) OR fold into main loop
│   ├── soil_reader_task.{h,cpp}       # NEW — periodic soil read (or extend sensor_task to soil)
│   ├── app_main.cpp                   # EDIT — construct WateringController + ReservoirController; start
│   │                                  #        the tasks; API mode flag drives it; keep pumps_force_off first
│   └── diag_console.cpp               # EDIT — route rs485test through a locked path (fix the PR-11 race)
└── test_apps/host/main/
    ├── test_watering_controller.cpp   # NEW — automatic + soak + fail-safe + manual + logging branches
    ├── test_reservoir.cpp             # NEW — reservoir truth table + caps + invalid/implausible rows
    ├── test_main.cpp · CMakeLists.txt # EDIT — register run_watering_controller_tests/run_reservoir_tests; REQUIRES control
```

**Structure Decision**: A new pure `control` component holds `WateringController` + `ReservoirController`
(both depend only on `interfaces` + `EventLogger`; no linux guard needed — fully portable), giving the
master-PRD "100 % host-tested" logic. The reservoir state machine is a separate pure class so it is tested
independently of the board wiring (the `BOARD_HAS_RESERVOIR_PUMP` flag gates only construction in
`app_main`). The snapshot helpers are added to the existing `Locked*` wrappers (resolving the booked
`TODO(PR-11)` and giving the controller/periodic-reader torn-read-free access). The controller runs on a
dedicated watchdog-registered task in `main/` (mirroring `sensor_task`); a periodic soil reader (a small
task, or `sensor_task` extended to soil) makes soil data live. The API's mode flag drives the controller
indirectly (it reads `getWateringEnabled()` each cycle — no direct ApiServer↔controller call path).

## Complexity Tracking

> No constitution violations. Table intentionally empty.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|--------------------------------------|
| —         | —          | —                                    |
