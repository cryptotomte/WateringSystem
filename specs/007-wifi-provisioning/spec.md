# Feature Specification: WiFi Provisioning & Station Management

**Feature Branch**: `007-wifi-provisioning`

**Created**: 2026-07-04

**Status**: Draft

**Input**: PR-07 (`docs/prd/PR-07-wifi-provisioning.md`) — WiFi station management with robust
reconnection plus first-boot provisioning, implementing the FR9 baseline decision (custom SoftAP
portal). Parity source: `docs/parity-checklist.md` §7.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Provision a fresh device onto home WiFi (Priority: P1)

An operator powers on a device that has never been configured. With no stored credentials the device
brings up its own WiFi access point with a known name and a reachable setup page. The operator connects
a phone or laptop to that access point, opens the setup page, picks/enters their home network name and
password, and submits. The device stores the credentials and restarts, after which it joins the home
network as a station.

**Why this priority**: Without provisioning, a device out of the box cannot be reached at all — every
other network-dependent capability (status UI, remote control, time sync) is unreachable. This is the
minimum viable slice: a device the operator can get onto their network from any phone browser, with no
app install and no cable.

**Independent Test**: Flash a device with empty credentials, confirm it exposes the setup access point
at the known address, submit valid credentials from a browser, and confirm the device restarts and
associates with the target network.

**Acceptance Scenarios**:

1. **Given** a device with an empty stored SSID, **When** it boots, **Then** it enters provisioning mode
   and exposes a WPA2-protected access point at a fixed address serving a WiFi setup page.
2. **Given** the device is in provisioning mode, **When** the operator submits a valid SSID
   (1–32 characters) and password (empty, or 8 or more characters), **Then** the credentials are persisted
   and the device confirms success and restarts shortly after so the confirmation is delivered first.
3. **Given** the device restarted with freshly stored valid credentials for a reachable network,
   **When** it boots, **Then** it starts in station mode and connects to that network.
4. **Given** the device is in provisioning mode, **When** the operator submits an SSID outside 1–32
   characters or a non-empty password shorter than 8 characters, **Then** the submission is rejected, no
   credentials are stored, no restart occurs, and the device stays provisionable.

---

### User Story 2 - Stay connected and survive network outages without affecting watering (Priority: P2)

A configured device runs day to day on the greenhouse network. The home access point occasionally
reboots, drops, or briefly loses power. The device must notice the loss, keep retrying on a predictable
schedule until it reconnects, and expose its current connection state for the status LED and status
reporting — all without ever pausing, delaying, or otherwise disturbing sensor polling or pump control.

**Why this priority**: The device's primary job is watering, which must never depend on the network.
Reconnection resilience protects the remote-visibility experience, but the hard requirement is that a
WiFi outage of any duration has zero effect on watering safety and timing.

**Independent Test**: With the device connected and (in a bench scenario) an active watering cycle
running, cut the access point; confirm watering continues uninterrupted and the pump control loop and
sensor polling keep their cadence, then restore the access point and confirm the device reconnects
automatically on schedule.

**Acceptance Scenarios**:

1. **Given** a configured device connected in station mode, **When** the connection is lost, **Then** the
   device retries connecting on a fixed 10-second interval.
2. **Given** repeated reconnection failures, **When** 5 consecutive attempts have failed, **Then** the
   device waits an additional 60 seconds before beginning the next round of attempts.
3. **Given** the device is connected, **When** connection monitoring runs, **Then** it checks connection
   health every 5 seconds; monitoring is suspended while the device is in provisioning (AP) mode.
4. **Given** an active watering cycle and running sensor polling, **When** WiFi is lost for any duration
   and later restored, **Then** watering behavior, pump control timing, and sensor polling are completely
   unaffected throughout, and the device reconnects when the network returns.
5. **Given** any point in the connect/reconnect lifecycle, **When** status is queried, **Then** the
   current connection state (e.g. connecting, connected, disconnected/retrying, provisioning) is available
   for status reporting and for driving the status LED.

---

### User Story 3 - Recover a device with wrong or changed credentials (Priority: P3)

The operator has moved the device to a new network, or mistyped the password during setup. They need a
reliable, cable-free way to force the device back into provisioning mode, and a device that mistyped its
password must never get stuck in a reboot loop.

**Why this priority**: This is the recovery path. It is less frequent than first-boot setup and normal
operation, but without it a device with bad credentials becomes unrecoverable in the field without a
reflash.

**Independent Test**: On an already-configured device, hold the config button while powering on and
confirm the device enters provisioning mode with its stored credentials cleared; separately, provision a
wrong password and confirm the device keeps retrying on schedule and remains provisionable rather than
rebooting repeatedly.

**Acceptance Scenarios**:

1. **Given** an already-configured device, **When** the config button is held during boot for the
   required hold time, **Then** the stored WiFi credentials are cleared and the device restarts into
   provisioning mode.
2. **Given** a device configured with a wrong home-WiFi password, **When** it boots and fails to connect,
   **Then** it keeps retrying using the fixed-interval reconnection strategy and does not enter a boot
   loop.
3. **Given** a device that cannot connect with its stored credentials, **When** the operator wants to
   correct them, **Then** the device remains provisionable (reachable to receive new credentials) via the
   config-button recovery path.

---

### Edge Cases

- **Empty vs. open network**: An empty submitted password is valid (open home network); a non-empty
  password must be 8+ characters (WPA2 minimum). A 1–7 character password is rejected.
- **Submit while already configured**: Credential submission is only honored while the device is in
  provisioning (AP) mode; a submission attempt outside provisioning mode is rejected.
- **Loss of the setup client mid-provisioning**: If the operator's phone disconnects from the setup AP
  before submitting, the device stays in provisioning mode indefinitely (no timeout that abandons setup).
- **Network returns during the 60-second pause**: Reconnection resumes on the next scheduled round; a
  returned network does not need to wait out the full pause beyond the normal retry cadence.
- **Config button held on an unconfigured device**: Already provisionable; the forced-provisioning path
  is a no-op beyond confirming provisioning mode.
- **Credentials submitted, restart interrupted by power loss before it completes**: On next boot the
  device reads whatever was persisted; if the write completed it connects, otherwise it is unconfigured
  and re-enters provisioning.

## Requirements *(mandatory)*

### Functional Requirements

**Provisioning entry & mode**

- **FR-001**: The system MUST treat a device with an empty stored SSID as unconfigured and boot it into
  provisioning mode. (The legacy `CONFIGURE_ME` sentinel is retired; unconfigured is represented as an
  empty SSID string in stored configuration.)
- **FR-002**: In provisioning mode the system MUST expose a WiFi access point at a fixed, known address
  (192.168.4.1) with a known SSID, serving a WiFi setup page.
- **FR-003**: The provisioning access point MUST use WPA2 with a password sourced from a build-time
  configuration option (a documented non-secret), not a hard-coded source constant. A brand-new password
  MUST be chosen at implementation; the legacy password (exposed in public git history) MUST NOT be reused.
- **FR-004**: The system MUST allow an operator to force provisioning mode on an already-configured device
  by holding the config button during boot; doing so MUST clear the stored WiFi credentials and restart
  into provisioning mode.

**Credential capture & persistence**

- **FR-005**: The setup page MUST accept an SSID of 1–32 characters and a password that is either empty or
  at least 8 characters; submissions violating these bounds MUST be rejected without persisting anything.
- **FR-006**: Credential submission MUST be honored only while the device is in provisioning (AP) mode and
  rejected otherwise.
- **FR-007**: On a valid submission the system MUST persist the credentials to non-volatile storage, return
  a success confirmation to the client, and then restart the device after a short delay (~3 seconds) so the
  confirmation is delivered before the restart.
- **FR-008**: Stored WiFi credentials MUST persist across reboots and MUST be read from non-volatile storage
  at boot to decide between provisioning mode and station mode.

**Station connection & reconnection**

- **FR-009**: When configured, the system MUST start in station mode and attempt to connect using the
  stored credentials.
- **FR-010**: On connection loss the system MUST retry connecting on a fixed 10-second interval (parity
  baseline; explicitly NOT exponential backoff).
- **FR-011**: After 5 consecutive failed connection attempts the system MUST wait an additional 60 seconds
  before starting the next round of attempts.
- **FR-012**: The system MUST monitor connection health every 5 seconds while in station mode, and MUST
  suspend this monitoring while in provisioning (AP) mode.
- **FR-013**: A device configured with credentials that never succeed (e.g. wrong password) MUST keep
  retrying under the reconnection strategy and MUST NOT enter a reboot loop, remaining provisionable via the
  config-button recovery path.

**Isolation from watering (safety)**

- **FR-014**: WiFi connection, disconnection, reconnection, and provisioning activity MUST NOT block,
  delay, or otherwise influence watering tasks (sensor polling and pump control). Loss of WiFi for any
  duration MUST have zero effect on watering behavior or timing.

**State exposure**

- **FR-015**: The current WiFi connection state MUST be exposed for consumption by status reporting and by
  the status LED indicator.

### Key Entities *(include if feature involves data)*

- **WiFi credentials**: The stored network name (SSID) and password used for station-mode connection.
  Persisted in non-volatile storage (from the storage feature, PR-06). Unconfigured is represented by an
  empty SSID.
- **Connection state**: The device's current network posture — e.g. provisioning, connecting, connected,
  disconnected/retrying — together with the counters/flags needed to drive the reconnection schedule and
  status reporting.
- **Provisioning AP configuration**: The known SSID, fixed address, and WPA2 password (from build-time
  configuration) used when the device exposes its setup access point.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: An operator can take a factory-fresh (unconfigured) device and get it onto their home
  network entirely from a phone or laptop browser, with no app install and no cable.
- **SC-002**: During a WiFi outage of any duration, an in-progress watering cycle completes on its normal
  schedule and sensor polling keeps its normal cadence — measurably zero deviation attributable to the
  network event.
- **SC-003**: After the home network returns from an outage, the device re-associates automatically within
  one reconnection round of it becoming available, with no operator action.
- **SC-004**: A device given a wrong home-WiFi password stays powered and provisionable indefinitely
  without rebooting on a loop, and can be corrected via the config-button recovery path.
- **SC-005**: An operator can force an already-configured device back into provisioning mode using only the
  config button at boot, with no cable and no reflash.
- **SC-006**: Both firmware board targets build with the provisioning feature included, and the FR9
  provisioning-method decision is documented in the feature directory and referenced from
  `firmware/CLAUDE.md`.

## Assumptions

- **FR9 method is pre-decided (not re-opened here)**: The provisioning mechanism is a custom SoftAP portal,
  resolved by Paul on 2026-06-10. IDF's `wifi_provisioning` component is a documented fallback ONLY if the
  portal proves fragile during implementation, in which case the choice is re-escalated to Paul. This spec
  records and validates the portal choice; it does not re-evaluate it.
- **Minimal, standalone portal**: The provisioning setup page runs on a minimal standalone web server
  instance. The full JSON status API and the `/api/wifi/scan` and `/api/wifi/config` endpoints belong to
  PR-09 and are out of scope here.
- **AP SSID name**: The setup access point keeps a known, stable SSID consistent with the legacy behavior
  (a fixed setup SSID such as `WateringSystem-Setup`); the exact string is confirmed at implementation.
- **Config-button hold time**: The forced-provisioning button hold at boot uses the established hold
  duration (legacy: ~5 seconds during startup) unless bring-up shows a reason to change it.
- **STA and AP are mutually exclusive**: The device is either connected/attempting in station mode OR
  serving the provisioning AP, not both simultaneously (matches legacy behavior).
- **No pre-persist credential validation**: The device does not test the submitted credentials against the
  target network before persisting and restarting (matches legacy behavior); a wrong password is handled by
  the reconnection strategy + config-button recovery, not by rejecting it at submit time.
- **Brownout detector stays enabled**: Unlike the legacy firmware (which disabled the brownout detector to
  mask WiFi power spikes on the rev1 devkit), the target keeps brownout protection enabled unless bring-up
  reproduces the symptom (parity checklist QUIRK 4). This is a watch-item during HIL, not a requirement of
  this feature.

### Dependencies

- **PR-06 (NVS/storage)** — provides the persisted-configuration store from which WiFi credentials are read
  and to which they are written. Unconfigured state is an empty SSID string in that store.
- **Board component (config button, status LED GPIO)** — provides the config-button input used for forced
  provisioning and the status LED used for connection-state indication.

### Out of Scope

- HTTP status/control API and the `/api/wifi/scan`, `/api/wifi/config`, and full status JSON endpoints
  (PR-09).
- SNTP / time synchronization (PR-08).
- The full web frontend and its LittleFS assets (PR-10).
- OTA updates (PR-13).
- mDNS or fixed-hostname conveniences beyond what is free with the chosen components (explicitly not
  gold-plated).
