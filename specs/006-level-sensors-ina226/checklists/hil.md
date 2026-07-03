# HIL Checklist: Level Sensors, Capability Flag and INA226 (006) — rev1 bench rig

**Purpose**: hardware-in-the-loop verification of PR-05 at Checkpoint 3 (Paul, bench rig)
**Rig**: ESP32 devkit + two XKC-Y26 level sensors on GPIO32 (low mark) / GPIO33 (high mark), internal pull-ups; pumps, BME280 and RS485 soil sensor as wired for the previous features
**Build**: rev1 target (`sdkconfig.board.rev1_devkit`), flash per `firmware/CLAUDE.md`
**Reference**: acceptance criteria `docs/prd/PR-05-level-sensors-ina226.md`; spec SC-003/SC-004/SC-005; quickstart.md §3
**Note**: the XKC-Y26 can be hand-triggered (palm/water glass against the sensing face) — no filled reservoir needed. Remember the 300 ms debounce: state changes lag the physical event by ~0.3 s by design.

## A. Level status wet/dry, both marks (US1, SC-003)

- [ ] A1. Boot; within ~1 s of the first main-loop passes `level` answers
      `OK low=… (raw=…) high=… (raw=…)` — no `not_yet_valid` lingers on
      rev1 (settle 0 ms, only the 300 ms warm-up)
- [ ] A2. Both sensors dry → `level` shows `low=dry high=dry`
- [ ] A3. Trigger the LOW sensor (wet) → after ~0.3 s `level` shows
      `low=water high=dry`; raw for the low sensor reads 1 (active HIGH)
- [ ] A4. Trigger BOTH sensors → `low=water high=water`
- [ ] A5. Trigger only the HIGH sensor → `low=dry high=water` — the
      physically invalid combination is REPORTED as-is, never masked
      (interpreting it is PR-11's job)
- [ ] A6. **Record the measured polarity in `docs/parity-checklist.md`
      line 96** (the marked REV1 BENCH MEASUREMENT RECORD block): GPIO
      level with water present, GPIO level without, and whether `level`
      matched — measured values only (FR5 bench task, SC-003; this closes
      the phase-1 polarity verification for rev1)

## B. Debounce: chatter → single transition (US1, SC-005)

- [ ] B1. From a stable dry state, hand-simulate slosh at the low mark
      (rapidly waggle the trigger against the sensing face for ~1 s), then
      hold it wet → repeated `level` polls show NO oscillation: `dry`
      throughout the chatter, then exactly ONE transition to `water`
- [ ] B2. Same in the other direction (wet → chatter → dry): one clean
      transition, no flapping
- [ ] B3. A short dip (<0.3 s) that returns to the stable state never
      surfaces in `level` output (window restart behavior; host-tested
      deterministically — this bench item is a spot check)

## C. Fail direction: disconnect (US4, checklist line 97)

- [ ] C1. Disconnect the LOW sensor signal wire mid-run → after ~0.3 s
      `level` reads `low=water` (pull-up ⇒ HIGH ⇒ rev1 active HIGH =
      "water present" — the fill-pump-stays-off direction); no crash, no
      flapping
- [ ] C2. Reconnect → the true state returns within one debounce window
- [ ] C3. Confirm the direction is logged/annotated in the parity
      checklist line 97 item (rev2's inverted direction is host-tested
      only — no rev2 hardware yet)

## D. Pump + console regression (US2, SC-004)

- [ ] D1. Pumps OFF at boot (watch outputs during the A1 boot) — safety
      invariant untouched by the capability-flag rework
- [ ] D2. `pump reservoir start 5` runs and self-stops; `pump reservoir
      stop` and `pump reservoir status` behave as before (rev1 keeps both
      pumps — no regression from the gating)
- [ ] D3. `pump status` lists BOTH pumps (plant + reservoir)
- [ ] D4. `env`, `soil`, `rs485test`, `config get`, `storage stats` still
      respond normally (shared main-loop/console surface unchanged)
- [ ] D5. Main loop keeps its 10 Hz cadence with the two level update()
      calls added: no watchdog resets, no missed pump self-stops during a
      timed run while `level` is polled repeatedly

## E. INA226 — host-covered, hardware at PR-14

- [ ] E1. Nothing to test on the rev1 rig (no INA226 device;
      `BOARD_HAS_INA226=0` — the `power` command does not exist in this
      build; verify `power` is an unknown command). Scaling, identity,
      absence and recovery paths are host-tested deterministically in
      `test_ina226.cpp`; on-hardware validation (incl. the written config
      value, `TODO(PR-14)` in `Ina226Sensor.cpp`) is deferred to PR-14 by
      the mini-PRD

**Sign-off**: date + result per item as PR comment (pattern from PR #7).
