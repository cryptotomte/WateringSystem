# Feature Specification: SNTP Time, Task Watchdog & Event Logging

**Feature Branch**: `008-sntp-watchdog-logging`

**Created**: 2026-07-04

**Status**: Draft

**Input**: PR-08 (`docs/prd/PR-08-sntp-watchdog-logging.md`) — completes the phase-2 exit criteria:
correct wall-clock time (SNTP, Swedish pool, CET/CEST), a hardware task watchdog on critical tasks, and
structured logging with persistence of important events. Depends on PR-06 (littlefs event storage) and
PR-07 (WiFi network). Parity: `docs/parity-checklist.md` §7 (NTP), QUIRK 2/3, §6 (event log surface).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Correct local time after boot, safe before sync (Priority: P1)

Once the device is on the network it learns the correct wall-clock time from the internet and keeps it,
reported in Swedish local time (CET in winter, CEST in summer) wherever a person reads it. Before the
first successful sync the device is explicitly in a "time-not-set" state that the rest of the system can
detect, so nothing that depends on the calendar/clock misfires on a bogus 1970 time.

**Why this priority**: Correct, monotonic timestamps underpin every logged event and every future
schedule decision. The explicit not-yet-synced state is the safety-relevant part: watering schedule logic
(PR-11) must never act on an unset clock.

**Independent Test**: Boot a networked device; confirm it reports "time not set" until sync, then reports
correct Swedish local time with the right DST offset; confirm timezone conversion is correct across a DST
boundary (host-tested with fixed epochs).

**Acceptance Scenarios**:

1. **Given** a freshly booted device that has not yet synced, **When** its time state is queried, **Then**
   it reports "time not set" and any consumer can detect that state (rather than reading a 1970/epoch-zero
   time as if valid).
2. **Given** the device has joined the network, **When** SNTP obtains a plausible time from the Swedish
   pool, **Then** the device transitions to "time set", records the sync, and reports correct Swedish
   local time.
3. **Given** a known epoch on a winter date and one on a summer date, **When** each is converted to local
   time, **Then** the winter time shows the CET offset and the summer time shows the CEST offset (DST
   rules for Europe/Stockholm).
4. **Given** SNTP cannot reach the server, **When** sync fails, **Then** the device keeps running normally
   (non-fatal), stays in "time not set", and keeps retrying in the background.
5. **Given** any point in time, **When** an internal timestamp is recorded, **Then** it is epoch seconds
   and monotonic across the migration (user-facing displays convert to local time; storage stays epoch).

---

### User Story 2 - Persistent record of important events (Priority: P2)

Important system events are written to a durable, bounded log that survives power loss, each entry
carrying a timestamp and a cause. An operator can read recent events over the serial console to
understand what the device did and why — including why it last restarted.

**Why this priority**: This is the observability backbone and the safety audit trail. It captures the
fail-safe activations and the reset reason that make watchdog/brownout events diagnosable after the fact.
It reuses the durable event-log storage already provided by PR-06.

**Independent Test**: Trigger several loggable events (pump start/stop with cause, a sensor-failure
fail-safe activation, a WiFi state change), read them back over the serial console with timestamps and
causes; power-cycle and confirm the log persists; confirm rotation bounds the size (host-tested against
mock storage).

**Acceptance Scenarios**:

1. **Given** a loggable event occurs (pump start/stop with cause, sensor-failure fail-safe activation,
   WiFi state change), **When** it is recorded, **Then** the entry carries a timestamp and the cause and
   is readable over the serial diagnostic console.
2. **Given** the device just booted, **When** the boot completes, **Then** the reset reason (normal,
   watchdog, brownout, panic, etc.) is captured and persisted as an event.
3. **Given** the log has reached its size bound, **When** new events are written, **Then** older entries
   are rotated out (bounded size) and the newest events are always retained.
4. **Given** events were logged before a power cut, **When** the device is powered back on, **Then** the
   previously logged events are still present.
5. **Given** the clock is not yet set when an event occurs, **When** it is recorded, **Then** the entry is
   still logged (with the best timestamp available / marked as pre-sync) rather than dropped.

---

### User Story 3 - Automatic recovery from a hung task, pumps safe (Priority: P3)

If a critical task hangs, the device reboots itself automatically instead of freezing; after that reboot
the pumps are OFF, and the reason for the restart is recorded so the operator can see it happened.

**Why this priority**: Reliability and safety net. A frozen controller must not leave a pump running or
the greenhouse unattended; hardware watchdog-forced recovery + the boot fail-safe together guarantee a
safe, self-healing restart.

**Independent Test**: Deliberately starve a registered critical task on the rig; confirm the device
reboots, the pumps are measured OFF immediately after the reset, and the reset reason is persisted in the
event log.

**Acceptance Scenarios**:

1. **Given** a critical task is registered with the watchdog, **When** that task stops servicing the
   watchdog for longer than the timeout, **Then** the device reboots automatically.
2. **Given** a watchdog-induced reboot, **When** the device comes back up, **Then** both pump outputs are
   OFF immediately at boot (existing fail-safe invariant), before any task starts.
3. **Given** a watchdog (or brownout/panic) reset occurred, **When** the device reboots, **Then** the
   reset reason is captured and persisted to the event log.
4. **Given** all critical tasks are servicing the watchdog normally, **When** the system runs, **Then**
   the watchdog never fires (no spurious reboots).

---

### Edge Cases

- **Clock never syncs (offline device)**: device runs indefinitely in "time not set"; events still log;
  schedule logic (PR-11) treats not-set as "do not act on calendar/clock".
- **Clock jumps forward on first sync**: timestamps recorded before sync were low/pre-sync; after sync
  they are correct — the log tolerates the discontinuity (entries are ordered by insertion, timestamps
  reflect best-known time at write).
- **DST transition instant**: the spring-forward/fall-back boundaries convert correctly (the ambiguous/
  skipped local hour follows the standard POSIX TZ rule).
- **WiFi task hangs**: a WiFi/network stall must NOT reboot the device (watering must be unaffected) — the
  watchdog covers watering-critical tasks, not the network task (see Assumptions).
- **Event written while storage is full/rotating**: rotation makes room; a storage write failure is
  logged/counted but never crashes the device or affects watering.
- **Repeated watchdog reboots (boot loop risk)**: each reboot is safe (pumps OFF) and the reset reason is
  recorded each time so the loop is diagnosable.

## Requirements *(mandatory)*

### Functional Requirements

**Time / SNTP (FR10)**

- **FR-001**: The system MUST obtain wall-clock time via SNTP from the Swedish NTP pool after the WiFi
  station connects; failure to reach the server MUST be non-fatal (the device keeps running and keeps
  retrying).
- **FR-002**: The system MUST apply the Europe/Stockholm timezone with correct CET/CEST daylight-saving
  rules so that user-facing times are shown in Swedish local time.
- **FR-003**: Internal timestamps MUST be epoch seconds and remain monotonic across the migration;
  conversion to local time happens only for user-facing output.
- **FR-004**: The system MUST expose an explicit "time-not-set" state (no plausible time yet) that any
  consumer can detect, and MUST NOT let a pre-sync placeholder time be treated as valid wall-clock time.
- **FR-005**: The system MUST expose sync status (synced / not-yet-synced, and the time of last successful
  sync) for status reporting and the serial diagnostic console.

**Task watchdog**

- **FR-006**: The system MUST register a hardware task watchdog on all watering-critical tasks (the
  environmental sensor task today; the watering/control and reservoir-control tasks arriving in PR-11 — the
  registration mechanism MUST be ready for them).
- **FR-007**: When a registered critical task fails to service the watchdog within the timeout, the system
  MUST reboot automatically.
- **FR-008**: After a watchdog-induced reset, both pump outputs MUST be OFF immediately at boot, before any
  task starts (existing fail-safe invariant — pumps off at boot / after watchdog reset / after OTA restart).
- **FR-009**: The system MUST capture the reset reason at boot and persist it to the event log.

**Logging & event persistence**

- **FR-010**: The system MUST maintain a persistent, bounded, rotating event log (reusing PR-06 storage)
  that survives power loss; the newest events are always retained when older ones are rotated out.
- **FR-011**: Each event entry MUST carry a timestamp and a cause/detail. Logged event categories MUST
  include: pump start/stop with cause, sensor-failure fail-safe activations, WiFi state changes, reset
  reasons (watchdog/brownout/panic/normal), and a defined surface for OTA events (emitted by PR-13).
- **FR-012**: The event log MUST be readable over the serial diagnostic console. (HTTP/API exposure is
  out of scope — PR-09.)
- **FR-013**: Per-component log verbosity MUST be configurable (build-time configuration).
- **FR-014**: Logging and event persistence MUST NOT block or affect watering; a storage failure while
  logging MUST be contained (logged/counted) and never crash the device or disturb the watering path.

### Key Entities *(include if feature involves data)*

- **Time state**: synced flag, last-sync epoch, current epoch; "not set" when no plausible time yet.
  Plausibility threshold (a minimum reasonable year) distinguishes set from not-set.
- **Event log entry**: timestamp (epoch, best-known at write), category (pump / fail-safe / wifi / reset /
  ota), and a cause/detail string. Stored in the durable rotating event log from PR-06.
- **Reset reason**: the hardware-reported cause of the last boot (normal, watchdog, brownout, panic,
  power-on, software), captured once at boot and logged.
- **Watchdog registration**: the set of critical tasks subscribed to the hardware task watchdog and the
  timeout after which a non-servicing task forces a reboot.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: After boot on a working network, the device shows correct Swedish local time (correct DST
  offset) and reports "synced"; before that it reports "time not set" — never a bogus 1970 time treated as
  valid.
- **SC-002**: Timezone conversion is correct on both sides of a DST boundary (verified for winter and
  summer epochs without any hardware).
- **SC-003**: A deliberately hung critical task causes an automatic reboot within the watchdog timeout,
  and after that reboot the pumps are measured OFF and the reset reason appears in the event log.
- **SC-004**: The event log survives a power cycle: events logged before the cut (with timestamps and
  causes) are present after power-on.
- **SC-005**: The event log stays within its size bound under sustained event volume, always retaining the
  most recent events (verified without hardware against mock storage).
- **SC-006**: A network/SNTP outage or a storage-write failure never reboots the device on its own and
  never interrupts an active watering cycle.
- **SC-007**: Both firmware board targets build with the feature included.

## Assumptions

- **SNTP server**: the Swedish pool `se.pool.ntp.org` is the default (the legacy unit used
  `0.se.pool.ntp.org`; the pooled hostname is kept). The server is a documented, build-configurable value.
- **Time-not-set detection**: a plausible-time threshold (minimum year, e.g. 2020 as the legacy unit used)
  distinguishes an unset clock from a synced one — matching the legacy plausibility check.
- **Watchdog scope now vs later**: only the environmental sensor task exists as a watering-relevant task
  today; the watering/control and reservoir-control tasks land in PR-11. PR-08 delivers the watchdog
  registration mechanism and registers the currently-existing critical task(s); PR-11 registers its tasks
  through the same mechanism. This scoping is recorded so it does not surface as a review finding.
- **WiFi task deliberately excluded from the watchdog**: a WiFi/network stall MUST NOT reboot the device
  (watering must be unaffected — the PR-07 isolation guarantee). The watchdog covers watering-critical
  tasks, not the network task.
- **Watchdog action = reboot**: the watchdog is configured to force a reboot (panic/abort → restart) on a
  non-servicing task, so recovery is automatic; the timeout is a documented, build-configurable value with
  a safe margin over the slowest critical-task cadence (the legacy software watchdog used 30 s but was
  ineffective — QUIRK 3).
- **Event log storage reuse**: the durable, bounded, rotating event log is the one delivered by PR-06
  (`IDataStorage` event log); PR-08 defines the event categories and wires the producers into it rather
  than inventing new storage.
- **Pre-sync timestamps**: events logged before the first sync use the best available time (low/pre-sync
  epoch) and are still recorded, never dropped; ordering is by insertion.

### Dependencies

- **PR-06 (littlefs / `IDataStorage`)** — the durable rotating event log this feature writes to.
- **PR-07 (WiFi network)** — SNTP starts after the station connects; WiFi state changes are a logged
  event source.
- **Board fail-safe (pumps OFF at boot)** — the existing `app_main` invariant that FR-008 relies on after
  a watchdog reset; this feature reaffirms and records the reset reason, it does not change the fail-safe.

### Out of Scope

- The watering schedule itself and its use of the clock/not-set state (PR-11).
- HTTP/API exposure of the event log or time/sync status (PR-09).
- OTA event sources (PR-13 emits into the event-log surface defined here).
- Migrating any legacy log/history data (the event log is a new surface, no legacy equivalent).
