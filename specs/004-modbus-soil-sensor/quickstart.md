# Quickstart Validation: Modbus Soil Sensor (004)

Prerequisites: docker (espressif/idf:v6.0.1). OneDrive tree cannot be mounted by
Docker Desktop — rsync to /tmp first (PR-06 lesson):

```bash
rsync -a --delete --exclude build --exclude managed_components --exclude sdkconfig \
  "$PWD/firmware/" /tmp/ws004-firmware/
```

## 1. Host tests (CI-equivalent) — [CI] acceptance criteria

```bash
docker run --rm -v /tmp/ws004-firmware:/project -w /project/test_apps/host \
  espressif/idf:v6.0.1 bash -c "idf.py --preview set-target linux && idf.py build \
  && ./build/host_tests.elf"
```

Expected: exit code 0; suite includes soil-sensor cases: decode of a known
9-register payload (incl. negative temperature), range-validation failures
(moisture/temp/pH), invalid-on-timeout via MockModbusClient, exception-code
mapping, calibration factor computation + best-effort write, statistics counters.

## 2. Both board targets build — [CI] acceptance criterion

```bash
# rev1 (repeat with BOARD_REV2 for rev2; fullclean + rm sdkconfig between boards)
docker run --rm -v /tmp/ws004-firmware:/project -w /project espressif/idf:v6.0.1 \
  bash -c "idf.py fullclean; rm -f sdkconfig; \
  echo CONFIG_BOARD_REV1_DEVKIT=y >> sdkconfig.defaults.local && idf.py build"
```

Expected: both builds green; rev2 build contains no `BOARD_PIN_RS485_DE` reference
(compile-time guaranteed).

## 3. HIL on the rev1 bench rig — Checkpoint 3 checklist (Paul)

Rig: devkit + RS485 5 Click + soil sensor at 9600 8N1 (TX 16 / RX 17 / DE 25).

| # | Step | Expected |
|---|---|---|
| 1 | Flash, open console, run `soil` | All 7 values printed; plausible vs Arduino unit on same probe (SC-002) |
| 2 | Compare each value with the Arduino unit | Match within one scaling step; temperature sign correct |
| 3 | `rs485test` | Success outcome, counters increment |
| 4 | Disconnect A/B, run `soil` | Error after ~3 s (timeout), flagged invalid, logged; no crash/watchdog (SC-003) |
| 5 | Reconnect A/B, run `soil` | Next read succeeds, no reboot needed |
| 6 | `soil_cal_ph <ref>` with known buffer (optional) | Factor updated; write result reported; subsequent `soil` reflects factor |
| 7 | Scope/logic-analyzer on DE (optional) | Frames untruncated at 9600 baud (R2 risk check, parity §5 HIL item) |

rev2-specific items (echo suppression, RX pull-up effect with SENS_PWR_EN off) are
validated at PR-14 bring-up per spec assumption.
