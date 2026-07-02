# Feature Specification: BME280 Environmental Sensor over I2C

**Feature Branch**: `005-bme280-i2c`

**Created**: 2026-07-02

**Status**: Draft

**Input**: User description: "Port the BME280 environmental sensor to the new ESP-IDF
i2c_master API behind a hardware-independent environmental-sensor interface, replacing
the Adafruit library, per docs/prd/PR-03-bme280-i2c.md (authoritative mini-PRD; ground
truth for behavior is docs/parity-checklist.md §5). Includes the periodic sensor task
(5 s cadence) and mock implementations for host tests."

## Clarifications

### Session 2026-07-02

- Q: Sampling profile — exact legacy configuration (parity) or a deliberate switch to
  forced-mode reads suited to the 5 s cadence? → A: Parity — NORMAL mode with the
  exact legacy settings (oversampling T×2 / P×16 / H×1, IIR ×16, standby 500 ms), so
  the HIL comparison against the Arduino unit is like-for-like. Forced mode remains a
  possible future power optimization. Confirmed by Paul at Checkpoint 1.
- Q: Deliver the periodic sensor task now (PRD-03 scope) or defer periodic polling to
  PR-11 as PR-04 did? → A: Deliver the task now, polling the environmental sensor
  only at 5 s; the soil-sensor cadence stays with PR-11's controller, which may take
  over or reuse this task's pattern. Confirmed by Paul at Checkpoint 1.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Environmental readings on the bench rig (Priority: P1)

Paul flashes the rev1 rig, which has the real BME280 module wired to the I2C pins. The
firmware reads temperature, humidity and pressure on a steady 5-second cadence and the
values agree with what the production Arduino unit shows for the same environment —
same units (°C, %RH, hPa), same smoothness characteristics (identical sensor sampling
configuration). From the serial console he can also trigger an immediate reading for
spot checks.

**Why this priority**: Air temperature and humidity feed the greenhouse status display
and future watering decisions (PR-11); a driver that reproduces the production unit's
readings on the rig is the core deliverable of this PR.

**Independent Test**: Flash the rig with the BME280 attached, watch the periodic log
output and/or issue the console reading command, compare values against the Arduino
unit in the same environment (±0.5 °C, ±3 %RH, ±1 hPa).

**Acceptance Scenarios**:

1. **Given** the rig with the BME280 connected, **When** the firmware runs, **Then**
   temperature (°C), humidity (%RH) and pressure (hPa) are read every 5 seconds and
   the values are plausible for the environment.
2. **Given** the rig and the Arduino unit side by side, **When** readings are compared,
   **Then** they agree within sensor tolerance (±0.5 °C, ±3 %RH, ±1 hPa) — the sensor
   is configured with the exact legacy sampling profile (continuous measurement,
   oversampling T×2 / P×16 / H×1, IIR filter ×16, standby 500 ms), so filtering and
   smoothness match.
3. **Given** the rig, **When** the operator runs the environmental-reading console
   command, **Then** an immediate reading is taken and displayed with all three values
   and their validity state.

---

### User Story 2 - Sensor faults yield invalid data, never wrong data (Priority: P2)

The BME280 module is unplugged (or fails) while the system runs. The system does not
crash, does not reboot, and does not present garbage as truth: readings are flagged
invalid with a logged warning, and when the module is plugged back in the driver
recovers on a subsequent poll without a restart. The same applies at boot: a missing
sensor never blocks startup — the system comes up, keeps retrying, and starts
delivering readings as soon as the sensor appears.

**Why this priority**: Constitution principle I (Safety First) — downstream automatic
watering logic (PR-11) must be able to trust the validity signal. Graceful degradation
on sensor loss is parity behavior (`docs/parity-checklist.md` §5).

**Independent Test**: On the rig, unplug the BME280 mid-operation; observe invalid
readings + logged warnings and no reboot; replug and observe automatic recovery
without restart. Boot with the sensor absent; observe normal startup and recovery
when attached.

**Acceptance Scenarios**:

1. **Given** a running system with a connected sensor, **When** the module is
   unplugged and the next poll occurs, **Then** the read fails with a distinct error,
   the reading is flagged invalid, a warning is logged, and the system keeps running
   (no crash, no watchdog reset).
2. **Given** an unplugged sensor, **When** the module is reconnected, **Then** a
   subsequent poll succeeds without any manual intervention or restart.
3. **Given** a system booting with no sensor attached, **When** startup completes,
   **Then** the system runs normally with readings flagged invalid, and attaching the
   sensor later leads to automatic recovery (lazy re-initialization, parity).
4. **Given** a sensor whose measurement comes back unusable (not-a-number after
   compensation), **When** the reading is evaluated, **Then** the read fails with a
   distinct error and no partial values are presented as valid (parity: NaN fails the
   read).
5. **Given** a failed read, **When** downstream code checks the reading, **Then**
   validity is unambiguous — consumers gate on the read result, never on sniffing
   value contents.

---

### User Story 3 - Works with both module address variants (Priority: P3)

Paul (or a future builder) wires up a BME280 breakout. Some modules ship strapped to
I2C address 0x76, others to 0x77 (the greenhouse unit's module is at 0x77). The
firmware finds the sensor on either address automatically and verifies it is really a
BME280 (chip identity check) before trusting it — no code change or build flag needed
to swap modules.

**Why this priority**: Removes a hardware-assembly foot-gun. This is a deliberate new
capability (the legacy firmware hard-codes 0x77); it makes the rig and rev2 builds
robust to module sourcing.

**Independent Test**: Attach a 0x76-strapped module and a 0x77-strapped module in
turn; both are detected and deliver readings without any configuration change.

**Acceptance Scenarios**:

1. **Given** a module strapped to 0x76, **When** the driver initializes, **Then** the
   sensor is found, its chip identity is verified, and readings flow.
2. **Given** a module strapped to 0x77, **When** the driver initializes, **Then** the
   same happens.
3. **Given** a device at a probed address that is not a BME280 (wrong chip identity),
   **When** the driver initializes, **Then** that device is rejected and the sensor is
   reported unavailable rather than misread.

---

### User Story 4 - Sensor behavior testable without hardware (Priority: P4)

An AI developer changes compensation, validation or error-handling logic and runs the
host test suite in CI. The raw-to-physical compensation math is verified against the
sensor vendor's published reference vectors, and the invalid-data paths (bus errors,
absent sensor, unusable values) are verified against mock implementations — no devkit,
no sensor, failures block the merge.

**Why this priority**: Constitution principle II (Host-Testability). The mock
environmental sensor created here is also an input for PR-11's watering-controller
tests.

**Independent Test**: Run the host test suite on a machine with no hardware attached;
compensation and error-path tests pass deterministically.

**Acceptance Scenarios**:

1. **Given** the vendor datasheet's example calibration constants and raw readings,
   **When** the compensation math runs on the host, **Then** the computed temperature,
   humidity and pressure match the datasheet's reference results.
2. **Given** a mocked bus/sensor that reports a communication error, **When** a
   reading is requested, **Then** the result is invalid with a distinct error and no
   stale values leak through as fresh.
3. **Given** a mock environmental sensor scripted with known values, **When** a
   consumer (e.g. a future controller test) reads it, **Then** it observes exactly the
   scripted values and validity states.
4. **Given** CI on a clean checkout, **When** the host tests run, **Then** they
   complete without any hardware or device-specific environment.

---

### Edge Cases

- Sensor disappears *between* the availability check and the read, or mid-transaction:
  the read fails with an error — never a hang, never a partially-updated "valid"
  reading.
- Sensor is replugged at the *other* address (module swapped 0x76 ↔ 0x77 while
  unpowered): recovery re-probes both addresses, so the swap is transparent.
- A different I2C device ACKs at 0x76/0x77 (address collision): chip identity check
  rejects it; the driver keeps reporting unavailable instead of decoding garbage.
- First reading immediately after initialization: in continuous-measurement mode the
  first data may not be ready instantly; the first poll must yield either a valid
  reading or a clean invalid result — never garbage from empty registers.
- Concurrent access (periodic task + console command, later web server): reads and
  accessor calls serialize safely; torn reads (temperature from one cycle, humidity
  from another presented as one snapshot) must not happen across the shared-access
  boundary.
- The I2C bus will be shared with another device family later (INA226, PR-05): bus
  setup is one-time and additional devices must be attachable without reworking this
  driver.
- Repeated failed polls (sensor absent for hours): no resource leak, no log flood
  beyond a reasonable warning cadence, and the retry cost stays bounded.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Environmental sensor functionality MUST be exposed through a
  hardware-independent interface (pure C++, no hardware-SDK types) providing
  temperature (°C), humidity (%RH), pressure (hPa) and explicit validity/availability
  reporting, usable from host tests without any hardware headers. The interface
  contract MUST follow the conventions established by the existing soil-sensor
  interface: consumers gate on the read result; accessors return last-good values
  after a failed read; initialization is lazy-capable.
- **FR-002**: The driver MUST communicate over the I2C bus defined by the board
  component (rev1: SDA 21, SCL 22) at 100 kHz standard mode, using the current
  (non-deprecated) platform I2C master driver — not the legacy I2C API.
- **FR-003**: The I2C bus MUST be created once and be shareable: additional devices
  (INA226 in PR-05) attach to the same bus without changes to this driver or bus
  teardown/re-init.
- **FR-004**: At initialization the driver MUST locate the sensor by probing I2C
  addresses 0x76 and 0x77 and MUST verify the chip identity before use; a responding
  device with the wrong identity is rejected (sensor reported unavailable). Recovery
  after sensor loss MUST re-probe both addresses.
- **FR-005**: The driver MUST read the sensor's factory calibration data and apply the
  vendor-specified compensation to produce physical values: temperature in °C,
  relative humidity in %RH, pressure in hPa (parity: legacy converts Pa → hPa). The
  compensation math MUST be host-testable against the vendor datasheet's reference
  vectors.
- **FR-006**: Sensor sampling configuration MUST match the legacy unit exactly
  (parity, `docs/parity-checklist.md` §5): continuous measurement (NORMAL mode),
  oversampling T×2 / P×16 / H×1, IIR filter ×16, standby 500 ms — set explicitly, not
  relying on any library default.
- **FR-007**: A failed read (bus/communication error, absent sensor, or a
  compensation result that is not a number) MUST flag the reading invalid with a
  distinct error and MUST NOT crash, hang, or present partial values as valid
  (parity: NaN fails the read). No numeric range validation is applied beyond the
  NaN check (parity: the legacy driver validates nothing else).
- **FR-008**: A sensor absent at boot MUST NOT block startup; the driver MUST support
  lazy (re-)initialization so that a sensor attached or re-attached later is picked up
  on a subsequent poll without restart (parity, `docs/parity-checklist.md` §5).
- **FR-009**: Availability reporting MUST reflect reality: an availability check on an
  initialized sensor MUST be able to detect that the sensor has disappeared (deliberate
  divergence from legacy, which caches "available" forever after first init — aligned
  with the soil-sensor contract where availability checks touch the bus).
- **FR-010**: A dedicated periodic sensor task MUST poll the environmental sensor
  every 5 seconds (parity cadence) and publish readings for other parts of the system;
  shared access MUST be protected so concurrent readers (console now, web server in
  PR-09, controller in PR-11) never observe torn or interleaved state (established
  mutex-decorator pattern). The task keeps running and retrying through sensor
  failures.
- **FR-011**: A serial console command MUST exist on the rig to trigger and display an
  immediate environmental reading including validity state — the HIL verification
  path, following the existing console command pattern.
- **FR-012**: A mock environmental sensor MUST exist for host tests, sufficient to
  script known values, validity states and failure sequences for consumer tests; the
  driver's pure logic (compensation, error paths) MUST be host-testable behind a
  mockable seam.
- **FR-013**: Both board targets MUST build green in CI with the component enabled;
  hardware-touching code MUST be excluded from the host-test build following the
  established component split.

### Key Entities

- **Environmental reading**: temperature (°C), humidity (%RH), pressure (hPa) plus
  validity/error state; produced atomically per poll — one reading is one snapshot.
- **Environmental sensor interface**: the hardware-independent contract (read,
  availability, last-good accessors, error reporting) consumed by the sensor task,
  console, and later PRs (PR-09 web, PR-11 controller).
- **I2C bus**: the single shared bus instance defined by board pins; owner of device
  attachments (BME280 now, INA226 in PR-05).
- **Sensor task**: the periodic poller (5 s) that turns the driver into
  continuously-published readings under concurrency protection.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Both board variants build green in CI from a clean checkout with the new
  component enabled.
- **SC-002**: Host test suite verifies the compensation math against the vendor
  datasheet's reference vectors and the invalid-data paths (bus error, absent sensor,
  NaN) with zero hardware dependencies, passing deterministically in CI.
- **SC-003**: On the rig, temperature/humidity/pressure are logged every 5 seconds and
  agree with the Arduino unit's readings in the same environment within ±0.5 °C,
  ±3 %RH, ±1 hPa (HIL checklist).
- **SC-004**: Unplugging the BME280 on the running rig yields invalid readings and a
  logged warning with zero crashes or reboots; replugging recovers automatically
  without restart; booting without the sensor and attaching it later also recovers
  (HIL checklist).
- **SC-005**: Modules strapped to 0x76 and 0x77 both work with no configuration change
  (HIL checklist, if both module variants are on hand; otherwise verified for the
  available variant plus host-level probe-order tests).
- **SC-006**: The console reading command works on the first attempt using documented
  syntax and its output is sufficient to distinguish "sensor absent" from "sensor
  present but read failed" (HIL checklist).

## Assumptions

- **Sampling profile stays on parity** (continuous NORMAL mode with the exact legacy
  oversampling/filter/standby settings) rather than switching to on-demand
  forced-mode reads — confirmed at Checkpoint 1 (see Clarifications). Rationale: the
  HIL acceptance criterion compares readings against the Arduino unit — identical
  filtering makes that comparison meaningful. Forced mode remains a possible future
  optimization (power) once the parity baseline is proven.
- **Interface style follows the soil-sensor precedent**, not the legacy `ISensor`
  hierarchy: a standalone environmental-sensor interface with an explicit validity
  contract (gate on read result, last-good values retained after failure). This is a
  documented divergence from the legacy driver, which leaves NaN in its accessors
  after a failed read; the legacy behavior is a foot-gun the new contract removes.
  The PRD's mention of `ISensor`/`IEnvironmentalSensor` is read as "the interface
  role", not a mandate for a base-class hierarchy — final shape is a plan-phase
  decision.
- **Availability semantics diverge deliberately from legacy**: legacy caches
  "available" after first init and can report a dead sensor as alive; the new driver
  aligns with the soil-sensor contract (availability checks reflect the bus truth).
  The unplug/replug HIL criterion depends on this.
- **Address probing is a new capability, not parity**: legacy hard-codes 0x77 (the
  greenhouse unit's module). Probe order and re-probe-on-recovery behavior are as
  specified in FR-004; the bench rig's module is at 0x77.
- **Registry component vs own compensation code is a plan-phase decision** (PRD
  explicitly defers it). The binding requirements are observable: compensation math
  host-tested against vendor reference vectors (FR-005) and the new-driver-API
  mandate (FR-002). Note the host-testability criterion weighs toward own
  compensation code; the registry component's IDF v6.0.1 compatibility is unverified.
- **Where the bus instance lives** (board layer, sensor component, or app wiring) is
  a plan-phase decision; the binding requirement is FR-003 (one bus, shareable with
  PR-05).
- **The sensor task polls the environmental sensor only** — confirmed at
  Checkpoint 1 (see Clarifications). The legacy task also read the soil sensor; here
  the soil read cadence stays out of scope (PR-04 delivered on-demand reads; the
  periodic soil reader and fail-safe consumption arrive with the controller in
  PR-11, which may then take over or consume this task's pattern).
- **Web exposure and data logging are out of scope** (PR-09/PR-06 territory); the
  reading contract (cached last-good values, validity flag) is designed so PR-09 can
  serve cached values without blocking on a fresh read (parity QUIRK 5).
- **No numeric plausibility-range validation** on temperature/humidity/pressure
  (parity: legacy checks only NaN). Range validation would be a behavior change;
  downstream consumers may add their own guards.
