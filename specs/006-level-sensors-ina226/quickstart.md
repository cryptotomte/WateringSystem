# Quickstart Validation: Level Sensors, Capability Flag and INA226

Prerequisites: docker (`espressif/idf:v6.0.1`), branch `006-level-sensors-ina226`
(implementation rebased on main AFTER PR #11 merged).

## 1. Host tests (CI gate)

```bash
cd firmware/test_apps/host
docker run --rm -v "$PWD/../..":/project -w /project/test_apps/host espressif/idf:v6.0.1 bash -c \
  "idf.py --preview set-target linux && idf.py build && ./build/pump_host_tests.elf"
```

Expected: exit 0; new suites cover debounce/settle/polarity/fail-direction
(`DebouncedLevelSensor` over scripted inputs + FakeTimeProvider), INA226 scaling
vectors + identity/absent/recovery paths (`Ina226Sensor` over MockI2cBus incl.
16-bit writes), mock coherence (MockLevelSensor four truth-table states), and all
pre-existing suites unchanged (SC-004 regression guard).

## 2. Both board targets build green (CI gate)

```bash
cd firmware
docker run --rm -v "$PWD":/project -w /project espressif/idf:v6.0.1 \
  idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.board.rev1_devkit" build
docker run --rm -v "$PWD":/project -w /project espressif/idf:v6.0.1 bash -c \
  "idf.py fullclean && rm -f sdkconfig && idf.py -DSDKCONFIG_DEFAULTS='sdkconfig.defaults;sdkconfig.board.rev2' build"
```

Expected: both green; rev2 binary has no reservoir pump (compile-time), rev1 no
INA226 wiring (SC-001). `dependencies.lock` unchanged (no new managed deps).

## 3. HIL on the rev1 bench rig (checklist created at implementation: checklists/hil.md)

1. **Level status** — `level` shows both sensors; trigger low/high sensors wet/dry
   → correct logical states (SC-003); record measured polarity in
   `docs/parity-checklist.md` line 96 (FR5 bench task).
2. **Debounce** — hand-simulate chatter at a mark → exactly one reported
   transition (SC-005).
3. **Fail direction** — disconnect a sensor → reads "water present" (rev1
   direction), logged/documented (checklist line 97).
4. **Pump regression** — `pump reservoir start/stop/status` unchanged; pumps OFF
   at boot; `env`/`soil` commands still work (shared main-loop/console surface).
5. **INA226** — not HIL-testable on rev1 (no device); host-covered; hardware
   validation deferred to PR-14 by the mini-PRD.

## References

- Contracts: [contracts/interfaces.md](contracts/interfaces.md)
- Board flags, registers, scaling: [data-model.md](data-model.md)
- Decisions: [research.md](research.md)
