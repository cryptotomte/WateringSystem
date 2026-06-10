# PR-03: bme280-i2c

> Phase 1 — drivers

## Goal

Port the BME280 environmental sensor to the new ESP-IDF `i2c_master` API behind the
existing `ISensor`/`IEnvironmentalSensor` interfaces, replacing the Adafruit library.

## Scope

- `ISensor` / `IEnvironmentalSensor` interfaces ported to pure C++ (temperature,
  humidity, pressure; validity/availability reporting), host-includable.
- `Bme280Sensor` component using the **new `i2c_master` driver** (not the legacy
  `driver/i2c.h`):
  - I2C bus on board-component pins (rev1: SDA 21, SCL 22), 100 kHz standard mode.
  - Address probing 0x76 / 0x77 (module variants differ).
  - Chip-ID check, calibration data readout, compensation math per Bosch datasheet
    (or the `espressif/bme280`-style registry component if it pins cleanly — decide
    in plan phase; no Arduino dependencies either way).
  - Sampling configuration is an **explicit, documented choice** — the current
    Arduino behavior is *not* library defaults: NORMAL mode, oversampling T×2 /
    P×16 / H×1, IIR filter ×16, standby 500 ms (`src/sensors/BME280Sensor.cpp:41-46`,
    `docs/parity-checklist.md` §5). Either match those settings (parity) or switch
    to forced-mode reads suited to the 5-second sensor task cadence — if so, mark
    it as a deliberate change (note: forced mode without the IIR filter changes
    reading smoothness vs the Arduino unit).
- Error handling: sensor-absent and read-failure paths return invalid data without
  crashing; recovery on next poll (graceful degradation parity).
- Sensor task integration: dedicated FreeRTOS task polling at 5 s, mutex-protected
  shared readings (pattern from Arduino firmware, now with IDF primitives).
- Mock `IEnvironmentalSensor` for host tests.

## Out of scope

- Soil sensor (PR-04), web exposure of readings (PR-09), data logging to littlefs
  (PR-06/PR-09), INA226 (shares the I2C bus later, PR-05 reuses this bus handle).

## Functional requirements covered

- FR3 (BME280 via I2C, new i2c_master API).

## Dependencies

- PR-02 (board component pin map, interface placement conventions, sensor task home).

## Acceptance criteria

- [CI] Both board targets build with the component enabled.
- [CI] Host test: compensation math against Bosch datasheet reference vectors;
  invalid-data path when mock bus errors.
- [HIL] Rig prints plausible temperature/humidity/pressure every 5 s; values agree
  with the Arduino unit within sensor tolerance (±0.5 °C, ±3 %RH, ±1 hPa).
- [HIL] Unplugging the BME280 yields invalid readings and a logged warning, no reboot;
  replugging recovers without restart.

## Estimated size

M
