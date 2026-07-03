# Interface Contracts: Level Sensors, Capability Flag and INA226

Normative contracts for this feature's interfaces, console commands and board
flags. Style follows specs/004 and 005.

## `ILevelSensor` (interfaces component, header-only, no IDF includes)

```
class ILevelSensor {
    virtual void update()          = 0;  // poll driver; owner calls at its cadence
    virtual bool isValid()         = 0;  // false during settle + debounce warm-up
    virtual bool isWaterPresent()  = 0;  // logical, polarity-absorbed, debounced
    virtual bool rawState()        = 0;  // undebounced pin level (diagnostics)
    virtual void notifyPowerOn()   = 0;  // restart settle gating (FW-3 hook)
};
```

Contract:

- `update()` samples the raw input and advances the settle/debounce state machine
  (data-model.md). No update ⇒ no state change: validity and state are functions
  of the update stream, never of wall-clock reads alone.
- `isWaterPresent()` is meaningful only when `isValid()` — consumers gate on
  validity (PR-11's truth table treats invalid as "do not act"). Before the first
  stable window and during settle gating: `isValid()` false.
- Polarity is fully absorbed here via board configuration (FW-5); no consumer ever
  sees a raw-polarity value except through `rawState()` (diagnostics only).
- `notifyPowerOn()` re-arms settle gating (rev2 ≥500 ms; rev1 0 ms). Rail control
  itself is PR-14 (CP1 decision A) — app_main calls this once at boot on rev2.
- Debounce: reported state changes only after `BOARD_LEVEL_DEBOUNCE_MS` of raw
  stability; any flip restarts the window (deliberate divergence from legacy's
  bare reads — parity-checklist §6 entry).
- Fail direction (pinned by host tests, checklist line 97): disconnected input is
  pulled HIGH ⇒ rev1 reads "water present" (fill pump stays off), rev2 reads
  "water absent" (drawing node does not pump). Both fail safe for their topology.

## `IPowerSensor` (interfaces component, header-only, no IDF includes)

```
class IPowerSensor {
    virtual bool  initialize()     = 0;  // idempotent; lazy-capable
    virtual bool  read()           = 0;  // one snapshot: V, I, P refreshed together
    virtual bool  isAvailable()    = 0;  // real identity probe
    virtual int   getLastError()   = 0;  // 0/1/2 (family convention)
    virtual float getBusVoltage()  = 0;  // V   — last-good, NaN before first success
    virtual float getCurrent()     = 0;  // A   — signed; last-good, NaN before first
    virtual float getPower()       = 0;  // W   — last-good, NaN before first success
};
```

Contract: identical validity family as `IEnvironmentalSensor` (gate on read();
last-good values; NaN placeholders; error 1 = not found/identity mismatch, error 2
= communication failure after identification; lazy re-init;
uninitialize-on-bus-error → identity re-probe; `isAvailable()` probe never touches
the error code, a lazy init triggered by it owns its own error reporting).

## `Ina226Sensor` (sensors component, pure logic — builds on linux)

`Ina226Sensor(II2cBus&, uint8_t address, uint32_t shuntMilliOhm)` implements
`IPowerSensor`. Owns: identity check (0xFE == 0x5449, 0xFF == 0x2260),
configuration + calibration writes (data-model.md; register values verified
against TI SBOS547 at implementation), scaling math (1.25 mV / Current_LSB 0.5 mA /
25 × Current_LSB), signed current handling. Host-tested against `MockI2cBus`
with reference vectors (hand-computed from the datasheet formulas, derivations
cited in test comments).

## `DebouncedLevelSensor` + `GpioLevelSensor` (sensors component)

- `DebouncedLevelSensor` (pure): implements `ILevelSensor` over an injected raw
  input source + `ITimeProvider`; all settle/debounce/polarity policy lives here.
  Host-tested with scripted input sequences + `FakeTimeProvider`.
- `GpioLevelSensor` raw-input provider (target-only): configures the pin as input
  with internal pull-up (both boards, R4) and reads it. No logic.

## `LockedLevelSensor` / `LockedPowerSensor` (sensors component, header-only)

Per-call mutex decorators, same pattern and same documented cross-call limitation
as `LockedEnvironmentalSensor` (TODO(PR-11) snapshot helper applies here too).

## Mocks (sensors component, testing/, header-only)

- `MockLevelSensor`: scripted `isValid`/`isWaterPresent`/`rawState` with
  consistency helpers (`scriptValidState(present)`, `scriptInvalid()`) — must
  express all four PR-11 truth-table combinations across two instances, including
  low-dry+high-wet.
- INA226 tests reuse `MockI2cBus` (extended for 16-bit writes, R6).

## `II2cBus` extension (interfaces component — modifies PR-03's seam per its contract)

- NEW virtual `writeRegister16(addr7, reg, uint16_t value)`: register pointer +
  two data bytes **big-endian in one transaction**. Same error semantics as
  `writeRegister` (false on NACK/error/timeout, no retries).
- Non-virtual convenience `readRegister16(addr7, reg, uint16_t &out)` implemented
  in the header over `readRegisters(..., 2)`, big-endian decode.
- `EspI2cBus` and `MockI2cBus` updated accordingly; `MockI2cBus` records 16-bit
  writes into the per-address byte map (big-endian) so existing byte assertions
  remain valid. BME280 call sites are untouched.

## Board contract (board.h)

Per data-model.md table. Enforcement: `BOARD_HAS_RESERVOIR_PUMP=0` ⇒
`BOARD_PIN_RESERVOIR_PUMP` undefined (compile error on unguarded use — RS485-DE
pattern); consistency asserts flag⇒pin; existing reservoir-pin sanity checks
flag-guarded; level pins distinct from each other/I2C/RS485 pins (compile-time).

## Console commands (main/diag_console, thin wrappers)

```
level                 # both sensors: logical + raw + validity   (both boards)
power                 # INA226 V/I/P or ERROR <code> + hint      (BOARD_HAS_INA226 only)
```

- `level` output must distinguish "not yet valid" from wet/dry (SC-005/FR-012).
- `power` failure output distinguishes error 1 (not found) from 2 (read failed).
- `pump reservoir ...` registration is compiled out on `BOARD_HAS_RESERVOIR_PUMP=0`
  builds — rev2 console lists exactly one pump (PR-14 contract).

## Wiring (app_main)

Function-local statics after `pumps_force_off()` (which becomes capability-aware:
only existing pumps are forced off — the invariant "every pump that exists is OFF
first" is unchanged). Level sensors: two `GpioLevelSensor` + `DebouncedLevelSensor`
+ `Locked*` wrappers; `update()` called from the 10 Hz main loop; `notifyPowerOn()`
once at boot on rev2. INA226 (rev2): `Ina226Sensor` on the SAME `EspI2cBus`
instance as the BME280 (bus-sharing contract from PR-03 — never a second bus).

## Deliberate divergences from legacy (parity-checklist §6 candidates)

1. **Debounce** — legacy reads bare pins every loop pass; new: stability-window
   debounce with explicit validity.
2. **Not-yet-valid state** — legacy has no concept; new: settle + warm-up gating
   (FW-3) distinct from wet/dry.
3. **INA226 identity check** — new capability (no legacy INA226 at all).
4. **Reservoir pump capability flag** — rev2 compiles the pump out entirely
   (single-pump decision); legacy always has both.
