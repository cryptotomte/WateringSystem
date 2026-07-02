# Feature Specification: Modbus Soil Sensor over RS485

**Feature Branch**: `004-modbus-soil-sensor`

**Created**: 2026-07-02

**Status**: Draft

**Input**: User description: "Replace the hand-rolled SP3485 Modbus client with esp-modbus
(pinned 2.1.2) behind IModbusClient/ISoilSensor interfaces, supporting both rev1 (manual DE
via RTS on GPIO 25) and rev2 (THVD1426 auto-direction, no DE pin) RS485 hardware, per
docs/prd/PR-04-modbus-soil-sensor.md (authoritative mini-PRD; ground truth for behavior is
docs/parity-checklist.md §5) plus hardware-driven requirements FW-2 and FW-4 from the rev2
design review 2026-07-02."

## Clarifications

### Session 2026-07-02

- Q: Are the legacy calibration commands (moisture/pH/EC factor from reference value,
  best-effort write to sensor registers 0x0100–0x0102 via function 0x06) in scope for
  PR-04, given the mini-PRD scope list omits them but `docs/parity-checklist.md` §5
  lists them under this sensor? → A: Include with exact legacy semantics (option A);
  factors held in memory this PR, store persistence wired when configuration
  consumers land (PR-09/PR-11). Confirmed by Paul at Checkpoint 1.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Soil readings on the bench rig (Priority: P1)

Paul flashes the rev1 rig, which has the real RS485 soil sensor wired to it. From the
serial console he triggers a soil reading and gets all seven used values (moisture,
temperature, EC, pH, N, P, K) with the same numbers the production Arduino unit shows
for the same sensor — including correct negative temperatures. A bus-level diagnostic
command shows raw transaction health for troubleshooting wiring.

**Why this priority**: Soil moisture is the primary input to every watering decision
(PR-11); a driver that reads the real sensor correctly on the rig is the core
deliverable of this PR and the second hardware-in-the-loop milestone of phase 1.

**Independent Test**: Flash the rig with the sensor attached, issue the soil diagnostic
command, compare each value against the Arduino unit's readings for the same probe.

**Acceptance Scenarios**:

1. **Given** the rig with the soil sensor connected, **When** the operator requests a
   soil reading, **Then** all nine registers are fetched in one transaction and the
   seven used values are reported with correct scaling (moisture ÷10 → %, temperature
   ÷10 → °C signed, EC ×1 µS/cm, pH ÷10, N/P/K ×1 mg/kg).
2. **Given** a sensor reporting a below-zero temperature, **When** the value is read,
   **Then** it is reported as the correct negative number (signed interpretation).
3. **Given** the rig, **When** the operator runs the bus diagnostic command, **Then**
   the output shows transaction success/failure and enough detail to distinguish "no
   response" from "bad response".

---

### User Story 2 - Sensor faults yield invalid data, never wrong data (Priority: P2)

A wire comes loose in the greenhouse (or the sensor dies). The system does not crash,
does not hang, and does not present stale or garbage numbers as truth: the reading is
flagged invalid with a logged error, and when the wire is reconnected the very next
read cycle recovers without a reboot. The downstream fail-safe logic (PR-11) can rely
on this validity signal to stop watering.

**Why this priority**: Constitution principle I (Safety First) — invalid sensor data
must stop the pumps in automatic mode. That fail-safe is only as good as the validity
flag this driver produces.

**Independent Test**: On the rig, disconnect the RS485 A/B pair mid-operation, observe
invalid readings + logged errors and no crash; reconnect and observe automatic recovery
on a subsequent read.

**Acceptance Scenarios**:

1. **Given** a connected sensor, **When** the A/B pair is disconnected and a read is
   attempted, **Then** the read fails with a timeout error after the configured
   response timeout (3000 ms default), the result is flagged invalid, and the failure
   is logged — no crash, no watchdog reset.
2. **Given** a disconnected sensor, **When** the pair is reconnected, **Then** the next
   read attempt succeeds without any manual intervention or restart.
3. **Given** a sensor response with an out-of-range value (moisture outside 0–100 %,
   temperature outside −40–80 °C, pH outside 3–9), **When** the reading is validated,
   **Then** the read fails with a distinct validation error and no partial values are
   presented as valid.
4. **Given** a failing read, **When** the driver reports the failure, **Then** exactly
   one bus attempt was made (no automatic retry — recovery comes from the caller's
   read cadence, parity with the Arduino client).
5. **Given** the sensor answers with a Modbus exception response, **When** the reply is
   parsed, **Then** it maps to a distinct error code (not a generic timeout).

---

### User Story 3 - One driver, two RS485 hardware generations (Priority: P3)

An AI developer (or Paul) builds the firmware for either board revision. On rev1 the
transceiver needs explicit transmit/receive direction control on a dedicated pin with
safe switching margins; on rev2 the transceiver switches direction automatically, has
no direction pin at all, and — because its receiver is always on — every transmitted
frame is heard back on the receive line. The same driver serves both: the board
abstraction decides direction handling, the rev2 build discards its own transmit echo
before parsing replies, and the receive line is kept from floating when the rev2
switched sensor power domain is off.

**Why this priority**: Dual-board support is the reason this driver is being rewritten
at all (the legacy client is rev1-only). The rev2-specific behaviors (echo, floating
RX) come straight from the 2026-07-02 hardware design review and must be encoded now so
rev2 bring-up (PR-14) is a validation exercise, not a rewrite.

**Independent Test**: Build both board variants in CI; verify the rev1 binary
configures the direction pin and the rev2 binary does not; verify by host test that
exactly one well-formed reply is parsed per transaction (single-reply parsing via
the mock — echo suppression itself is hardware receive-gating, verified
electrically at PR-14 per plan decision R3/FR-014).

**Acceptance Scenarios**:

1. **Given** a rev1 build, **When** the driver initializes, **Then** direction control
   uses the board-defined direction pin (GPIO 25) and transmitted frames complete
   without truncation at 9600 baud (direction-switch margins hold).
2. **Given** a rev2 build, **When** the driver initializes, **Then** no direction pin
   is configured or touched (the pin macro does not even exist — compile-time
   guarantee).
3. **Given** a rev2 build, **When** a request frame is transmitted, **Then** the echo
   of that frame never reaches the reply parser and only the sensor's reply is
   parsed — echo suppression is hardware receive-gating (RS485 half-duplex TX
   gating, plan decision R3), verified electrically at PR-14; host tests verify
   single-reply parsing (exactly one well-formed reply per transaction) via the
   mock (FR-014).
4. **Given** a rev2 board with the sensor power domain switched off, **When** the
   receive line would otherwise float, **Then** the internal pull-up on the RX pin
   keeps the line idle-high so no garbage bytes accumulate.

---

### User Story 4 - Sensor behavior testable without hardware (Priority: P4)

An AI developer changes decode, validation or error-handling logic and runs the host
test suite in CI. Register decoding (including signed temperature), range validation,
invalid-on-timeout behavior and single-reply parsing are verified against mock
implementations — no devkit, no sensor, failures block the merge.

**Why this priority**: Constitution principle II (Host-Testability). The mock soil
sensor created here is also the input PR-11's watering-controller tests build on.

**Independent Test**: Run the host test suite on a machine with no hardware attached;
decode/validation/timeout tests pass deterministically.

**Acceptance Scenarios**:

1. **Given** a mock Modbus client returning a known 9-register payload, **When** the
   sensor logic decodes it, **Then** every value matches the expected scaled result,
   including a negative temperature case.
2. **Given** a mock client that times out, **When** a reading is requested, **Then**
   the result is invalid with a timeout error and no stale values leak through.
3. **Given** a mock client returning out-of-range values, **When** the reading is
   validated, **Then** the read fails with a validation error.
4. **Given** CI on a clean checkout, **When** the host tests run, **Then** they
   complete without any hardware or device-specific environment.

---

### Edge Cases

- Spurious/noise bytes on the bus (e.g. from the rev2 sensor domain powering on or
  timing-offset garbage) must not permanently poison subsequent transactions: the next
  well-formed transaction succeeds. (The legacy client tolerates leading garbage by
  scanning for the address+function pattern; equivalent resilience is required.)
- Echo handling when the reply arrives back-to-back with the echo: the boundary between
  echoed request and real reply must not be misparsed.
- A reply shorter than expected (truncated frame) or with a bad CRC fails the read with
  an error — never a partially-decoded "valid" reading.
- Response timeout while bytes are still trickling in: the timeout window is extended
  while reception is in progress (parity), so a slow-but-arriving reply is not cut off.
- Registers 0x0007 (salinity) and 0x0008 (TDS) are read as part of the block but not
  exposed as values (parity: read but unused).
- Concurrent read requests (e.g. console diagnostic while a periodic read is active in
  a later PR): transactions must serialize on the bus, never interleave.
- Sensor availability probing must be a real bus read (parity), not a cached flag —
  otherwise a dead sensor looks alive.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Modbus client and soil sensor functionality MUST be exposed through
  hardware-independent interfaces (ported `IModbusClient`/`ISoilSensor`, pure C++, no
  hardware-SDK types) usable from host tests without any hardware headers.
- **FR-002**: The soil sensor driver MUST read device address 0x01 with function 0x03,
  one transaction of 9 holding registers starting at 0x0000, over the board-defined
  RS485 UART at 9600 baud 8N1. Pin source of truth is the board component (rev1:
  TX 16 / RX 17 / DE 25 per `docs/parity-checklist.md`; `docs/hardware.md` is known
  swapped — QUIRK 6).
- **FR-003**: Register decoding MUST apply the parity scaling map: 0x0000 moisture ÷10
  → % (moisture ≡ humidity for this sensor); 0x0001 temperature ÷10 → °C **signed**;
  0x0002 EC ×1 → µS/cm × calibration factor; 0x0003 pH ÷10 × calibration factor;
  0x0004–0x0006 N/P/K ×1 → mg/kg; 0x0007–0x0008 read but unused.
- **FR-004**: Range validation MUST fail the read (distinct error) when moisture is
  outside 0–100 %, temperature outside −40–80 °C, or pH outside 3–9. EC and N/P/K
  ranges are NOT enforced on read (parity with the Arduino validation).
- **FR-005**: Every reading MUST carry an explicit validity signal (valid data XOR
  error code) that downstream fail-safe logic (PR-11) can consume; a failed read MUST
  never present stale or partial values as valid.
- **FR-006**: The response timeout MUST default to 3000 ms and be configurable; the
  timeout window MUST be extended while response bytes are arriving. Exactly **one**
  bus attempt per call — the driver MUST NOT retry automatically (parity;
  `docs/parity-checklist.md` §5). Any future retry mechanism would be a deliberate
  behavior change, out of scope here.
- **FR-007**: Transmit/receive direction handling MUST be selected by the board
  component's `BOARD_HAS_RS485_DE` flag: rev1 drives the board-defined direction pin
  around each transmission such that frames complete without truncation at 9600 baud
  (the legacy 50 µs assert/release margins are the reference behavior); rev2 configures
  **no** direction pin, and rev2 builds MUST NOT reference a direction pin at all
  (compile-time enforced by the existing board sanity checks).
- **FR-008**: On boards without a direction pin (rev2), the driver MUST discard the
  echo of its own transmitted frame from the receive path before parsing the reply
  (THVD1426 receiver is always enabled; FW-4, rev2 design review 2026-07-02).
- **FR-009**: The internal pull-up on the RS485 RX pin MUST be enabled so the line does
  not float when the transceiver's receiver output is high-impedance (on rev2 the
  transceiver shuts down with the switched sensor power domain; FW-2, rev2 design
  review 2026-07-02). Enabling it unconditionally on both boards is acceptable — the
  receiver output drives through it.
- **FR-010**: Malformed traffic MUST degrade gracefully: bad CRC, truncated frames,
  Modbus exception responses (mapped to distinct error codes, parity: 100 + exception
  code) and spurious leading bytes each fail only the current transaction; the next
  well-formed transaction MUST succeed without reinitialization.
- **FR-011**: Sensor availability checks MUST perform an actual bus read (parity), not
  return cached state.
- **FR-012**: Calibration MUST follow parity: per-quantity calibration factors for
  moisture, pH and EC are computed locally from an operator-supplied reference value
  and best-effort written to sensor registers 0x0100–0x0102 (function 0x06, echo
  verified); a failed sensor write is non-fatal (the local factor still applies).
- **FR-013**: Serial diagnostics equivalent to the legacy `rs485test` and `soil`
  commands MUST exist on the rig console: trigger a raw bus test transaction and a
  full decoded soil reading, and expose the client's success/error statistics counters
  (parity) for troubleshooting.
- **FR-014**: Mock implementations of the Modbus client and soil sensor MUST exist for
  host tests, sufficient to deterministically test register decoding (incl. signed
  temperature), range validation, invalid-on-timeout, exception mapping and
  single-reply parsing (exactly one well-formed reply parsed per transaction — the
  host-observable equivalent of rev2 echo discarding, which is resolved at the
  hardware receive-gating layer per plan decision R3 and verified electrically at
  PR-14) in CI.
- **FR-015**: Both board targets MUST build green in CI — rev1 with the direction pin
  configured, rev2 without.

### Key Entities

- **Soil reading**: the seven used values (moisture %, temperature °C, EC µS/cm, pH,
  N/P/K mg/kg) plus validity/error state; produced atomically from one 9-register
  transaction.
- **Modbus transaction**: one request/response exchange with a single device — address,
  function, register window, outcome (success, timeout, CRC error, exception,
  validation failure), contributing to success/error statistics.
- **RS485 board profile**: the board component's UART pins and direction-control flag
  (`BOARD_HAS_RS485_DE`); the single source of truth the driver configures itself from.
- **Calibration factor**: per-quantity multiplier (moisture, pH, EC) derived from an
  operator reference value; applied locally on read, best-effort mirrored to the
  sensor.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Both board variants build green in CI from a clean checkout; the rev2
  binary provably contains no direction-pin handling (compile-time check).
- **SC-002**: On the rig, a soil reading returns all seven used values matching the
  Arduino unit's readings for the same sensor (same probe, same soil) on every
  attempt of the HIL checklist, including the signed-temperature spot check.
- **SC-003**: Disconnecting the A/B pair produces flagged-invalid readings and logged
  errors with zero crashes or watchdog resets; reconnection recovers automatically on
  a subsequent read with no operator action (HIL checklist).
- **SC-004**: Host test suite covering decode, validation, timeout and single-reply
  parsing (echo suppression itself is hardware receive-gating, verified
  electrically at PR-14 per plan decision R3/FR-014) runs in CI with zero hardware
  dependencies and passes deterministically (no flaky reruns).
- **SC-005**: Operator can run the bus diagnostic and soil reading commands on the
  first attempt using documented commands, and the output is sufficient to distinguish
  wiring faults from sensor faults (HIL checklist).

## Assumptions

- **Calibration is in scope** (confirmed at Checkpoint 1, see Clarifications):
  included with exact legacy semantics (local factor + best-effort sensor write) since
  `docs/parity-checklist.md` §5 lists the calibration commands as `[HOST]` items under
  this sensor and no other PR covers Modbus writes. Calibration factors are held in
  memory in this PR; persisting them via the configuration store is wired when
  configuration consumers land (PR-09/PR-11).
- **Read cadence stays out of scope**: the legacy 5 s periodic read loop is
  controller-level behavior (PR-11). This PR delivers on-demand reads (console
  diagnostics + API for later PRs); no periodic task is added.
- **rev2 electrical validation is deferred**: THVD1426 behavior at 9600 baud on real
  rev2 hardware (echo timing, auto-direction margins) is validated at PR-14 bring-up.
  This PR covers rev2 at build level and echo/pull-up logic at host-test level, per
  the mini-PRD's out-of-scope note.
- **The switched sensor power domain (`SENS_PWR_EN`) is not managed here**: rev2 power
  gating of the sensor rail belongs to the board/power layer (rev2 board profile,
  PR-14). This driver only guarantees it behaves correctly when the domain is off
  (pull-up, FR-009) and after it turns on.
- **esp-modbus internals may satisfy some requirements natively** (e.g. direction
  control via RTS, echo suppression in half-duplex mode): where the pinned component
  already provides a required behavior, verifying that behavior counts as
  implementing the requirement — the observable contract above is what is binding,
  not who implements it.
- The legacy client's tolerant response parsing (scanning the first bytes for the
  address+function pattern) is a means, not an end: the binding requirement is
  garbage-resilience per FR-010, achieved by whatever mechanism the new client stack
  provides.
