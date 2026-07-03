# Feature Specification: Level Sensors, Single-Pump Capability Flag and INA226 Power Telemetry

**Feature Branch**: `006-level-sensors-ina226`

**Created**: 2026-07-02

**Status**: Draft

**Input**: User description: "Level sensors (XKC-Y26) with board-configured polarity,
debounce and mock behind a level-sensor interface; introduce the
BOARD_HAS_RESERVOIR_PUMP capability flag (rev1=1, rev2=0, single-pump decision);
Ina226Sensor raw power readings on the shared I2C bus, rev2 only — per
docs/prd/PR-05-level-sensors-ina226.md (authoritative mini-PRD; behavior ground truth
docs/parity-checklist.md §3 lines 95–97) plus hardware-driven requirements FW-3 and
FW-5 from the rev2 design review 2026-07-02."

## Clarifications

### Session 2026-07-02

- Q: FW-3 scope split — how much of the switched sensor rail (`SENS_PWR_EN`, rev2
  IO25) lands in this PR? → A: Settle-gate only: the level-sensor abstraction gets
  a board-configured settle time (rev1 = 0 ms, rev2 = 500 ms) and reports
  not-yet-valid during the window; actual rail control (IO25 driving, keep-rail-on
  during watering runs) belongs to the rev2 board profile / PR-14 alongside
  FW-1/FW-6. Rev1 has no switched rail, so more could not be HIL-verified now
  anyway. Confirmed by Paul at Checkpoint 1.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Trustworthy water-level status on both boards (Priority: P1)

Paul (or the future controller) asks the system whether the reservoir has water at
the low mark and at the full mark. The answer is correct on both board revisions even
though the electrical polarity is opposite (rev1 reads the XKC-Y26 directly = active
HIGH; rev2 goes through an inverter = active LOW): the board configuration owns the
polarity, application code only ever sees logical "water present at this mark". Brief
electrical chatter at the water line does not flip the answer back and forth, and a
sensor that has just been powered (rev2 switched sensor rail) is not trusted until it
has had time to settle.

**Why this priority**: the level sensors gate every reservoir decision (PR-11's
fail-safe truth table) and FR5's bench-verified polarity is a phase-1 exit
requirement (`docs/parity-checklist.md` line 96: verify by measurement, not parity —
the legacy code reads active HIGH after the 2026-04-12 fix; the master PRD FR5
sentence claiming otherwise is stale and superseded by the checklist).

**Independent Test**: on the rig, dip/remove the sensors in water (or hand-trigger
them): console shows the correct logical state; polarity verified by bench
measurement and recorded in the parity checklist.

**Acceptance Scenarios**:

1. **Given** the rev1 rig with an XKC-Y26 on the low-level input, **When** water is
   present at the sensor, **Then** the system reports "water present" (GPIO reads
   HIGH; board configuration maps it to true) — and the measured polarity is
   recorded in `docs/parity-checklist.md` line 96 as that line requires.
2. **Given** a rev2 build, **When** the same firmware logic runs, **Then** the
   polarity mapping is inverted by board configuration alone (FW-5: 2N7002 inverter,
   water present = GPIO LOW) — no application-level `#ifdef`s.
3. **Given** water sloshing at exactly the sensor mark, **When** the raw input
   chatters, **Then** the reported logical state changes only after the input has
   been stable for the configured debounce window (deliberate divergence: legacy has
   no debounce).
4. **Given** a board with a switched sensor power rail (rev2), **When** the rail has
   just been enabled, **Then** level readings are reported as not-yet-valid until
   the board-configured settle time (FW-3: ≥500 ms) has elapsed.
5. **Given** the rig console, **When** the operator runs the level-status command,
   **Then** both sensors' logical state (and raw pin state for troubleshooting) are
   shown.

---

### User Story 2 - One firmware, one-pump and two-pump boards (Priority: P2)

An AI developer (or Paul) builds the firmware for rev2, which by final decision
(2026-06-10) is a single-pump node: there is no reservoir refill pump — the reservoir
is refilled manually until the central-reservoir unit exists. The rev2 binary
contains exactly one pump: no reservoir pump instance, no reservoir pump console
command, no dead reservoir-pump GPIO writes. The rev1 rig keeps both pumps and all
existing reservoir-pump behavior unchanged. Safety invariants hold on both: every
pump that exists on a board is forced OFF first thing at boot.

**Why this priority**: the capability flag is the load-bearing decision of this PR —
PR-09 (API), PR-11 (controller) and PR-14 (rev2 bring-up, "exactly one pump in
console/API") all build on it. Getting the compile-time gating right now prevents a
class of rev2 bugs later.

**Independent Test**: build both variants in CI; rev1 console has `pump reservoir`,
rev2 does not (compile-time absence, not a runtime error); both boards' existing
pump tests still pass.

**Acceptance Scenarios**:

1. **Given** a rev1 build, **When** the system boots, **Then** both pumps exist,
   both are forced OFF at boot, and `pump reservoir start/stop/status` works as
   before (no regression).
2. **Given** a rev2 build, **When** the firmware compiles, **Then** no reservoir
   pump instance, pin reference or console registration exists — the reservoir pump
   pin is deliberately undefined so any unguarded reference is a compile error
   (same enforcement pattern as the RS485 direction pin).
3. **Given** a rev2 build, **When** the operator lists pump status, **Then** exactly
   one pump is reported (PR-14 expectation).
4. **Given** either board, **When** the boot sequence runs, **Then** every pump that
   exists on that board is driven OFF before anything else (existing invariant,
   now capability-aware; QUIRK 2 target unchanged).

---

### User Story 3 - Pump power telemetry on rev2 (Priority: P3)

The rev2 board carries an INA226 current/voltage monitor on the pump's 12 V supply
(high-side 5 mΩ shunt). The firmware reads bus voltage, current and power as plain
values — visible on the console for bring-up and consumable by PR-09's API later. A
missing or unresponsive INA226 degrades gracefully (readings invalid, system runs
on); rev1 builds contain none of this code.

**Why this priority**: raw telemetry only — no protection logic (dry-run/blockage
detection is a future PRD), no on-hardware validation (PR-14). It rides along now
because it shares this PR's I2C infrastructure.

**Independent Test**: host tests drive the driver over a scripted bus (scaling math
against datasheet formulas); rev2 build compiles it, rev1 build excludes it;
absent-device behavior verified by host test (hardware validation deferred to
PR-14).

**Acceptance Scenarios**:

1. **Given** a scripted INA226 at address 0x40, **When** the driver reads it,
   **Then** bus voltage, current and power come back correctly scaled from the raw
   registers (shunt value and current resolution from build-time configuration;
   default 5 mΩ per the rev2 BOM).
2. **Given** a responding device with the wrong identity, **When** the driver
   initializes, **Then** the device is rejected and reported unavailable (identity
   check, mirroring the BME280 chip-ID pattern).
3. **Given** no device at 0x40 (rev2 board without the part, or a fault), **When**
   readings are requested, **Then** they are flagged invalid with a distinct error,
   nothing crashes, and a later-appearing device recovers on a subsequent attempt
   (lazy re-init, same contract family as the other sensors).
4. **Given** a rev1 build, **When** it compiles, **Then** no INA226 code or
   configuration is present (`BOARD_HAS_INA226` = 0).
5. **Given** the rev2 console, **When** the operator runs the power-telemetry
   command, **Then** voltage/current/power (or a distinct error) are shown — the
   PR-14 bring-up path.

---

### User Story 4 - Behavior testable without hardware (Priority: P4)

An AI developer changes debounce, polarity mapping, capability gating or INA226
scaling and runs the host suite in CI: level-sensor logic (polarity per board,
debounce, settle gating, fail-direction per board), INA226 register scaling and
error paths, and the mocks PR-11 will consume are all verified against scripted
inputs — no hardware, failures block merge.

**Why this priority**: Constitution II. The level-sensor mock created here is the
direct input to PR-11's reservoir truth-table tests (both-wet / low-only /
both-dry / invalid).

**Independent Test**: host suite passes on a hardware-less machine; the mock can
express every state PR-11's truth table needs, including the invalid combination.

**Acceptance Scenarios**:

1. **Given** scripted raw pin sequences, **When** the debounce logic runs, **Then**
   state changes only after the configured stability window, and the pre-first-
   stable-sample state is reported as not-yet-valid (never a guess).
2. **Given** both board polarity configurations, **When** the same logical scenario
   is scripted, **Then** both produce identical logical results (polarity is fully
   absorbed by configuration).
3. **Given** a disconnected-sensor simulation per board (rev1: pull-up reads HIGH =
   "water present" → fill pump would stay off; rev2: pull-up reads HIGH = "water
   absent" → drawing node would not pump), **When** the fail-direction tests run,
   **Then** each board's documented fail-safe direction is pinned as a host-tested
   truth (checklist line 97).
4. **Given** the INA226 datasheet scaling formulas, **When** the conversion math
   runs on scripted register values, **Then** results match reference calculations
   (including the 5 mΩ / ~0.5 mA-LSB rev2 operating point).
5. **Given** the level-sensor mock, **When** PR-11-style consumer tests script the
   four truth-table states, **Then** all four (incl. low-dry+high-wet = invalid)
   are expressible with coherent validity.

---

### Edge Cases

- Raw input chattering exactly at the debounce boundary: state must resolve
  deterministically (stability window restarts on every flip); no oscillating
  reported state.
- Settle-time gating (FW-3): a read during the settle window is "not yet valid" —
  distinct from "water absent" (an early XKC-Y26 sample falsely reads absent; the
  gate exists to prevent exactly that misread).
- The invalid combination low=dry + high=wet is REPORTED as-is (both sensors'
  states with validity); interpreting it is PR-11's job — this layer never masks it.
- Sensor disconnected mid-operation: reported state follows the board's documented
  fail direction (pull-ups); no crash, no flapping (debounce applies).
- INA226 present but bus contention with BME280/soil reads: transactions serialize
  on the shared bus (established bus contract); neither sensor's cadence breaks.
- INA226 negative current (register is signed): reported correctly signed, not
  wrapped — the pump should never regenerate, but the math must not lie.
- Reservoir pump commands on a rev2 build: absent at compile time — not a runtime
  "unknown command" surprise for PR-14's "exactly one pump" check.
- Boot with the sensor rail off (rev2, future rail control): level readings remain
  not-yet-valid rather than "absent" — fail-safe direction for a drawing node.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: Level-sensor functionality MUST be exposed through a
  hardware-independent interface (pure C++, host-includable) reporting, per sensor
  (low mark, high mark): logical water-present state, raw input state (diagnostics),
  and an explicit validity signal (not-yet-valid before the first debounced sample
  and during settle gating). Interface conventions follow the established sensor
  contracts (consumers gate on validity; unsynchronized by design with the Locked
  decorator pattern for cross-task access).
- **FR-002**: Logical polarity MUST be owned by the board configuration (FW-5):
  rev1 = active HIGH (XKC-Y26 direct through non-inverting level shifter), rev2 =
  active LOW (2N7002 inverter). Application code MUST contain no board conditionals
  for polarity. Ground truth for verification is `docs/parity-checklist.md`
  lines 95–97 — NOT the stale master-PRD FR5 sentence (superseded).
- **FR-003**: Level inputs MUST be sampled by polling with debounce: the reported
  logical state changes only after the raw input has been stable for a configured
  window (board-tunable; deliberate divergence — legacy reads bare pins with no
  filtering). Before the first stable sample the state is not-yet-valid.
- **FR-004**: Readings MUST support settle-time gating (FW-3): a board-configured
  time after sensor-power-on during which readings report not-yet-valid (rev2:
  ≥500 ms per XKC-Y26 response time; rev1: 0 — sensors are permanently powered).
  Actual power-rail control (`SENS_PWR_EN`, rev2 IO25) is OUT of scope — rev2
  board profile / PR-14 (CP1 decision, see Clarifications).
- **FR-005**: Pull configuration MUST follow the board: rev1 uses internal pull-ups
  (parity, checklist line 95); rev2 has external pull-ups — internal ones stay
  enabled on both (redundant-but-harmless, per the rev2 design notes). The
  fail direction per board (disconnected sensor: rev1 reads "water present",
  rev2 reads "water absent" — both fail safe for their pump topology) MUST be
  documented and host-tested (checklist line 97).
- **FR-006**: The board component MUST expose a `BOARD_HAS_RESERVOIR_PUMP`
  capability flag: 1 on rev1, 0 on rev2 (single-pump decision, master PRD FR4,
  final 2026-06-10). On boards with flag 0 the reservoir pump pin MUST be
  deliberately undefined so unguarded references fail at compile time (existing
  RS485-DE enforcement pattern), and existing board sanity checks MUST be
  flag-guarded accordingly.
- **FR-007**: All reservoir-pump wiring MUST be capability-gated: instance
  creation, boot force-OFF, and console registration exist only when
  `BOARD_HAS_RESERVOIR_PUMP` = 1. The boot fail-safe invariant (every existing
  pump driven OFF first) MUST remain intact on both boards. Rev1 behavior is
  unchanged (no regression); reservoir feature flags remain non-persisted (parity).
- **FR-008**: An INA226 driver MUST provide bus voltage, current and power as plain
  readings over the shared I2C bus instance (established bus-sharing contract —
  never a second bus), device address 0x40 (rev2 pump monitor, A0=A1=GND), with
  conversion per the vendor datasheet: calibration derived from the
  build-configurable shunt resistance (default 5 mΩ per the rev2 BOM) and current
  resolution; 16-bit big-endian registers.
- **FR-009**: The INA226 driver MUST verify device identity at initialization
  (manufacturer/die ID registers) and reject foreign devices (deliberate
  divergence, mirrors the BME280 chip-ID check).
- **FR-010**: INA226 absence or failure MUST degrade gracefully: readings carry an
  explicit validity signal with distinct errors (not-found vs read-failed, same
  error-code family as the other sensors), no crash, lazy re-probe recovery on
  later attempts. Alert/limit functionality is OUT of scope (ALERT pin is
  unconnected on rev2).
- **FR-011**: INA226 code MUST be compiled out on boards with `BOARD_HAS_INA226` =
  0 (rev1); both board targets MUST build green in CI.
- **FR-012**: Serial console diagnostics MUST exist following the established
  thin-wrapper pattern: a level-status command (both sensors: logical state, raw
  state, validity) on both boards, and a power-telemetry command (voltage, current,
  power or distinct error) on INA226-equipped boards — the HIL/bring-up
  verification paths (PR-14 relies on the latter).
- **FR-013**: Mocks MUST exist for host tests: a level-sensor mock expressive
  enough for PR-11's reservoir truth table (both-wet / low-only / both-dry /
  invalid combination, with validity control and consistency helpers per the
  established mock conventions) and a scripted-bus path for INA226 driver tests
  (16-bit register support in the mock bus as needed).
- **FR-014**: Where the existing I2C bus seam cannot express INA226 transactions
  (16-bit register writes are pointer + two data bytes in ONE transaction — not
  composable from single-byte writes), the seam MUST be extended consistently
  across the interface, the hardware implementation and the mock (the PR-03
  contract explicitly delegates this decision to this PR).

### Key Entities

- **Level reading**: per-sensor logical water-present state + raw input state +
  validity (not-yet-valid during debounce warm-up/settle window); produced by
  polling, consumed by console now, PR-11's controller later.
- **Board capability set**: `BOARD_HAS_RESERVOIR_PUMP` (new, rev1=1/rev2=0),
  `BOARD_HAS_INA226` (existing, rev1=0/rev2=1), level-sensor polarity/settle/pull
  parameters — the single source of truth application code configures itself from.
- **Power reading**: bus voltage, current (signed), power derived from INA226
  registers via shunt/current-LSB calibration; validity + error state; rev2 only.
- **I2C register seam**: the shared bus abstraction from PR-03, possibly extended
  for 16-bit transactions; one instance, BME280 + INA226 as peers.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Both board targets build green in CI from clean checkout; the rev2
  binary provably contains no reservoir pump (compile-time absence) and the rev1
  binary no INA226 code.
- **SC-002**: Host suite covers: polarity mapping equivalence across both board
  configs, debounce boundary behavior, not-yet-valid gating (warm-up + settle),
  per-board fail-direction truths, INA226 scaling against datasheet reference
  calculations, INA226 identity/absent/recovery paths, and mock coherence — zero
  hardware, deterministic passes.
- **SC-003**: On the rev1 rig, the level-status console command reports correct
  logical states for wet/dry at both marks on first attempt, and the measured
  polarity is recorded in `docs/parity-checklist.md` line 96 (FR5 bench-verification
  task closed for rev1).
- **SC-004**: Existing rev1 pump behavior unchanged: all pre-existing pump host
  tests and console commands pass unmodified (regression guard).
- **SC-005**: Chatter at a level mark (hand-simulated on the rig) produces no
  oscillating state at the console — exactly one transition per real state change.
- **SC-006**: PR-11 can consume the level-sensor mock for all four truth-table
  states without modification (verified by a representative consumer-style host
  test in this PR).

## Assumptions

- **Polling, not interrupts**: level inputs are polled (legacy pattern; PR-11's
  controller is polling-based; debounce needs periodic sampling anyway). Polling
  cadence is an implementation choice bounded by the debounce window.
- **Debounce default**: state accepted after stability across the configured window
  (default on the order of a few hundred ms — slow physical process; exact value
  fixed in plan, board-tunable). Legacy has none — documented divergence
  (parity-checklist §6 entry).
- **Console surface**: `level` on both boards; power telemetry command named in
  plan (rev2/`BOARD_HAS_INA226` builds only). Grammar follows the existing
  serial-diagnostic contract style.
- **INA226 reads on demand** (console now, PR-09 API later): the device runs in
  continuous-conversion mode configured at init; no periodic telemetry task in this
  PR; no data-storage logging (PR-09 territory). Averaging/conversion-time settings
  are plan-level choices documented against the datasheet.
- **Only the pump INA226 (0x40) is in scope** — the DNP solar INA226 (0x41) is
  future power-track work; the I2C address map (0x40 pump, 0x41 solar reserved,
  0x76/0x77 BME280) is recorded in the board component for collision safety.
- **FW-1 (VBAT ADC) and FW-6 (IO35 input-only) are NOT this PR** — bookmarked for
  the rev2 board profile / PR-14 (power-block telemetry).
- **Kconfig scope**: shunt resistance (mΩ, default 5) is the build-time
  configuration; current LSB derived. Placement (project vs component Kconfig) is a
  plan decision.
- **Interface shape** (per-sensor `ILevelSensor` × 2 vs a two-sensor aggregate) is
  a plan decision; the spec's binding contract is per-sensor state + validity as in
  FR-001.
- **Hardware-source caveat**: rev2 pin/part facts come from the in-flight hardware
  track (`docs/rev2-firmware-notes.md`, `hardware/rev2/design-notes/` — untracked
  on main, provisional until SYNC1); INA226 on-hardware validation is deferred to
  PR-14 by the mini-PRD.
