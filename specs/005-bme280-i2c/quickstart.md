# Quickstart Validation: BME280 Environmental Sensor over I2C

How to prove the feature works, end to end. Prerequisites: docker (pinned image
`espressif/idf:v6.0.1`), repo checkout of branch `005-bme280-i2c`.

## 1. Host tests (CI gate — compensation math + error paths)

```bash
cd firmware/test_apps/host
docker run --rm -v "$PWD":/project -w /project espressif/idf:v6.0.1 bash -c \
  "idf.py --preview set-target linux && idf.py build && ./build/pump_host_tests.elf"
```

Expected: exit code 0 (= zero Unity failures); the run includes the
`test_bme280.cpp` suite — Bosch reference-vector compensation checks (incl.
negative temperature), calibration parsing, probe/chip-ID logic, absent-sensor and
mid-read-loss error paths, recovery re-probe, mock-consistency checks.

## 2. Both board targets build green (CI gate)

```bash
cd firmware
docker run --rm -v "$PWD":/project -w /project espressif/idf:v6.0.1 \
  idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.board.rev1_devkit" build
docker run --rm -v "$PWD":/project -w /project espressif/idf:v6.0.1 bash -c \
  "idf.py fullclean && idf.py -DSDKCONFIG_DEFAULTS='sdkconfig.defaults;sdkconfig.board.rev2' build"
```

Expected: both builds succeed from clean checkout (SC-001). `dependencies.lock`
unchanged (no new managed dependencies in this feature).

## 3. HIL on the rev1 bench rig (Checkpoint 3, Paul)

Full checklist: `checklists/hil.md` (created at implementation). Outline:

1. **Periodic readings** — flash rig with BME280 attached, open monitor: a
   temperature/humidity/pressure log line every 5 s with plausible values
   (SC-003).
2. **Arduino agreement** — compare against the Arduino unit in the same
   environment: within ±0.5 °C, ±3 %RH, ±1 hPa (SC-003; parity sampling profile
   makes this like-for-like).
3. **Console** — `ws>` prompt: `env` returns the three values; output
   distinguishes sensor-absent from read-failed (SC-006).
4. **Unplug/replug** — unplug the module while running: invalid readings + logged
   warning, no reboot; replug: automatic recovery (SC-004).
5. **Boot without sensor** — power up with the module disconnected: normal boot,
   invalid readings; attach module: readings start without restart (SC-004).
6. **Address variants** — if a 0x76-strapped module is on hand, swap it in: works
   with no config change (SC-005; otherwise 0x77 verified + host-level probe-order
   tests count).
7. **Regression guard** — pump console commands still work; pumps OFF at boot
   (safety invariant untouched).

## References

- Interface contracts: [contracts/interfaces.md](contracts/interfaces.md)
- Registers, sampling profile, error codes: [data-model.md](data-model.md)
- Design decisions: [research.md](research.md)
