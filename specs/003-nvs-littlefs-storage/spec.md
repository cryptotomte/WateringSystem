# Feature Specification: NVS Configuration and LittleFS Data Storage

**Feature Branch**: `003-nvs-littlefs-storage`

**Created**: 2026-06-11

**Status**: Draft

**Input**: User description: "PR-06 nvs-littlefs-storage (Phase 2 — infrastructure). Port the storage layer to ESP-IDF per docs/prd/PR-06-nvs-littlefs-storage.md: typed configuration in NVS with compiled-in factory defaults (FR13), mounted littlefs partition with usage reporting, sensor history and event records with explicit bounding, host-testable interfaces with mocks. No migration of Arduino-era data."

## Clarifications

### Session 2026-06-11

- Q: Sensor history retention target — legacy capacity (~1 week), fixed ≥30-day
  window, or maximize (~months, fill available ~870–900 KiB)? → A: Fixed ≥30-day
  retention for all metrics at default log interval; keep headroom for the event
  log and future needs.
- Q: Do sensorReadInterval and dataLogInterval become first-class settable
  configuration items (legacy persists them but has no setters or API)? → A: Yes,
  settable typed items with validation; deliberate improvement, divergence
  recorded in the parity checklist.
- Q: Reservoir feature flags (reservoirPumpEnabled, reservoirAutoLevelControl) —
  persist in NVS (partition plan) or keep legacy non-persistence? → A: Deferred
  to PR-05 (which introduces the reservoir board flag); not part of this PR's
  configuration schema. The store must be extensible for PR-05 additions.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Configuration survives restarts and resets to safe defaults (Priority: P1)

The greenhouse operator adjusts watering settings (moisture thresholds, watering
duration, soak pause, automatic watering on/off). Those settings persist across
power cycles, firmware restarts, and OTA updates. A fresh device — or one whose
configuration storage was erased or corrupted — boots with the documented factory
defaults rather than failing or behaving unpredictably. An explicit factory-reset
operation returns all configuration to those defaults.

**Why this priority**: FR13 is the core requirement of this PR. Every later phase
(WiFi provisioning, HTTP API, watering controller) reads its configuration through
this layer; safe defaults are part of the safety story — a device must never run
with undefined watering parameters.

**Independent Test**: On a fresh-flashed device, verify documented defaults are in
effect; change a setting, power-cycle, verify it persisted; invoke factory reset,
verify defaults are restored. Fully verifiable on host against the configuration
interface with storage mocked.

**Acceptance Scenarios**:

1. **Given** a device with empty/erased configuration storage, **When** the firmware
   boots and the configuration layer initializes, **Then** every configuration item
   reads back its documented factory default.
2. **Given** a running device, **When** the operator changes a configuration item to
   a valid value, **Then** the change is persisted immediately and survives a power
   cycle.
3. **Given** a stored value that is out of its documented valid range (e.g. written
   by a future buggy build), **When** the configuration is read, **Then** the
   documented default is returned for that item instead of the invalid value.
4. **Given** a configured device, **When** factory reset is invoked, **Then** all
   configuration items return to factory defaults and stored WiFi credentials are
   removed.
5. **Given** any failed write (storage full, I/O error), **When** the write is
   rejected, **Then** the previously stored value remains intact and readable.

---

### User Story 2 - Sensor history is recorded, bounded, and retrievable (Priority: P2)

The system records periodic sensor readings (environment temperature/humidity/
pressure, soil moisture/temperature/pH/EC, and optional nutrient values) so the
operator can later view history over a chosen time window. Storage is explicitly
bounded: the device runs for months without history growth degrading or breaking
the system — old data is discarded in favor of new data, never the reverse.

**Why this priority**: Enables the history part of FR7 (consumed by the HTTP API in
PR-09). The legacy firmware's unbounded history files eventually break retrieval;
the explicit bound is the headline improvement required by the parity checklist
(line 172: "equivalent or better retention behavior with explicit bounding").

**Independent Test**: Through the data-storage interface (mocked filesystem on
host): store readings, query by metric and time range, verify correct filtering;
fill storage past its bound, verify oldest data is evicted and writes keep
succeeding.

**Acceptance Scenarios**:

1. **Given** stored readings for a metric, **When** history is queried for a time
   range, **Then** exactly the readings inside the range are returned in
   chronological order.
2. **Given** a query for a metric or range with no data, **When** history is
   queried, **Then** an empty result is returned (not an error) — parity with
   legacy behavior.
3. **Given** history storage at its configured bound, **When** new readings arrive,
   **Then** the oldest readings are discarded, the new readings are stored, and
   retrieval keeps working.
4. **Given** sustained recording of all metrics at the default log interval,
   **Then** at least the most recent 30 days of history remain retrievable and
   total history storage stays within its documented budget (≥30-day retention
   per the 2026-06-11 clarification).

---

### User Story 3 - Safety-relevant events are persisted (Priority: P3)

The system can persist operational event records — pump start/stop with cause,
fail-safe activations, connectivity state changes, OTA events, and reset/watchdog
reasons — so the operator can diagnose what the device did and why, even after
restarts. (This PR provides the storage capability; wiring actual producers is
PR-08.)

**Why this priority**: New surface with no legacy equivalent; the constitution
requires safety-relevant events to be persisted. Needed by PR-08/PR-09 but has no
consumer inside this PR, so it ranks below configuration and history.

**Independent Test**: Through the data-storage interface on host: append event
records, retrieve them newest-first, verify rotation keeps total event storage
within its bound while always retaining the most recent events.

**Acceptance Scenarios**:

1. **Given** appended event records, **When** events are retrieved, **Then** they
   come back with timestamp, category, and detail, most recent first.
2. **Given** event storage at its bound, **When** new events are appended, **Then**
   the oldest events are rotated out and the newest events are always retained.
3. **Given** a burst of events (e.g. crash loop), **When** events are appended
   rapidly, **Then** event storage never exceeds its budget and never starves
   history or configuration storage.

---

### User Story 4 - Storage health is visible (Priority: P3)

The operator (and later the status API) can see how much data storage is in use:
total and used bytes for the data filesystem. A freshly flashed device prepares its
data filesystem automatically on first boot; a corrupted filesystem is recovered by
reformatting rather than leaving the device unusable.

**Why this priority**: Parity item (legacy reports storage in serial status and
`/api/status`); also the bring-up proof that the data partition works at all.

**Independent Test**: [HIL] Fresh-flash the rig: first boot formats and mounts the
data filesystem and reports plausible total/used numbers; subsequent boots mount
without reformatting and previously stored data is still present.

**Acceptance Scenarios**:

1. **Given** a fresh-flashed device (blank data partition), **When** it boots,
   **Then** the data filesystem is formatted and mounted automatically and usage
   reporting shows total/used bytes.
2. **Given** a device with an unmountable/corrupted data filesystem, **When** it
   boots, **Then** the filesystem is reformatted and the device continues operating
   (data loss is accepted; bricking is not) — parity with legacy mount behavior.
3. **Given** a mounted filesystem with stored data, **When** the device reboots,
   **Then** the data survives and usage figures reflect it.

---

### Edge Cases

- Power loss mid-write: a torn configuration write must never leave the item
  unreadable — the previous value or the default must win; a torn history/event
  append may lose that record but must not corrupt earlier records.
- Configuration storage full: writes fail explicitly; existing values remain
  readable; the system keeps running.
- System clock not yet set (before time sync): readings/events are still accepted;
  timestamps are epoch-based as provided by the caller (time correctness is the
  caller's concern, per parity checklist line 184).
- Concurrent access from multiple tasks (controller logging while status/history is
  being read): no corruption, no torn reads.
- Query with start time after end time: empty result, not an error.
- Storing a reading for a metric never seen before: accepted and retrievable up
  to the documented metric cap of 10 distinct metrics (exactly the legacy metric
  set's size; nutrient metrics appear only when the sensor provides them). An
  11th distinct metric is rejected with an error — this guards the storage
  budget and prevents a buggy caller from silently destroying history.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST provide a typed configuration store with these items,
  defaults, and valid ranges (the documented factory defaults, verified against the
  legacy firmware):

  | Item | Default | Valid range | Unit |
  |---|---|---|---|
  | moistureThresholdLow | 30.0 | 0–100 | % |
  | moistureThresholdHigh | 55.0 | 0–100 | % |
  | wateringDuration | 20 | 1–300 | s |
  | minWateringInterval (soak pause) | 300 | ≥ 1 | s |
  | wateringEnabled | true | — | bool |
  | sensorReadInterval | 5000 | ≥ 1000 | ms |
  | dataLogInterval | 300000 | ≥ 60000 | ms |

  All seven items are first-class settable items, including sensorReadInterval
  and dataLogInterval (decision 2026-06-11: a deliberate improvement — legacy
  persists these two but offers no way to change them; the divergence is recorded
  in the parity checklist, and the future API may expose them).

- **FR-002**: Reading any configuration item that is missing, erased, or out of its
  valid range MUST return the documented factory default (FR13). The device MUST
  never operate with undefined configuration.

- **FR-003**: Accepted configuration changes MUST be persisted immediately and
  survive power cycles, restarts, and OTA updates. Writes of out-of-range values
  MUST be rejected without altering the stored value.

- **FR-004**: The configuration store MUST reserve storage for WiFi credentials
  (network name and secret) with "unconfigured" as factory state. Provisioning UX
  is out of scope (PR-07); this PR defines where credentials live, that they are
  excluded from any diagnostic output, and that factory reset removes them.

- **FR-005**: An explicit factory-reset operation MUST restore every configuration
  item to its factory default and remove stored WiFi credentials, equivalent to
  erasing the configuration storage (per docs/partition-plan.md factory-reset
  doctrine).

- **FR-006**: Reservoir feature flags (reservoirPumpEnabled,
  reservoirAutoLevelControl) are NOT part of this PR's configuration schema
  (decision 2026-06-11): legacy deliberately resets them to false at boot (parity
  checklist line 171) while docs/partition-plan.md line 91 anticipated NVS entries
  — the persistence decision is deferred to PR-05, which introduces the
  reservoir board flag (rev1-only since the single-pump decision). The
  configuration store's design allows PR-05 to add items without contract
  changes (per-item schema, see plan/research D8).

- **FR-007**: The system MUST prepare its data filesystem on the dedicated data
  partition at startup: mount if valid, format-then-mount on first boot or
  corruption (no manual intervention, no bricking — parity checklist line 166).

- **FR-008**: The system MUST report data-filesystem usage as total and used bytes,
  suitable for the serial status output and status API (parity checklist line 106).

- **FR-009**: The system MUST store sensor readings (metric identifier, epoch
  timestamp, numeric value) and return them filtered by metric and time range in
  chronological order. Errors and no-data conditions yield an empty result, not a
  failure (parity with legacy retrieval). At most 10 distinct metrics are
  accepted (budget guard, sized to the legacy metric set); storing an 11th
  distinct metric is rejected with an error.

- **FR-010**: Sensor history storage MUST be explicitly bounded with
  oldest-data-first eviction; retrieval MUST keep working at and beyond the bound.
  The bound and resulting retention MUST be documented in the parity checklist as a
  deliberate divergence from the legacy unbounded format.

- **FR-011**: The system MUST store event records (epoch timestamp, category,
  detail) covering at least: pump start/stop with cause, fail-safe activation,
  connectivity state change, OTA event, reset/watchdog reason. Event storage MUST
  rotate within an explicit budget, always retaining the most recent events.
  Producers are out of scope (PR-08); this PR delivers the storage and retrieval
  capability.

- **FR-012**: Configuration and data-storage capabilities MUST be exposed through
  hardware-agnostic interfaces with mock implementations, fully exercisable in the
  host test suite (Constitution II). The legacy single mixed-concern interface is
  deliberately split: configuration store and data storage are separate contracts;
  the two legacy methods with no callers (latest-reading lookup, manual pruning)
  are dropped in favor of the internal bounding guarantee (FR-010).

- **FR-013**: Concurrent use from multiple tasks (logging writes while history or
  status is read) MUST NOT corrupt stored data or return torn values.

- **FR-014**: No data is migrated from the legacy firmware (Arduino LittleFS
  history, wifi_config.json): first boot of the new firmware starts clean. Storage
  formats may diverge from legacy; every divergence is recorded in the parity
  checklist.

### Key Entities

- **Configuration item**: a named, typed setting with factory default and valid
  range; persisted individually in non-volatile configuration storage (16 KiB
  partition, ~378 usable entries — far above the ~10 items needed).
- **Sensor reading**: metric identifier + epoch timestamp + numeric value; metrics
  follow the legacy naming (env_temperature, env_humidity, env_pressure,
  soil_moisture, soil_temperature, soil_ph, soil_ec, optional soil_nitrogen/
  soil_phosphorus/soil_potassium).
- **Event record**: epoch timestamp + category + human-readable detail; new in this
  firmware generation.
- **Storage statistics**: total and used bytes of the data filesystem.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A fresh-flashed device reaches a fully configured state (all defaults
  active, data filesystem mounted and reporting usage) on first boot with zero
  manual steps.
- **SC-002**: 100% of accepted configuration changes survive an immediate power
  cycle ([HIL] verified on the bench rig).
- **SC-003**: Factory reset restores 100% of configuration items to their
  documented defaults and removes stored credentials ([HIL] verified).
- **SC-004**: After continuous simulated recording at default intervals to 10× the
  retention bound, history retrieval still succeeds and total data storage stays
  within its documented budget (host-tested).
- **SC-005**: The host test suite covers: every default value, set/get round-trips
  for every item, rejection of every documented out-of-range case, factory-reset
  semantics, history range filtering, bounded eviction, and event rotation — and
  runs green in CI alongside both board targets' builds (the host suite itself
  is board-independent and runs once).
- **SC-006**: Both board targets build green in CI including data-filesystem image
  creation.

## Assumptions

- The partition layout from PR-01 is authoritative: 16 KiB configuration (NVS)
  partition and 960 KiB data partition named `storage` (littlefs subtype) per
  `firmware/partitions.csv`; the PR-06 PRD wording "label littlefs" is understood
  as that partition (name `storage`).
- The pinned filesystem dependency (joltwallet/littlefs 1.22.1, already in
  `firmware/main/idf_component.yml`) is used as-is; no version change.
- ~870–900 KiB of the data partition remain for history + events after gzipped web
  assets and filesystem metadata (docs/partition-plan.md) — web assets themselves
  are PR-10's concern.
- The component/mock/host-test layout follows the pattern established by PR-02
  (interfaces component, header-only mocks under `testing/`, host test app on the
  linux preview target with failure-count exit code). If PR-06 merges before PR-02,
  this PR creates the shared interfaces component; otherwise it extends it.
- "Mode" in the PR-06 PRD maps to the persisted `wateringEnabled` flag; pump manual
  mode is runtime-only state (legacy parity) and is not persisted.
- AP-mode password for provisioning is a build-time option (PR-07 decision), not a
  stored configuration item.
- Timestamps are epoch seconds supplied by callers; time synchronization is PR-08's
  concern (parity checklist line 184: epoch-based and monotonic across migration).
- Event record categories listed in FR-011 follow the PR-08 PRD; PR-08 may extend
  the set without changing this PR's storage contract.
