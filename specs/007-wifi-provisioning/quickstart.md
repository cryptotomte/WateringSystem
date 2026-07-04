# Quickstart / Validation Guide: WiFi Provisioning & Station Management

How to prove PR-07 works. CI-tagged items gate the merge; HIL-tagged items run on the rev1 devkit rig at
Checkpoint 3.

## Prerequisites

- ESP-IDF v6.0.1 via Docker. **Docker cannot mount the OneDrive tree** — rsync the firmware to `/tmp`
  first:
  ```bash
  rsync -a --delete --exclude build --exclude managed_components --exclude sdkconfig \
    "$PWD/firmware/" /tmp/ws007-firmware/
  ```
- On board switch: `idf.py fullclean` + `rm -f sdkconfig`.

## CI validation (host tests + both target builds)

### Host tests (`test_apps/host`, IDF linux preview target)

Run the Unity suite (exit code = failure count). The new `run_wifi_tests()` must cover:

- **Reconnect schedule** (contract `wifi-manager-states.md`): retry only at 10 s; pause 60 s after 5
  failures; monitor at 5 s; no attempts during pause; no boot loop under infinite failure.
- **Boot-mode truth table**: all four `decideBootMode` rows.
- **Credential validation**: SSID 1–32 accept / empty + >32 reject; password empty accept, 1–7 reject,
  ≥8 accept, >64 reject.
- **Connect happy path**: `Connected` after `GotIp`, failures reset to 0.
- **Isolation (FR-014)**: `tick()` never blocks on a hung/silent driver; manager has no watering
  dependency.

```bash
# from the rsync'd copy
docker run --rm -v /tmp/ws007-firmware:/project -w /project espressif/idf:v6.0.1 \
  bash -lc "idf.py --preview set-target linux && idf.py -C test_apps/host build && \
            ./test_apps/host/build/*.elf"
```

### Both board targets build

```bash
for b in BOARD_REV1_DEVKIT BOARD_REV2; do
  docker run --rm -v /tmp/ws007-firmware:/project -w /project espressif/idf:v6.0.1 \
    bash -lc "idf.py fullclean; rm -f sdkconfig; \
              idf.py -DSDKCONFIG_DEFAULTS='' set-target esp32 && idf.py build"   # + select $b via Kconfig
done
```

- **Acceptance [CI]**: both targets build; `idf.py size` confirms the portal fits the 1.5 MiB slot
  (research R2); `dependencies.lock` and the two `esp-modbus` pins are unchanged.
- **Acceptance [CI]**: the FR9 SoftAP-portal decision is documented in this spec directory and referenced
  from `firmware/CLAUDE.md`.

## HIL validation (rev1 devkit rig — Checkpoint 3)

From `docs/prd/PR-07-wifi-provisioning.md` acceptance criteria + parity §7:

1. **Fresh device provisioning**: flash with empty credentials → device boots into AP mode, SSID visible,
   setup page reachable at 192.168.4.1 (WPA2, using `CONFIG_WS_PROV_AP_PASSWORD`). Submit real
   credentials → success page → device restarts (~3 s) → joins the LAN.
2. **AP power-cycle / outage isolation**: with the device connected (and a watering-relevant task
   running), cut the AP → confirm sensor polling / pump-control cadence continue unaffected; restore AP →
   device reconnects on the 10 s/60 s schedule.
3. **Config-button-at-boot**: on an already-configured device, hold `BOARD_PIN_BTN_CONFIG` (GPIO18) at
   boot → credentials cleared → device enters provisioning mode.
4. **Wrong-password**: provision a wrong home-WiFi password → device keeps retrying with the schedule,
   stays provisionable, **no boot loop**.
5. **Console/portal equivalence**: credentials set via the portal and via diag console
   `config wifi <ssid> <pass>` produce the same `wifi=configured` state (research D10).
6. **LED (parity §7/§9)**: connect-attempt toggle ~500 ms; config-button-hold blink ~100 ms.
7. **Brownout watch (QUIRK 4)**: no spike-induced resets during the AP power-cycle test with brownout
   detection left enabled.

## Definition of done

- All host-test items above pass in CI (green, 0 failures) and both board targets build.
- HIL checklist executed by Paul on the rig (or explicitly deferred with rationale if rig/hardware
  unavailable, mirroring the deferred-HIL register in the handover).
- `firmware/CLAUDE.md` references the FR9 decision recorded here.
