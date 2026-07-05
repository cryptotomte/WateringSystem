# Quickstart / Validation Guide: Watering Controller

CI-tagged items gate the merge (this PR's deliverable is the host-tested logic). HIL-tagged items are a
smoke on the rev1 rig at Checkpoint 3.

## Prerequisites

- ESP-IDF v6.0.1 via Docker; rsync to `/tmp` first (Docker can't mount OneDrive):
  `rsync -a --delete --exclude build --exclude managed_components --exclude sdkconfig "$PWD/firmware/" /tmp/ws011-firmware/`
- Between board builds: `idf.py fullclean` + `rm -f sdkconfig`. If the host build dir is stale from a
  failed run: `rm -rf test_apps/host/build` before `set-target linux`.

## CI validation (host tests — the core deliverable + both board builds)

### Host tests — `run_watering_controller_tests()` + `run_reservoir_tests()` (100 % of decision/safety branches, SC-001)
Drive the pure controllers over the mocks + `FakeTimeProvider`/`FakeWallClock`:
- **Automatic**: start at ≤ low; no start during soak; restart after soak while still dry; stop at ≥ high;
  gate-on-read (no action before first successful read).
- **Fail-safe**: soil unavailable / stale (>30 000 ms / never) / moisture out-of-range each stops the pump
  and blocks watering in automatic; a pending soak pause does NOT delay a safety stop; manual bypasses.
- **Manual**: continues despite sensor failure; capped at 300 s; `stop()` clears the override; auto-started
  runs are automatic (fail-safe applies).
- **Reservoir**: all 5 truth-table rows (incl. invalid-sensor + implausible → no action); start on low-dry;
  stop on high-wet; max-fill abort at 300 s; feature disabled forces off.
- **Logging**: data-log cadence + epoch timestamp + NPK≥0; time-not-set handled (no bogus 1970 as valid).
- **Config**: setter validation; a runtime config change is picked up on the next tick.

```bash
docker run --rm -v /tmp/ws011-firmware:/project -w /project espressif/idf:v6.0.1 \
  bash -lc "cd test_apps/host && idf.py --preview set-target linux && idf.py build && ./build/pump_host_tests.elf"
# exit code == Unity failures; must be 0
```

### Both board targets build (controller integrated)
```bash
for b in rev1_devkit rev2; do
  docker run --rm -v /tmp/ws011-firmware:/project -w /project espressif/idf:v6.0.1 \
    bash -lc "idf.py fullclean >/dev/null 2>&1; rm -f sdkconfig; \
      idf.py -DSDKCONFIG_DEFAULTS='sdkconfig.defaults;sdkconfig.board.$b' build"
done
```
- **[CI]** both build; `idf.py size` fits the 1.5 MiB OTA slot; `dependencies.lock` unchanged. rev1 wires
  the reservoir pump; rev2 does not (level sensors status-only) — both compile the pure reservoir logic.

## HIL validation (rev1 rig — Checkpoint 3 smoke)

1. **Automatic waters**: set moisture below the low threshold (or a dry sensor) in automatic mode → the
   plant pump runs a burst, pauses (soak), re-waters until high.
2. **Fail-safe on cable pull**: in automatic mode with the pump running, pull the RS485 cable → the pump
   stops within the staleness window (~30 s).
3. **Manual override**: a manual run keeps going despite a sensor failure; caps at 300 s.
4. **Reservoir**: fill starts on low, stops on high, aborts at 300 s if the high sensor never trips.
5. **Live soil**: `/api/v1/sensors` now shows soil `valid:true` with fresh values (periodic reader);
   `/status` mode follows the flag and drives the controller.
6. **Isolation**: pull WiFi during an active watering cycle → watering is unaffected.

## Definition of done

- Host suite green (0 failures), every controller decision/safety branch covered (branch checklist in the
  PR per the acceptance criteria); both board targets build + fit the slot.
- `firmware/CLAUDE.md` gains a Watering Controller section.
- HIL smoke executed on the rig, or deferred with rationale if no rig time (deferred-HIL register).
