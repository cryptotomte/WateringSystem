# Data Model: BME280 Environmental Sensor over I2C

## Environmental reading

One atomic snapshot per successful `read()`; on failure the previous good values
remain in the getters (validity contract, spec FR-001/FR-007).

| Quantity    | Unit | Type  | Derivation                                        |
|-------------|------|-------|---------------------------------------------------|
| Temperature | °C   | float | Bosch compensation (int32 path) ÷ 100             |
| Humidity    | %RH  | float | Bosch compensation (int32 Q22.10 path) ÷ 1024     |
| Pressure    | hPa  | float | Bosch compensation (int64 Q24.8 path) ÷ 256 ÷ 100 (parity: legacy converts Pa → hPa) |

No range validation beyond the NaN check (parity: legacy validates nothing else).
A compensation result that is NaN fails the whole read (legacy error 2).

## Error codes (`getLastError()`)

Same philosophy as the soil sensor's table: small, parity-anchored, distinct where
consumers must distinguish (SC-006: console separates "absent" from "read failed").

| Code | Meaning | Produced when | Parity |
|------|---------|---------------|--------|
| 0 | OK | last operation succeeded | legacy 0 |
| 1 | Sensor not found | probe of 0x76 and 0x77 finds no device ACK, or a responding device fails the chip-identity check (logged distinctly) | legacy 1 ("sensor not found") |
| 2 | Read failed | bus/communication error during a data read, or compensation yields NaN | legacy 2 ("read failed / NaN") |

`isAvailable()` never modifies the error code (soil-sensor convention).

## BME280 register map (used subset)

| Register | Name | Use |
|----------|------|-----|
| 0xD0 | chip_id | identity check — must read **0x60** (BME280) |
| 0x88–0xA1 | calib00–25 | calibration: dig_T1–T3, dig_P1–P9, dig_H1 |
| 0xE1–0xE7 | calib26–32 | calibration: dig_H2–H6 |
| 0xF2 | ctrl_hum | humidity oversampling — write **before** ctrl_meas (datasheet: takes effect on ctrl_meas write) |
| 0xF4 | ctrl_meas | T/P oversampling + mode |
| 0xF5 | config | standby + IIR filter |
| 0xF7–0xFE | press/temp/hum data | burst-read all 8 bytes in one transaction (atomic snapshot within the chip's shadowing rules) |

I2C addresses probed: **0x76, then 0x77** (bench rig module is at 0x77; greenhouse
unit hard-codes 0x77). Recovery after loss re-probes both (spec FR-004).

## Sampling profile (parity, spec FR-006)

Exact legacy configuration (`src/sensors/BME280Sensor.cpp:41-46`,
`docs/parity-checklist.md` §5):

| Register | Field values | Byte |
|----------|-------------|------|
| ctrl_hum (0xF2) | osrs_h = ×1 (001) | 0x01 |
| ctrl_meas (0xF4) | osrs_t = ×2 (010), osrs_p = ×16 (101), mode = NORMAL (11) | 0x57 |
| config (0xF5) | t_sb = 500 ms (100), filter = ×16 (100), spi3w = 0 | 0x90 |

Write order at init: chip-ID check → calibration readout → ctrl_hum → ctrl_meas →
config. (The implementer verifies the byte encodings against the datasheet tables;
the field values above are the binding contract.)

## Calibration data (read once at init)

Bosch trimming parameters, parsed little-endian from calib00–25 + calib26–32:

- `dig_T1` (u16), `dig_T2`, `dig_T3` (s16)
- `dig_P1` (u16), `dig_P2`–`dig_P9` (s16)
- `dig_H1` (u8), `dig_H2` (s16), `dig_H3` (u8), `dig_H4` (s12, split-nibble),
  `dig_H5` (s12, split-nibble), `dig_H6` (s8)
- `t_fine` (s32) — temperature fine resolution, shared input to P and H
  compensation; temperature MUST be compensated first each cycle.

Re-read on every (re-)initialization — a re-attached module may be a different
physical sensor.

## Driver state

```
UNINITIALIZED ──initialize()/lazy──▶ probe 0x76/0x77 ──ACK+chip-id 0x60──▶ INITIALIZED
      ▲                                    │ fail: error 1                     │
      └──────────── read()/isAvailable() detects loss (bus error) ◀───────────┘
```

- Lazy (re-)initialization: `read()` and `isAvailable()` attempt `initialize()`
  when uninitialized (parity, `docs/parity-checklist.md` §5).
- A bus error during read sets error 2 and marks the driver uninitialized so the
  next poll re-probes both addresses (spec FR-004 recovery; covers the
  swapped-address replug edge case).
- `isAvailable()` on an initialized sensor performs a real chip-ID read
  (deliberate divergence from legacy cached availability — spec FR-009).

## Concurrency

`Bme280Sensor` is unsynchronized by design (soil-sensor convention). Cross-task
access (sensor task + console REPL now; web PR-09, controller PR-11) goes through
`LockedEnvironmentalSensor`, which serializes the interface per call. Bus-level
safety across devices (INA226, PR-05) is provided by the i2c_master driver's
per-transaction bus lock (research R3) — the decorator exists for reading-snapshot
consistency, not bus safety.
