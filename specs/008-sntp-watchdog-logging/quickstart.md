# Quickstart / Validation Guide: SNTP Time, Task Watchdog & Event Logging

CI-tagged items gate the merge; HIL-tagged items run on the rev1 rig at Checkpoint 3.

## Prerequisites

- ESP-IDF v6.0.1 via Docker. **Docker cannot mount OneDrive** — rsync first:
  ```bash
  rsync -a --delete --exclude build --exclude managed_components --exclude sdkconfig \
    "$PWD/firmware/" /tmp/ws008-firmware/
  ```
- On board switch / to avoid stale sdkconfig: `idf.py fullclean` + `rm -f sdkconfig` before each target
  build (a leftover sdkconfig from the other board is NOT overridden by `-DSDKCONFIG_DEFAULTS`).

## CI validation (host tests + both board builds)

### Host tests — `run_time_tests()` + `run_event_logger_tests()` must cover

- **DST conversion**: a winter epoch → CET (+01:00), a summer epoch → CEST (+02:00); the last-Sunday-of-
  March/October boundaries flip correctly (`setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3")` on the host).
- **Time-not-set**: `isPlausibleEpoch(0)` false, a 2020+ epoch true.
- **EventLogger**: each producer (`logReset/logWifi/logPumpStart/logPumpStop`) writes one event with the
  expected category + detail (vs `MockDataStorage` + `FakeWallClock`); write-failure path increments the
  dropped counter without crashing; reset-reason mapping total.
- **Event-log rotation/content**: reuse the `LittleFsDataStorage` tempdir pattern (as `test_data_storage`)
  to confirm events persist + rotate + read newest-first.

```bash
docker run --rm -v /tmp/ws008-firmware:/project -w /project espressif/idf:v6.0.1 \
  bash -lc "cd test_apps/host && idf.py --preview set-target linux && idf.py build && ./build/pump_host_tests.elf"
# exit code == Unity failure count
```

### Both board targets build

```bash
for b in rev1_devkit rev2; do
  docker run --rm -v /tmp/ws008-firmware:/project -w /project espressif/idf:v6.0.1 \
    bash -lc "idf.py fullclean >/dev/null 2>&1; rm -f sdkconfig; \
      idf.py -DSDKCONFIG_DEFAULTS='sdkconfig.defaults;sdkconfig.board.$b' build && \
      grep -qx 'CONFIG_BOARD_${b^^}=y' sdkconfig || grep -q CONFIG_BOARD sdkconfig"
done
```

- **[CI]** both targets build; `idf.py size` confirms the app still fits the 1.5 MiB OTA slot;
  `dependencies.lock` + both `esp-modbus` pins unchanged (only IDF built-ins added).

## HIL validation (rev1 rig — Checkpoint 3)

1. **Correct Swedish time + sync status**: boot networked → console `time` first shows "not set", then
   correct CET/CEST local time after sync; sync status reports synced + last-sync.
2. **Watchdog reboot + pumps safe**: starve a subscribed critical task (debug hook/build flag) → device
   reboots within the timeout → pumps measured OFF immediately after reset → `storage events` shows
   `reset=TASK_WDT`.
3. **No spurious reboot**: extended normal run triggers no watchdog reset.
4. **Event log persists across power-cycle**: generate pump start/stop (console `pump ...`) + a WiFi drop →
   power-cycle → `storage events` still lists them with timestamps + causes.
5. **WiFi outage does not reboot**: pull WiFi during operation → device keeps running, no watchdog reset,
   watering unaffected; a `wifi=Reconnecting`/`wifi=Connected` event pair is logged.
6. **Non-fatal SNTP**: block NTP (no internet) → device runs in "time not set" indefinitely, no crash.

## Definition of done

- Host suite green (0 failures); both board targets build; app fits the OTA slot.
- HIL checklist (`specs/008-sntp-watchdog-logging/checklists/hil.md`) executed on the rig, or explicitly
  deferred with rationale if no rig time (deferred-HIL register).
- `firmware/CLAUDE.md` gains a short SNTP/watchdog/event-log section.
