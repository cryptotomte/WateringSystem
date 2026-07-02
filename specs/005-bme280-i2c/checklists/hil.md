# HIL Checklist: BME280 Environmental Sensor (005) — rev1 bench rig

**Purpose**: hardware-in-the-loop verification of PR-03 at Checkpoint 3 (Paul, bench rig)
**Rig**: ESP32 devkit + BME280 breakout on I2C SDA=GPIO21 SCL=GPIO22 (rig module strapped to 0x77), Arduino unit nearby as reading reference
**Build**: rev1 target (`sdkconfig.board.rev1_devkit`), flash per `firmware/CLAUDE.md`
**Reference**: acceptance criteria `docs/prd/PR-03-bme280-i2c.md`; spec SC-003/004/005/006
**Note**: section C doubles as an implicit regression check of the soil sensor sharing the rig — the RS485 subsystem must keep answering while the I2C sensor is unplugged (subsystem isolation).

## A. Periodic readings & Arduino agreement (US1, SC-003)

- [ ] A1. Boot with the BME280 connected; log shows
      `temperature=… C humidity=… %RH pressure=… hPa` every 5 s (steady
      cadence, first reading within ~10 s of boot)
- [ ] A2. Values are plausible for the environment (room: ~15–30 °C,
      ~20–70 %RH, ~950–1050 hPa)
- [ ] A3. Compare against the Arduino unit in the same spot: agreement
      within ±0.5 °C, ±3 %RH, ±1 hPa (identical parity sampling profile —
      smoothness should also look alike, no extra jitter)
- [ ] A4. Let it run ≥10 min: cadence stays 5 s, no drift, no watchdog
      resets, heap stable (no reboot lines in the log)

## B. Console `env` command (US1/SC-006)

- [ ] B1. `env` with the sensor connected → one immediate reading:
      `OK temperature=… C humidity=… %RH pressure=… hPa` (documented
      syntax works on the first attempt)
- [ ] B2. `env` with the sensor UNPLUGGED (after B4/C1 below or a
      sensorless boot) → `ERROR 1 (sensor not found)` — distinguishable
      from a read failure
- [ ] B3. If a mid-transaction failure can be provoked (wiggle SDA during
      a poll): `ERROR 2 (read failed)` appears for the failed read — the
      absent-vs-failed distinction is visible on the console (host tests
      cover both codes deterministically; this bench item is
      best-effort)
- [ ] B4. `env` immediately after replugging → OK reading again, no
      restart needed

## C. Unplug/replug & sensorless boot (US2, SC-004)

- [ ] C1. Unplug the BME280 mid-run → next poll logs ONE
      `environmental reading invalid (error …)` WARN; system keeps
      running, no crash, no reboot
- [ ] C2. Leave it unplugged ≥2 min → repeat WARN appears at the bounded
      cadence (~once a minute, every 12th failure), NOT every 5 s (no log
      flood)
- [ ] C3. While unplugged: `soil` still answers normally (RS485 unaffected
      by the I2C outage — implicit soil-sensor regression check)
- [ ] C4. Replug → within a poll or two: `environmental sensor recovered`
      WARN followed by normal 5 s readings, no manual intervention
- [ ] C5. Boot WITHOUT the sensor → normal startup (console up, pumps OFF),
      one invalid-reading WARN, no boot block; attach the sensor →
      readings start on a subsequent poll (lazy re-init)
- [ ] C6. During C1/C2 (unplugged): the WARNs come from the driver/task
      only (`bme280`, `sensor_task` tags) — no per-poll flood of
      `esp_i2c_bus` WARNs (unplug-NACKs classify at debug level; only
      timeouts/unexpected bus errors WARN — not host-testable, the bench
      pins this classification)

## D. Address variant 0x76 (US3, SC-005)

- [ ] D1. If a 0x76-strapped module (or a solderable ADDR pad) is on hand:
      swap modules while unpowered, boot → sensor found at 0x76 with no
      config/code change, readings flow. If NO 0x76 hardware is available:
      mark host-covered — probe order, wrong-chip-ID rejection and
      swapped-address recovery are all verified deterministically in
      `test_bme280.cpp` (SC-005 allows this)

## E. Regression guard

- [ ] E1. Pumps still OFF at boot (watch outputs during A1/C5 boots) —
      safety invariant untouched by this PR
- [ ] E2. Existing console commands still respond normally: `pump status`,
      `soil`, `rs485test`, `config get`, `storage stats`

**Sign-off**: date + result per item as PR comment (pattern from PR #7).
