# Phase 0 Research: Level Sensors, Capability Flag and INA226

Decisions resolving the open items from the spec and the pre-spec research report.
Facts cite the frozen legacy code, `docs/parity-checklist.md`, the rev2 design notes
(`hardware/rev2/design-notes/`, untracked hardware track) and `docs/rev2-firmware-notes.md`
(FW-3/FW-5, design review 2026-07-02).

## R1 — Level-sensor interface: per-sensor `ILevelSensor`, two instances

**Decision**: One `ILevelSensor` interface (interfaces component, pure C++): logical
`isWaterPresent()`, `rawState()` (diagnostics), `isValid()` (false during debounce
warm-up and settle gating), `update()` (poll driver — called by the owner at its
cadence). Two instances (low mark GPIO 32, high mark GPIO 33). A pure
`DebouncedLevelSensor` implements it over an injected raw-input seam
(`IDigitalInput`-style single-method functor or a tiny interface — plan keeps this
minimal); the hardware class `GpioLevelSensor` provides the raw read (target-only).

**Rationale**: per-sensor mirrors the actuators pattern (one WaterPump per pump),
lets PR-11 compose its truth table from two independent sensors (incl. the invalid
combination), and keeps the mock trivial. An aggregate "reservoir" type would bake
PR-11 semantics into this layer.

**Alternatives**: two-sensor aggregate interface (rejected — reservoir semantics
belong to PR-11); reusing `IEnvironmentalSensor`-style read()/getters (rejected —
level sensors are polled state, not transactional reads; validity here is
gating-based, not error-code-based).

## R2 — Debounce: N-consecutive-samples over a time window, restart on flip

**Decision**: The debounced state changes only after the raw input has held a new
value for `BOARD_LEVEL_DEBOUNCE_MS` (default **300 ms** on both boards; any raw flip
restarts the window). Sampling is polling-based via `update()` with an injected
time source (`ITimeProvider`, established pattern) — cadence chosen by the owner
(main loop 10 Hz ⇒ ~3 consecutive samples). Before the first stable window
completes, `isValid()` is false. 300 ms sits well under the XKC-Y26's own response
(~500 ms class) and far above any electrical chatter; board-tunable via the board
header, not Kconfig (it is a hardware property, not a user preference).

**Rationale**: deliberate divergence from legacy (bare `digitalRead`, checklist §6
entry required); deterministic and host-testable with `FakeTimeProvider`.

## R3 — Settle gating (FW-3, CP1 decision A)

**Decision**: `DebouncedLevelSensor` exposes `notifyPowerOn()` (or is constructed
with a settle duration and starts gated): readings report invalid until
`BOARD_LEVEL_SETTLE_MS` after the most recent power-on event. rev1 = 0 (rail always
on — constructor starts ungated... rev1 also gets debounce warm-up, which subsumes
settle=0), rev2 = **500 ms** (FW-3). Rail *control* (IO25/`SENS_PWR_EN`) is PR-14;
this PR only guarantees the abstraction honors a power-on notification, and
app_main on rev2 calls it once at boot (rail is on by default at power-up per the
rev2 design; PR-14 wires real switching to it).

**Rationale**: encodes the FW-3 invariant now so PR-14 is wiring, not redesign;
rev1 unaffected.

## R4 — Fail direction per board: pinned as host-tested constants

**Decision**: Internal pull-ups enabled on both boards (rev1 parity checklist line
95; rev2 redundant-but-harmless on top of external 10 kΩ per design notes §6.3).
Host tests pin the documented fail truths (checklist line 97): disconnected input
reads pulled-HIGH ⇒ rev1 (active HIGH) → "water present" → fill pump stays off;
rev2 (active LOW) → "water absent" → drawing node does not pump. Both fail safe for
their topology; the tests exist so a future polarity/pull change trips loudly.

## R5 — `BOARD_HAS_RESERVOIR_PUMP`: RS485-DE enforcement pattern

**Decision**: `board.h`: rev1 `#define BOARD_HAS_RESERVOIR_PUMP 1` + pin as today;
rev2 `#define BOARD_HAS_RESERVOIR_PUMP 0` and **`BOARD_PIN_RESERVOIR_PUMP` removed**
(deliberately undefined ⇒ unguarded reference = compile error, same as
`BOARD_PIN_RS485_DE`). Existing sanity checks referencing the reservoir pin
(`board.h:125-141`) get `#if BOARD_HAS_RESERVOIR_PUMP` guards + a new consistency
assert (flag=1 requires pin defined). `app_main`: `pumps_force_off()` and instance
wiring become capability-aware (rev2 forces off exactly the one existing pump —
QUIRK 2 target intact); `diag_console`: `pump reservoir` registration compiled out
on flag=0 (PR-14's "exactly one pump" check sees compile-time absence). Address
map comment (0x40 pump INA226, 0x41 solar reserved, 0x76/0x77 BME280) recorded in
the rev2 board profile.

## R6 — II2cBus 16-bit extension: add `writeRegister16` + `readRegister16` helpers

**Decision**: Extend `II2cBus` with `writeRegister16(addr7, reg, uint16_t value)`
(pointer + 2 data bytes big-endian, ONE transaction — required for INA226 config/
calibration writes; not composable from single-byte writes) and, for symmetry and
call-site clarity, a non-virtual convenience `readRegister16` implemented on the
interface in terms of `readRegisters` (big-endian decode; INA226 reads are already
expressible as a 2-byte `readRegisters`). `EspI2cBus` implements the new virtual
via `i2c_master_transmit` (3-byte buffer); `MockI2cBus` gets scripting/recording
support for 16-bit writes (stored into the same per-address byte map, big-endian,
so existing byte-level assertions keep working).

**Rationale**: sanctioned by the PR-03 contract ("PR-05 may extend the interface
or compose two 8-bit operations, its call"); one new virtual keeps the seam minimal
and BME280 code untouched.

**Alternatives**: a generic `writeRegisters(addr, reg, buf, len)` (rejected — wider
than any current need; can supersede later if a third device family needs it).

## R7 — `Ina226Sensor`: pure logic over II2cBus, BME280 architecture cloned

**Decision**: `Ina226Sensor` (pure C++, builds on linux) implements a new
`IPowerSensor` interface (busVoltage V, current A signed, power W + the established
validity contract: gate on read(), last-good values NaN-initialized, error codes
0/1/2, lazy re-init, identity check at init, uninitialize-on-bus-error → re-probe).
Identity = manufacturer ID reg 0xFE == 0x5449 and die ID reg 0xFF == 0x2260.
Init sequence: identity → write config (AVG=16, VBUSCT=VSHCT=1.1 ms, MODE=0b111
continuous shunt+bus — values TO BE VERIFIED against TI datasheet SBOS547 during
implementation, flagged) → write calibration `CAL = 0.00512 / (Current_LSB ×
R_shunt)` with Current_LSB = 0.5 mA, R_shunt from Kconfig (mΩ, default 5) ⇒ CAL =
2048 at defaults (matches design-note operating point ~±16.4 A FS). Scaling:
bus V = raw × 1.25 mV; current = raw(signed) × Current_LSB; power = raw × 25 ×
Current_LSB. Host tests pin these against hand-computed reference vectors.
No ALERT/limit usage (pin NC). Compiled per `BOARD_HAS_INA226` (the .cpp is pure
and could build everywhere, but the instance/wiring/console are rev2-only;
CMake keeps the pure file on all targets for host tests — rev1 target simply never
instantiates it, satisfying spec FR-011's "no INA226 configuration present" via
the board flag guards in wiring code).

**Rationale**: maximum reuse of the proven BME280 pattern; datasheet-formula
scaling is exactly the host-testable math Constitution II wants.

## R8 — Console + wiring

**Decision**: `level` command (both boards): both sensors' logical/raw/validity.
`power` command (rev2/`BOARD_HAS_INA226` only): V/I/P or distinct error. Both thin
wrappers, registered before console start. Level sensors are polled from the main
loop (10 Hz, `update()` calls) — no new task (they are cheap GPIO reads; the 5 s
sensor task is BME280's transactional cadence, not a fit). Cross-task access:
`LockedLevelSensor` wrappers (console + future controller), same decorator pattern.
INA226 access from console only this PR → still wrapped (`LockedPowerSensor`) per
the established rule (console REPL + PR-09/PR-11 future readers).

## R9 — Kconfig placement

**Decision**: `CONFIG_WS_INA226_SHUNT_MILLIOHM` (int, default 5, range-checked) in
`firmware/main/Kconfig.projbuild` under the existing "WateringSystem" menu —
first non-board option; a sensors-component Kconfig would be invisible next to the
board choice users already know. Current_LSB stays a compile-time constant in the
driver (derived docs in header); promote to Kconfig only if a real need appears.

## R10 — File overlap with PR-03 (#11)

Implementation touches the same files as PR-03 (`sensors/CMakeLists.txt`,
`app_main.cpp`, `diag_console.*`, `test_apps` harness, `firmware/CLAUDE.md`,
`board.h`) **and depends on its code** (`II2cBus`/`EspI2cBus`). Hard gate:
implementation starts only after PR #11 merges; this spec/plan phase is
conflict-free. Plan is written against the 005 worktree's file state.
