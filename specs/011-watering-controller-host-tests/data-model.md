# Phase 1 Data Model: Watering Controller

Pure in-memory state + the exact decision tables. All timing via injected `ITimeProvider::nowMs()`
(monotonic) for gates/staleness and `IWallClock::nowEpoch()` for log timestamps. No persisted schema new to
this PR (config → PR-06 NVS, history/events → PR-06 littlefs).

## Constants (parity §1–§3 + config-store defaults)

| Name | Value | Source |
|---|---|---|
| moisture low / high threshold | 30 % / 55 % (config) | `IConfigStore` defaults |
| burst (watering) duration | 20 s (config, 1..300) | `IConfigStore` |
| soak pause (min interval) | 300 s (config, ≥1) | `IConfigStore` (enforced — divergence) |
| staleness window | 30 000 ms | parity §2 |
| reservoir max fill | 300 s (pump cap) | parity §3 |
| data-log cadence | 300 000 ms (5 min) | `IConfigStore` (≥60 000) |
| pump hard max runtime | 300 s | `WaterPump::kDefaultMaxRunTimeMs` |

## WateringController state

| Field | Type | Meaning |
|---|---|---|
| mode | derived | automatic when `getWateringEnabled()` and no active operator run; else manual/override |
| lastBurstEndMs | int64 | monotonic time the last automatic burst ended (soak gate origin) |
| lastValidSoilMs | int64 | monotonic time of the last successful, in-range soil read (0 = never) |
| manualRunActive | bool | an operator-started run is in progress (bypasses fail-safe) |
| lastDataLogMs | int64 | monotonic time of the last data-log write |

## Automatic evaluation (per controller tick; pump `update()` runs first for self-stop/cap)

Order is safety-first; the soak gate is checked LAST and never gates a safety stop.

```text
tick():
  pump.update()                      # enforce timed self-stop + 300 s cap (actuator layer)
  soil = lockedSoil.snapshot()       # {readOk, available, moisture, ...} one locked read

  if manualRunActive:                # operator override — bypass automatic safety + gate
      if !pump.isRunning(): manualRunActive = false   # run ended -> back to automatic
      return

  if !getWateringEnabled():          # manual mode (suspended automatic)
      return

  # ---- FAIL-SAFE (unconditional, FR-005/006) ----
  stale = (lastValidSoilMs == 0) || (now - lastValidSoilMs > 30_000)
  invalid = soil.readOk && (soil.moisture < 0 || soil.moisture > 100)
  if !soil.available || stale || invalid:
      if pump.isRunning(): pump.stop(); logFailsafe(reason)
      return                          # no watering decision

  if soil.readOk: lastValidSoilMs = now
  else: return                        # gate on read result, not placeholder (FR-004)

  # ---- WATERING DECISION ----
  if pump.isRunning():
      if soil.moisture >= high: pump.stop(); lastBurstEndMs = now   # reached target
      # else keep running (within a burst)
  else:
      if soil.moisture <= low:
          soakElapsed = (lastBurstEndMs == 0) || (now - lastBurstEndMs >= soakPauseMs)
          if soakElapsed: pump.runFor(burstDurationS)   # start next burst
          # else: soak pause active -> do NOT start (FR-003), even though dry
```

Transitions:
- **start burst**: enabled + not running + moisture ≤ low + soak elapsed → `runFor(burst)`.
- **stop at target**: running + moisture ≥ high → `stop()`, set `lastBurstEndMs=now`.
- **burst self-stop**: the pump's `update()` stops it at `burstDurationS`; the controller observes
  `!isRunning()` next tick and (if still ≤ low) waits out the soak pause before the next burst.
- **fail-safe stop**: any of unavailable/stale/invalid while running (automatic) → immediate `stop()`.
- **manual**: `startManual(duration≤300)` → `runFor`, `manualRunActive=true`; exempt from fail-safe; `stop()`
  clears it.

Invariants: the soak gate NEVER blocks a `stop()` (safety or high-threshold). Manual is never blocked by
automatic logic. `lastValidSoilMs` advances only on a successful in-range read.

## ReservoirController state machine (pure; board-independent)

Inputs: `low.snapshot()` + `high.snapshot()` (each {valid, waterPresent}), `enabled`, `pump`.

Evaluate only when `enabled` and pump not running:

| low | high | action |
|---|---|---|
| !valid OR high !valid | — | **no action** (invalid ≠ dry) |
| wet | wet | full → ensure stopped |
| wet | dry | sufficient → no action |
| dry | dry | **`pump.runFor(fillDuration)`** (start fill) |
| dry | wet | implausible → **no action** |

While the fill pump runs (regardless of how started):
- high mark reads wet → `pump.stop()` immediately.
- `pump.update()` aborts at the 300 s max fill (pump cap).

Feature gate: `!enabled` (or `!BOARD_HAS_RESERVOIR_PUMP`) → `pump.stop()` (force off), skip all logic.

**Post-abort cooldown (resolved 2026-07-05):** when a fill ends via `StopReason::MaxRuntimeForced`, record
`lastAbortMs`; suppress a new automatic fill while `now - lastAbortMs < kReservoirRefillCooldownMs` (a
documented constant, default ~60 s, tunable) even if still low-dry — prevents an endless 300 s cycle on a
stuck high sensor / empty source. A normal high-wet stop does NOT arm the cooldown. Manual fill bypasses it.
Reservoir state adds `lastAbortMs` (int64, 0 = none).

## Snapshot helper shape (added to Locked* wrappers, resolves TODO(PR-11))

```cpp
struct SoilSnapshot { bool readOk; bool available; int lastError; float moisture, temperature, humidity,
                      ph, ec, nitrogen, phosphorus, potassium; };   // one locked call
struct EnvSnapshot  { bool valid; float temperature, humidity, pressure; };
struct LevelSnapshot{ bool valid; bool waterPresent; };
```
One mutex acquisition returns validity + values together, closing the read()-then-getters cross-call gap.

## Logging (FR-014, D9)

- Every `dataLogInterval`: `storeSensorReading("env_temperature", nowEpoch, …)` etc. + soil metrics; NPK
  only when ≥ 0. Timestamp = `IWallClock::nowEpoch()`; if `!isTimeSet()`, do not treat it as a valid
  wall-clock time (log with the best-known / mark unsynced — no bogus 1970 as valid).
- Events: `logFailsafe("soil-unavailable" | "soil-stale" | "moisture-invalid" | "reservoir-…")`;
  pump start/stop transitions remain owned by `SystemObserver` (no double-log).

## Host-test matrix (must cover — SC-001)

Plant: start-at-low; no-start-in-soak; restart-after-soak-while-dry; stop-at-high; fail-safe
unavailable/stale(>30 s)/invalid each stops+blocks; fail-safe-not-delayed-by-soak; gate-on-read (no act
before first read); manual bypasses fail-safe; manual capped 300 s; stop clears override; graceful
degradation (sensor init fail). Reservoir: all 5 truth-table rows; stop-on-high; max-fill abort; feature
disabled forces off. Logging: cadence + epoch + NPK≥0 + time-not-set. Config: setter validation +
runtime change picked up next tick.
