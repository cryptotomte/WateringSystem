# HIL Checklist: WiFi Provisioning & Station Management (007) — rev1 bench rig

**Purpose**: hardware-in-the-loop verification of PR-07 at Checkpoint 3 (Paul, bench rig)
**Rig**: ESP32 devkit + status LED on GPIO2, config button on GPIO18 (to GND, internal pull-up ⇒ held == LOW); pumps, BME280 and RS485 soil sensor as wired for the previous features. A second WiFi AP (phone hotspot or a spare router you can power-cycle) plus a phone/laptop to reach the setup page.
**Build**: rev1 target (`sdkconfig.board.rev1_devkit`), flash per `firmware/CLAUDE.md`
**Reference**: acceptance criteria `docs/prd/PR-07-wifi-provisioning.md`; spec FR-004/FR-013/FR-014/FR-015; `quickstart.md` §HIL; parity `docs/parity-checklist.md` §7/§9
**AP credentials**: setup SSID `CONFIG_WS_PROV_AP_SSID` (default `WateringSystem-Setup`), WPA2 password `CONFIG_WS_PROV_AP_PASSWORD` (documented non-secret, set at build). Never a legacy password.
**Note**: credential VALUES must never appear in the serial log or the portal response (FR-004) — watch for leaks while testing.

## A. Fresh-device provisioning (US1, acceptance [HIL] #1)

- [ ] A1. Flash a device with NO stored credentials (or run `config
      wifi-clear` on the console, then reboot) → serial shows
      `WiFi: provisioning mode (SoftAP setup portal)`; the SoftAP SSID
      `WateringSystem-Setup` is visible from a phone
- [ ] A2. Join the AP (WPA2, `CONFIG_WS_PROV_AP_PASSWORD`); browse to
      `http://192.168.4.1/` → the English setup page loads (SSID +
      password form), self-contained, no external assets
- [ ] A3. Submit REAL home-WiFi credentials → success page indicating a
      restart is required; device restarts (~3 s later, response delivered
      first) and on reboot enters station mode and joins the LAN
- [ ] A4. Confirm the submitted password never appears in the serial log
      or the HTTP response body (FR-004)
- [ ] A5. Invalid submits are rejected without persisting or restarting:
      empty SSID → 4xx; a 1–7 char password → 4xx; device stays
      provisionable (page still reachable)

## B. AP power-cycle / outage isolation (US2, acceptance [HIL] #2, FR-014)

- [ ] B1. With the device connected in station mode, start a watering-
      relevant activity (e.g. `pump plant start 30`) and let the 5 s
      environmental poll run
- [ ] B2. Cut the home AP (power it off) → the pump timed run and its
      self-stop, and the `env`/`level` polling cadence, continue
      UNAFFECTED throughout the outage (WiFi never touches watering)
- [ ] B3. Status LED toggles ~500 ms while the device is reconnecting;
      serial shows the reconnect cadence (retry every 10 s; after 5
      consecutive failures an extra 60 s pause before the next round)
- [ ] B4. Restore the AP → device reconnects on schedule; LED goes steady
      ON when the link is back; `wifi` console command reports
      `state=Connected` with an RSSI
- [ ] B5. No watchdog resets, no missed pump self-stops across the whole
      outage window

## C. Config-button-at-boot forces provisioning (US3, acceptance [HIL] #3)

- [ ] C1. On an ALREADY-configured device (station mode confirmed), power-
      cycle while holding the config button (GPIO18) → serial shows the
      "config button down at boot — hold 5000 ms" message and the status
      LED blinks every 100 ms during the hold
- [ ] C2. Keep holding ≥ 5 s → serial shows "config button held … forcing
      WiFi provisioning" then "clearing stored WiFi credentials"; device
      enters provisioning mode (AP up), credentials cleared
- [ ] C3. Release the button BEFORE 5 s on a configured device → serial
      shows "config button released early — not forcing provisioning";
      device proceeds to station mode with credentials intact (no clear)
- [ ] C4. Normal boot (button not pressed) proceeds immediately with no
      startup delay and no LED blink from the button path

## D. Wrong-password / no boot loop (US3, acceptance [HIL] #4, FR-013)

- [ ] D1. Provision a WRONG home-WiFi password (valid length, wrong value)
      → device keeps retrying on the 10 s / +60 s-after-5 schedule and
      NEVER reboots (no boot loop): the serial banner appears exactly once,
      not repeatedly
- [ ] D2. The device stays reachable/recoverable: hold the config button at
      boot (section C) to force provisioning and re-enter correct
      credentials → joins the LAN

## E. Console / portal credential equivalence (research D10)

- [ ] E1. Set credentials via the diag console `config wifi <ssid> <pass>`
      then reboot → device reaches the same connected state as a
      portal-provisioned device (`config get` shows `wifi=configured`;
      values not echoed)
- [ ] E2. `config wifi-clear` returns the device to the unconfigured state
      → next boot enters provisioning (parity with a fresh device)

## F. LED patterns (parity §7/§9)

- [ ] F1. Connect-attempt (Connecting/Reconnecting): status LED toggles
      ~500 ms
- [ ] F2. Config-button hold: status LED blinks every 100 ms during the
      hold window (distinct from the 500 ms connect toggle)
- [ ] F3. Connected: LED steady ON; ReconnectPaused: LED off

## G. Brownout watch (QUIRK 4)

- [ ] G1. During the AP power-cycle test (section B), watch for brownout-
      detector-induced resets on the rev1 devkit with brownout detection
      left ENABLED. Record whether any WiFi power-spike reset is observed —
      if reproduced, note it (the legacy firmware disabled the brownout
      detector to mask this; the ESP-IDF target keeps it on, QUIRK 4)

## Deferral path

If the rig or a spare/power-cyclable AP is unavailable, defer the affected
items with a written rationale and add them to the deferred-HIL register
(mirroring the handover's deferred-HIL pattern, e.g. PR-04's
`hil-004`). The host-test suite (`test_wifi.cpp`) already covers the
reconnect schedule, boot-mode truth table, credential validation,
isolation and the no-boot-loop property deterministically; the items above
are the hardware-only confirmations.

**Sign-off**: date + result per item as a PR comment (pattern from PR #7).
