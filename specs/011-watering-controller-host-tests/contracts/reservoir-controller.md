# Contract: `ReservoirController` (pure state machine)

`firmware/components/control/include/control/ReservoirController.h` + `src/ReservoirController.cpp`. Pure,
board-independent; host-tested regardless of board (the `BOARD_HAS_RESERVOIR_PUMP` flag gates only
construction/wiring in `app_main`, never the logic — FR-013).

## Construction
`ReservoirController(ILevelSensor& lowMark, ILevelSensor& highMark, IWaterPump& fillPump,
ITimeProvider& clock, EventLogger& events)` (or the `Locked*` wrappers). Fill duration + the 300 s cap come
from the pump (`runFor` / max-runtime).

## Public surface
| Member | Contract |
|---|---|
| `void tick(bool enabled, bool autoLevelControl)` | `fillPump.update()` (cap) → running-safety (stop on high / cap) → if enabled+auto+not-running, evaluate the truth table. |
| `bool startManualFill(int durationS)` | manual fill (refuse if already full = high wet); capped at 300 s. |
| `void stop()` | `fillPump.stop()`. |

## Truth table (auto; enabled, auto-level, pump not running)
| low mark | high mark | action |
|---|---|---|
| invalid, or high invalid | — | **no action** (invalid never = dry — ILevelSensor contract) |
| wet | wet | full → ensure stopped |
| wet | dry | sufficient → no action |
| dry | dry | **start fill** (`fillPump.runFor(fill)`) |
| dry | wet | physically implausible → **no action** |

## Running safety (manual + auto)
- high mark reads wet → `stop()` immediately.
- `fillPump.update()` aborts at the 300 s hard max fill.

## Post-abort cooldown (resolved by Paul 2026-07-05 — deliberate divergence from parity)
After a fill ends by the max-runtime abort (`StopReason::MaxRuntimeForced`), record the abort time and do
NOT start another automatic fill until a cooldown (`kReservoirRefillCooldownMs`, a documented constant,
default ~60 s, tunable) has elapsed — even if the water still reads low. This prevents a stuck high sensor
or an empty source from producing an endless 300 s pump cycle. A normal stop (high-wet reached) does NOT
trigger the cooldown. Manual fill is unaffected.

## Feature gate (FR-013)
`!enabled` (or the board lacks the pump) → `fillPump.stop()` (force off) + skip all logic.

## Host-test targets (SC-004)
Every truth-table row incl. both invalid-sensor and implausible → no-action; start-on-low-dry;
stop-on-high-wet while running; max-fill abort at 300 s; disabled forces off; after-abort re-evaluation is
safe (no tight re-slam). Driven over two `MockLevelSensor` + `MockWaterPump` (real `WaterPump` over
`FakeTimeProvider`).
