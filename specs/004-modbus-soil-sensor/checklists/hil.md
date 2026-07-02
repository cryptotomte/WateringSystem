# HIL Checklist: Modbus Soil Sensor (004) — rev1 bench rig

**Purpose**: hardware-in-the-loop verification of PR-04 at Checkpoint 3 (Paul, bench rig)
**Rig**: ESP32 devkit + RS485 5 Click (ADM3485, manual DE) + NPK soil sensor, 9600 8N1, TX=GPIO16 RX=GPIO17 DE=GPIO25
**Build**: rev1 target (`sdkconfig.board.rev1_devkit`), flash per `firmware/CLAUDE.md`
**Reference**: acceptance criteria `docs/prd/PR-04-modbus-soil-sensor.md`; spec SC-002/003/005

## A. Basic readings (US1, SC-002)

- [ ] A1. Boot with sensor connected; console `soil` → all 7 values printed
      (moisture %, temperature °C, EC µS/cm, pH, N/P/K mg/kg), no error
- [ ] A2. Values match the Arduino unit's readings for the same probe/soil
      (within one scaling step, i.e. ±0.1 for moisture/temp/pH, ±1 for EC/NPK)
- [ ] A3. Temperature sign check: chill the probe tip (or verify a plausible
      positive value and confirm sign handling was host-tested) — reading is
      signed-correct, no 6553.x-style wraparound
- [ ] A4. `rs485test` → success outcome, success counter increments across
      repeated runs

## B. Fault injection & recovery (US2, SC-003)

- [ ] B1. Disconnect the RS485 A/B pair; `soil` → error after ~3 s (timeout,
      error 3), no crash, no watchdog reset, error logged on console
- [ ] B2. Repeat `soil` twice while disconnected → same failure each time,
      error counter increments each attempt (no retry storms, no lockup)
- [ ] B3. Reconnect A/B; next `soil` → succeeds without reboot or manual
      reinit (SC-003 automatic recovery)
- [ ] B4. Power-cycle the SENSOR only (if rig allows): `soil` fails while
      sensor boots, recovers on a later read

## C. RS485 frame integrity (plan risk 1 — hardware RTS vs TXS0108E margins)

- [ ] C1. With scope or logic analyzer on DE (GPIO 25) and A/B (optional but
      recommended, parity §5 open HIL item): DE asserts before the first start
      bit and releases after the last stop bit; request frames are untruncated
      at 9600 baud. If truncation appears: STOP, report — fallback is manual
      GPIO DE control (research R2), goes back through fixer
- [ ] C2. 20× `soil` in a row → 20 successes (no intermittent framing losses;
      check counters via `rs485test`)

## D. Calibration commands (US4/FR-012, optional bench check)

- [ ] D1. `soil_cal_ph <reference>` with a known buffer (or plausible dummy
      reference) → `OK calibration applied` (or the documented non-fatal
      write-failure variant); subsequent `soil` reflects the factored pH
- [ ] D2. `soil_cal_moisture <reference>` → OK, but subsequent `soil` moisture
      UNCHANGED (parity: moisture factor stored, never applied on read —
      expected behavior, not a bug)

## E. Regression guard

- [ ] E1. Pumps still OFF at boot (watch outputs during A1 boot) — safety
      invariant untouched by this PR
- [ ] E2. Existing console commands (pump/storage) still respond normally

## Deferred to PR-14 (rev2 bring-up — NOT this checklist)

- rev2 THVD1426 echo suppression at 9600 baud (FW-4, research R3)
- rev2 RX pull-up effect with SENS_PWR_EN off (FW-2)
- rev2 auto-direction frame integrity

**Sign-off**: date + result per item as PR comment (pattern from PR #7).
