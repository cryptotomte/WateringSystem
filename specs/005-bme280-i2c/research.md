# Phase 0 Research: BME280 Environmental Sensor over I2C

All decisions below resolve the open items flagged in the spec's Assumptions and the
pre-spec research report. Verification was done against authoritative sources: the
pinned toolchain container (`espressif/idf:v6.0.1`) and the IDF Component Registry
API (2026-07-02).

## R1 — Own Bosch compensation code, not the `espressif/bme280` registry component

**Decision**: Implement the BME280 driver in-repo: register access + Bosch datasheet
compensation math in our own pure C++ class. Do not depend on `espressif/bme280`.

**Rationale** (registry state verified via `components.espressif.com` API,
2026-07-02):

- Latest release is **v0.1.1 (2024-12-23)** with a **floating dependency**
  `espressif/i2c_bus: "*"` — a floating range violates Constitution III
  (exact pins only). We would have to pin `i2c_bus` transitively and track two
  third-party components for one sensor.
- It builds on the `i2c_bus` abstraction (esp-iot-solution), which **owns bus
  creation** internally. FR-003 requires one shareable native
  `i2c_master_bus_handle_t` for PR-05's INA226; forcing INA226 through `i2c_bus`
  or fighting over bus ownership adds an abstraction layer we don't control.
- SC-002 requires the compensation math to be host-tested against Bosch reference
  vectors. A third-party component compiled target-only cannot satisfy that; our
  own pure C++ compensation unit satisfies it trivially.
- Maintenance signals are weak: 0.1.x version line, sparse metadata, no examples,
  no declared targets.

**Alternatives considered**: `espressif/bme280` (rejected above); Bosch's official
`BME280_SensorAPI` vendored as C sources (viable, but it is a generic HAL-callback
C API — porting the ~200 lines of datasheet compensation into our C++ class is
simpler than wrapping the vendor HAL, and the datasheet publishes the reference
algorithms we must match anyway).

## R2 — Legacy I2C driver status on IDF v6.0.1 (verified)

**Finding**: `driver/i2c.h` (legacy) **still exists** in v6.0.1
(`/opt/esp/idf/components/driver/i2c/include/driver/i2c.h` in the pinned container)
— deprecated, not removed. The earlier assumption that v6 removed it was wrong.

**Consequence**: none for scope — the PRD/FR-002 mandates the new `esp_driver_i2c`
API (`driver/i2c_master.h`, also verified present). IDF forbids mixing legacy and
new drivers on the same port; nothing else in `firmware/` uses I2C, so the rule is
satisfied by simply never including the legacy header.

## R3 — New i2c_master driver serializes multi-task transactions (verified)

**Finding**: `i2c_master.c` in the pinned container takes a bus-level semaphore
(`bus_lock_mux`) around each transaction (take at `i2c_master.c:1004`, release at
`:1023`/`:1029`), so transactions issued from different tasks against devices on
the same bus serialize safely at transaction granularity.

**Consequence**:

- PR-05's INA226 reader (potentially another task) can share the bus handle without
  extra locking at the bus layer.
- `LockedEnvironmentalSensor` (mutex decorator) is still required — but for
  *snapshot consistency* of our reading (a `read()` updates three values + validity
  atomically as seen by console/web/controller), not for bus safety.

## R4 — Shared I2C bus: `EspI2cBus` class in the sensors component

**Decision**: A target-only `EspI2cBus` class (sensors component) owns
`i2c_new_master_bus()` using the board component's pins (`BOARD_PIN_I2C_SDA`/`SCL`),
implements the `II2cBus` interface (R6), and manages per-address device handles
internally (created on first use, 100 kHz per device). `app_main` instantiates it
as a function-local static (established pattern) and passes it to the BME280
driver; PR-05 passes the **same instance** to the INA226 driver.

**Rationale**: the board component stays a header-only pin table (established
convention); putting bus ownership in app wiring code would spread hardware setup
logic outside driver components. The class boundary gives PR-05 a ready seam and
keeps one single owner for the bus handle.

**Alternatives considered**: bus creation in the board component (rejected: board
is compile-time pin data, no runtime code today); raw handle created in `app_main`
(rejected: hardware calls in wiring code, no mockable seam).

## R5 — Interface shape: standalone `IEnvironmentalSensor` (soil-sensor style)

**Decision**: Port `IEnvironmentalSensor` as a standalone pure C++ interface in the
`interfaces` component, mirroring PR-04's `ISoilSensor` conventions: no `ISensor`
base class, no `getName()`; explicit validity contract — consumers gate on the
`read()` result; getters return last-good values after a failed read (documented:
meaningless before the first successful read); `initialize()` recommended but
lazy-capable; `isAvailable()` performs a real bus probe.

**Rationale**: consistency across the sensor interfaces PR-11 will consume; the
legacy NaN-in-getters behavior is a foot-gun the soil-sensor contract already
removed. Confirmed direction in the spec's Assumptions (divergences documented).

## R6 — Host-test seam: minimal `II2cBus` interface

**Decision**: New `II2cBus` interface (interfaces component, pure C++):

- `probe(address)` → bool/error — device ACK check (maps to `i2c_master_probe`)
- `readRegisters(address, startReg, buf, len)` → error
- `writeRegister(address, reg, value)` → error

`Bme280Sensor` (pure logic, builds on linux target) holds all policy: 0x76/0x77
probing order + re-probe on recovery, chip-ID verification (0xD0 == 0x60),
calibration readout/parsing, sampling configuration sequence (parity profile),
compensation math, Pa→hPa conversion, NaN check, error codes, lazy re-init.
`EspI2cBus` is the only hardware-touching class — no business logic (analogous to
PR-04's `ModbusSoilSensor`/`EspModbusClient` split). `MockI2cBus` (testing header)
scripts register maps, probe results and failure sequences for host tests.

**Rationale**: this is the exact split Constitution II prescribes and PR-04
established; it makes compensation *and* every error path host-testable.

## R7 — Sensor task: app-level, env-only, parity parameters

**Decision**: `sensor_task` in `firmware/main` (app wiring, not a component):
FreeRTOS task, 4096 B stack, priority 1, `vTaskDelayUntil` at 5000 ms (parity
values from the legacy controller task), each cycle calls
`LockedEnvironmentalSensor::read()` and logs validity transitions. Task starts even
if the sensor failed init (lazy re-init does the recovery, parity) and never exits.
Log discipline: WARN once on valid→invalid transition and on recovery; repeated
failures logged at a bounded cadence (every Nth failure) to avoid log flood.

**Rationale**: CP1 confirmed the task is in scope, polling the environmental sensor
only. It lives in app wiring because PR-11's controller will take over or absorb
this cadence — keeping it out of the sensors component avoids baking scheduling
policy into a driver component.

## R8 — Compensation reference vectors

**Decision**: Test vectors come from the Bosch BME280 datasheet's reference
implementation (int32 temperature, int64 pressure, int32 humidity — the
`BME280_compensate_*` routines): a fixed set of (calibration constants, raw ADC
values) → expected physical outputs, including the datasheet's worked example and
edge vectors (negative temperature; extreme-but-legal raw values). Vectors are
baked into the host test as constants with a comment citing their derivation.
Our implementation must reproduce the reference outputs exactly (integer paths)
before the float conversion to °C/%RH/hPa.

**Rationale**: SC-002/FR-005 make the datasheet algorithms the binding reference;
fixed vectors keep the test deterministic and hardware-free.

## R9 — NORMAL-mode readiness at first poll

**Decision**: After init writes the parity sampling profile (NORMAL, T×2/P×16/H×1,
IIR ×16, standby 500 ms), the first conversion completes well within one 5 s task
period, so the first scheduled poll reads real data. The driver additionally treats
all-zero/reset raw values as any other reading (compensation yields a number; no
special-casing) but MUST fail the read cleanly if the chip is mid-reset
(`readRegisters` error or chip-ID mismatch on lazy re-init). No status-register
polling loop is added — parity: the legacy driver never polled status either.

## R10 — File overlap with PR-04 (sequencing constraint)

**Finding**: PR-03's implementation extends files PR-04 (open PR #10) also touches:
`firmware/components/sensors/` (CMakeLists, component yml), `firmware/main/app_main.cpp`,
`diag_console.{h,cpp}`, `test_apps/host/main/{CMakeLists.txt,test_main.cpp}`,
`firmware/CLAUDE.md`.

**Decision**: Implementation starts only after PR #10 merges; this plan is written
against PR-04's versions of those files (read from the 004 worktree). The spec/plan
phase itself is conflict-free (touches only `specs/005-*`).
