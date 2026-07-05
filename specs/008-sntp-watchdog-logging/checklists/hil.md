# HIL Checklist: SNTP Time, Task Watchdog & Event Logging (008) — rev1 bench rig

**Purpose**: hardware-in-the-loop verification of PR-08 at Checkpoint 3 (Paul, bench rig)
**Rig**: ESP32 devkit + status LED on GPIO2, config button on GPIO18, pumps, BME280 and RS485 soil sensor as wired for the previous features. A WiFi AP with real internet access (so SNTP can reach `se.pool.ntp.org`) plus a phone/laptop, and a way to block internet / pull the AP.
**Build**: rev1 target (`sdkconfig.board.rev1_devkit`), flash per `firmware/CLAUDE.md`. The device must be provisioned with valid home-WiFi credentials (station mode) — see PR-07 §A if it is still unconfigured.
**Reference**: acceptance criteria `docs/prd/PR-08-*.md`; spec FR-008/FR-009/FR-014; `quickstart.md` §HIL; `contracts/task-watchdog.md`; parity `docs/parity-checklist.md`
**Watchdog note**: sections B and C need a subscribed watering-critical task to be starved on purpose — build with a debug hook / build flag that spins a `while(true){}` inside the 10 Hz main loop or the sensor task (never the WiFi task). Remove the hook before merge.

## A. Correct Swedish time + sync status (US1, quickstart §HIL #1)

- [ ] A1. Boot networked → console `time` immediately after boot reports
      "not set" (or an implausible 1970 epoch flagged as not-set) BEFORE the
      first sync
- [ ] A2. After the station reaches Connected and SNTP syncs (a few seconds),
      `time` shows the correct **local** Swedish time — CET (+01:00) in winter,
      CEST (+02:00) in summer (confirm the offset matches the current season)
- [ ] A3. `time` sync status reports **synced** with a plausible last-sync
      timestamp
- [ ] A4. Confirm SNTP is only started once WiFi is Connected (serial shows the
      SNTP start after the `wifi=Connected` event, not at boot)

## B. Watchdog reboot + pumps safe (US3, quickstart §HIL #2, FR-008/FR-009)

- [ ] B1. With the debug starve-hook armed on a subscribed critical task
      (main loop OR sensor task), trigger the stall → the device **reboots**
      within `CONFIG_WS_TASK_WDT_TIMEOUT_S` (default 20 s)
- [ ] B2. Measure the pump outputs immediately after the reset → **OFF**
      (pump fail-safe still runs first at boot, unchanged)
- [ ] B3. On the reboot, `storage events` lists a fresh `reset=TASK_WDT` entry
      with a timestamp (reset-reason event path)
- [ ] B4. Repeat starving the OTHER subscribed task (sensor task if B1 used the
      main loop, or vice versa) → same result: reboot + pumps OFF + `TASK_WDT`

## C. No spurious reboot (US3, quickstart §HIL #3)

- [ ] C1. Remove/disarm the starve-hook. Run the device normally for an
      extended period (≥ 30 min, sensor polling + a couple of pump timed runs) →
      **no watchdog reset** occurs (serial banner appears once; `storage events`
      shows no unexpected `reset=TASK_WDT`)

## D. Event log persists across power-cycle (US2, quickstart §HIL #4)

- [ ] D1. Generate events: `pump plant start 5` (start + self-stop), and drop
      the WiFi briefly (pull the AP, then restore) to log a
      `wifi=Reconnecting`/`wifi=Connected` pair
- [ ] D2. `storage events` lists the pump start/stop and WiFi events with
      timestamps + causes
- [ ] D3. **Power-cycle** the device (full power off/on, not a reset) → after
      reboot `storage events` STILL lists the pre-power-cycle events (persisted
      to littlefs, newest-first, survives the boot)

## E. WiFi outage does NOT reboot (US3, quickstart §HIL #5, FR-014)

- [ ] E1. With the device connected in station mode, start a watering activity
      (`pump plant start 30`) and let the 5 s environmental poll run
- [ ] E2. Pull the WiFi (power off the AP) for longer than the watchdog timeout
      → the device keeps running, the pump timed run + self-stop and the
      sensor cadence are UNAFFECTED, and **no watchdog reset** occurs (the WiFi
      task is not subscribed)
- [ ] E3. A `wifi=Reconnecting` (and later `wifi=Connected` on restore) event
      pair is logged

## F. Non-fatal SNTP offline (US1, quickstart §HIL #6)

- [ ] F1. Block NTP (connect to a WiFi network with no internet, or firewall
      the NTP pool) → the device runs indefinitely in "time not set" with no
      crash and no reboot
- [ ] F2. `time` keeps reporting not-set / not-synced; the rest of the system
      (pumps, sensors, console) is unaffected
- [ ] F3. Restore internet → SNTP eventually syncs on its own retry and `time`
      switches to the correct local time (no reboot required)

## Deferral path

If the rig, a real-internet AP, or the debug starve-hook build is unavailable,
defer the affected items with a written rationale and add them to the
deferred-HIL register (mirroring the handover's deferred-HIL pattern, e.g.
PR-04's `hil-004`). The host-test suite (`test_time.cpp` DST/plausibility,
`test_event_logger.cpp` producers + drop counter + reset-reason mapping +
littlefs rotation/persistence) already covers the pure logic deterministically;
the items above are the hardware-only confirmations — the SNTP wall-clock step,
the physical watchdog reboot + pump-OFF measurement, and cross-power-cycle
persistence.

**Sign-off**: date + result per item as a PR comment (pattern from PR #7).
