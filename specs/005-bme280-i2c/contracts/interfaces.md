# Interface Contracts: BME280 Environmental Sensor over I2C

Normative contracts for the interfaces, console command and task behavior this
feature introduces. Header doc comments summarize these; this file is the source
of truth. Style follows `specs/004-modbus-soil-sensor/contracts/interfaces.md`.

## `IEnvironmentalSensor` (interfaces component, header-only, no IDF includes)

Standalone pure C++ interface (soil-sensor conventions: no base class, no
`getName()` — research R5). Deliberate divergences from the legacy driver are
listed at the end of this file.

```
class IEnvironmentalSensor {
    virtual bool  initialize()      = 0;  // idempotent; lazy-capable
    virtual bool  read()            = 0;  // one atomic snapshot
    virtual bool  isAvailable()     = 0;  // REAL bus probe (chip-ID read)
    virtual int   getLastError()    = 0;  // 0/1/2, see data-model.md
    virtual float getTemperature()  = 0;  // °C     — last-good value
    virtual float getHumidity()     = 0;  // %RH    — last-good value
    virtual float getPressure()     = 0;  // hPa    — last-good value
};
```

Contract:

- **`initialize()`**: probes 0x76 then 0x77, verifies chip identity (0x60), reads
  calibration, writes the parity sampling profile (ctrl_hum → ctrl_meas → config,
  data-model.md). Returns false with error 1 if no BME280 is found. Idempotent;
  callers need not call it first — `read()`/`isAvailable()` initialize lazily
  (parity).
- **`read()`**: burst-reads 0xF7–0xFE in one transaction, compensates T → P → H
  (t_fine ordering), converts to °C/%RH/hPa. Atomic: either all three getters are
  refreshed, or the call fails (error 1 on lost/absent sensor, error 2 on bus
  error or NaN) and the last-good values remain untouched. **Consumers gate on the
  read result, never on value plausibility.** Before the first successful read the
  getters return meaningless placeholders. A bus error marks the driver
  uninitialized → the next call re-probes both addresses (recovery, spec FR-004).
- **`isAvailable()`**: real chip-ID read every call — never cached state. Does not
  modify `getLastError()`.
- Exactly one bus attempt per operation; no automatic retry (recovery comes from
  the caller's poll cadence — same philosophy as the soil sensor).

## `II2cBus` (interfaces component, header-only, no IDF includes)

The hardware seam (research R6). Minimal register-oriented I2C master contract:

```
class II2cBus {
    virtual bool probe(uint8_t address7) = 0;                 // ACK check
    virtual bool readRegisters(uint8_t address7, uint8_t startReg,
                               uint8_t *buf, size_t len) = 0; // write-reg, repeated-start read
    virtual bool writeRegister(uint8_t address7, uint8_t reg,
                               uint8_t value) = 0;
};
```

Contract:

- 7-bit addresses. `readRegisters` = register-pointer write followed by an
  N-byte read in one transaction (repeated start — required for correct BME280
  burst reads). Returns false on NACK/bus error/timeout; no retries at this layer.
- Implementations serialize safely for multi-task use at transaction granularity
  (the ESP implementation inherits this from the i2c_master driver's bus lock,
  research R3; mocks are used single-threaded in host tests).
- No BME280 knowledge in this interface — it is reused by PR-05 (INA226 uses
  16-bit register values; PR-05 may extend the interface or compose two 8-bit
  operations, its call).

## `Bme280Sensor` (sensors component, pure logic — builds on linux)

`Bme280Sensor(II2cBus &bus)` implements `IEnvironmentalSensor`. Holds ALL policy:
probing order, chip-ID check, calibration parsing, sampling configuration,
compensation math, unit conversion, NaN check, error codes, lazy re-init,
uninitialize-on-bus-error. No IDF includes. Host-tested against `MockI2cBus`.

## `EspI2cBus` (sensors component, target-only — excluded on linux)

Implements `II2cBus` over `driver/i2c_master.h` (new API — never the legacy
`driver/i2c.h`):

- Owns the single `i2c_master_bus_handle_t`: created once from
  `BOARD_PIN_I2C_SDA`/`BOARD_PIN_I2C_SCL` (board component; never hard-coded),
  port auto-select, internal pull-ups enabled (breakouts carry their own; the
  internal ones are harmless belt-and-braces).
- Manages per-address device handles internally, created on first use,
  **100 kHz** standard mode per device (spec FR-002).
- `probe()` maps to `i2c_master_probe`; `readRegisters` to
  `i2c_master_transmit_receive`; `writeRegister` to `i2c_master_transmit`.
  Finite timeouts (no infinite waits); errors logged at debug level and returned
  as false — classification is `Bme280Sensor`'s job.
- **Bus sharing (spec FR-003)**: the one `EspI2cBus` instance is constructed in
  `app_main` (function-local static, established pattern) and passed to every
  I2C driver — PR-05's INA226 receives the same instance. No second bus creation
  on these pins is permitted.

## `LockedEnvironmentalSensor` (sensors component, header-only)

Mutex decorator wrapping any `IEnvironmentalSensor`; serializes each interface
call (FreeRTOS mutex, same pattern as `LockedSoilSensor`/`LockedWaterPump`).
Anything accessed from more than one task (sensor task + console REPL now,
web/controller later) is wrapped and accessed ONLY through the wrapper.
Known pattern limitation (documented, matches LockedSoilSensor): a
read-then-getters sequence spans multiple lock acquisitions; the consistent-
snapshot helper is PR-11 bookkeeping (see PR-04's TODO(PR-11) notes).

## `MockI2cBus` / `MockEnvironmentalSensor` (sensors component, testing/, header-only)

- `MockI2cBus`: scriptable register map + probe results + failure injection
  (NACK on address X, error on register read N, corrupt data) — drives the real
  `Bme280Sensor` in host tests (chip-ID mismatch, absent sensor, mid-read loss,
  recovery re-probe, calibration parsing, compensation vectors).
- `MockEnvironmentalSensor`: scripted values/validity/error sequences for
  consumer tests (console-level logic now, PR-11 controller tests later). Include
  consistency helpers from the start (scriptFailedRead/scriptSuccessfulRead —
  lesson recorded from PR-04's MockSoilSensor bookkeeping).

## Console command (main/diag_console)

HIL verification path; thin wrapper, no logic (established rule). Registered as
`diag_console_register_env(IEnvironmentalSensor &sensor)` before console start.

```
env          # one read(); prints temperature/humidity/pressure or error
```

Output contract (exact format fixed at implementation, these fields are binding):

- Success: the three values with units (`temperature=23.4 C humidity=45.2 %RH
  pressure=1013.2 hPa`).
- Failure: `ERROR <code>` plus a human hint distinguishing code 1 ("sensor not
  found") from code 2 ("read failed") — SC-006.

## Sensor task (main/, app wiring)

- FreeRTOS task, 4096 B stack, priority 1, `vTaskDelayUntil` period 5000 ms
  (parity parameters, research R7). Polls `LockedEnvironmentalSensor::read()`.
- Starts even when the sensor failed init (lazy re-init recovers later — parity);
  never exits; never reboots on failures.
- Logging: WARN on valid→invalid transition and on recovery; repeated failures at
  bounded cadence (every Nth failure), INFO-level periodic readings consistent
  with the legacy 5 s status print.
- Publishing = the Locked sensor itself (last-good values + validity via
  getLastError); no separate shared-state structure this PR (PR-09/PR-11 consume
  the same wrapper).

## Deliberate divergences from legacy (parity-checklist §6 candidates)

1. **Address probing 0x76/0x77 + chip-ID check** — legacy hard-codes 0x77, no
   identity check. New capability (spec US3).
2. **Last-good getter values after failed read** — legacy leaves NaN in getters.
   Aligned with the soil-sensor contract; consumers gate on read().
3. **`isAvailable()` = real bus probe** — legacy caches available-after-init
   forever. Aligned with the soil-sensor contract (spec FR-009).
4. **Synchronized cross-task access via Locked decorator** — legacy has two
   unsynchronized readers on the same I2C device (main loop + web server).
