# Research: Modbus Soil Sensor over RS485 (004)

**Date**: 2026-07-02 | **Spec**: [spec.md](spec.md)

All NEEDS CLARIFICATION items from Technical Context are resolved here. Sources:
esp-modbus docs (context7, `/espressif/esp-modbus`), legacy client
(`src/communication/SP3485ModbusClient.cpp`, read-only reference), parity contract
(`docs/parity-checklist.md` §5), rev2 design review 2026-07-02 (FW-2/FW-4).

## R1: esp-modbus API generation — 2.x object API

**Decision**: Use the esp-modbus 2.x handle-based API: `mbc_master_create_serial()`
with `mb_communication_info_t.ser_opts` (port, `MB_RTU`, 9600 baud,
`UART_DATA_8_BITS`, `UART_STOP_BITS_1`, `MB_PARITY_NONE`,
`response_tout_ms = 3000`), then `mbc_master_send_request()` with
`mb_param_request_t` for raw register access. No CID/data-dictionary layer.

**Rationale**: Version pin 2.1.2 is mandated by the PRD. The request-based API maps
1:1 onto the ported `IModbusClient` (`readHoldingRegisters` → command 0x03,
`writeSingleRegister` → command 0x06) without inventing a parameter dictionary the
system doesn't need — the soil sensor is one device with one fixed register window.
`mbc_master_send_request` returns `ESP_ERR_TIMEOUT` on no-response, distinct codes
for invalid responses, satisfying FR-005/FR-010 error discrimination.

**Alternatives considered**:
- *CID data dictionary (`mbc_master_get_parameter`)*: adds a static descriptor table
  and float conversion machinery for no benefit — decode/scaling must live in
  host-testable `ModbusSoilSensor` logic anyway (parity scaling is bespoke).
- *Raw UART port of the legacy client*: rejected by the PRD (esp-modbus is the point
  of this PR); would re-own CRC/framing/timing code that esp-modbus already provides.

## R2: RS485 direction control — UART half-duplex mode on both boards

**Decision**: Configure the Modbus UART in `UART_MODE_RS485_HALF_DUPLEX` on **both**
boards, differing only in the RTS pin wiring:
- rev1 (`BOARD_HAS_RS485_DE == 1`): `uart_set_pin(..., rts = BOARD_PIN_RS485_DE)` —
  the UART peripheral drives DE via RTS automatically around each frame.
- rev2 (`BOARD_HAS_RS485_DE == 0`): `uart_set_pin(..., rts = UART_PIN_NO_CHANGE)` —
  no direction pin exists; the THVD1426 auto-directs.

Call order per the pinned esp-modbus serial-master example: create → `uart_set_pin`
→ start → `uart_set_mode(UART_MODE_RS485_HALF_DUPLEX)`.

**Rationale**: Satisfies FR-007 with zero application-level `#ifdef` logic beyond the
pin selection (constitution: board differences only via the board component). The
`#if BOARD_HAS_RS485_DE` guard lives in one place in `EspModbusClient`; rev2 builds
never reference `BOARD_PIN_RS485_DE` (it is undefined there — compile-time enforced
by the existing board.h sanity checks).

**Risk (HIL-verified)**: hardware-timed RTS switches within microseconds of frame
start/end, whereas the legacy driver gave the TXS0108E-shifted DE a 50 µs (2×
applied = 100 µs) assert margin and 50 µs release margin. At 9600 baud one bit is
104 µs, and the TXS0108E propagates in nanoseconds, so margins should hold — but
this is exactly the parity-checklist §5 open HIL item ("frames still complete
without truncation"). If HIL shows truncation on rev1, fallback is manual DE via a
GPIO toggled around `uart_wait_tx_done()` (legacy-equivalent timing), still behind
`IModbusClient`.

**Alternatives considered**: manual GPIO DE control as primary (rejected: reproduces
the code esp-modbus/UART hardware already provides and adds a race-prone task-timing
dependency); `UART_MODE_RS485_APP_CTRL` (rejected: lowest-level option, no need).

## R3: rev2 TX echo (FW-4) — suppressed by half-duplex mode, verified at HIL

**Decision**: Rely on `UART_MODE_RS485_HALF_DUPLEX` receive gating for echo removal:
in this mode the ESP32 UART ignores the receive path while the transmitter is
active, so the echo the THVD1426's always-on receiver (RE̅ grounded) feeds back
during transmission never reaches the driver. No application-level echo scrubber is
written up front. A rev2 HIL checklist item (deferred to PR-14 with the rest of
rev2 electrical validation, per spec assumption) verifies no echo bytes leak; the
esp-modbus RTU state machine's T3.5 frame resynchronization is the second line of
defense against residual tail bytes.

**Rationale**: The echo is physically simultaneous with transmission (transceiver
loopback, not a store-and-forward reflection), so TX-gated receive removal is
complete by construction. Writing a speculative echo-scrubber above esp-modbus would
be dead code below our own test boundary — and FR-008's observable contract is
"only the sensor's reply is parsed", not "an echo scrubber exists".

**Spec note (FR-014)**: this resolves "echo discarding" at a layer beneath
host-testable code; host tests therefore cover decode/validation/timeout/exception
paths against the mock client, and echo correctness is a HIL concern (PR-14). The
spec's FR-014 echo-discard mention is satisfied by testing that the sensor logic
parses exactly one well-formed reply per transaction from the mock — the
host-observable equivalent.

## R4: RX pull-up (FW-2) — unconditional `gpio_pullup_en` on the RX pin

**Decision**: After UART pin setup, enable the internal pull-up on
`BOARD_PIN_RS485_RX` (IO17) unconditionally on both boards, in `EspModbusClient`
initialization.

**Rationale**: FR-009. On rev2 the THVD1426 SHDN̅ tracks `SENS_PWR_EN`; RO goes hi-Z
when the sensor domain is off and IO17 would otherwise float into the UART RX,
producing garbage bytes/noise. The transceiver's RO output drives through a weak
pull-up when active, so leaving it enabled permanently is harmless on both boards
(rev1's ADM3485 RO likewise drives push-pull). Doing it in the client (not the board
component) keeps the board component a pure pin/flag table, per PR-02's design.

## R5: Timeout semantics — `response_tout_ms = 3000`, parity equivalence documented

**Decision**: Set `ser_opts.response_tout_ms = 3000` at create time and route
`IModbusClient::setTimeout()` to the esp-modbus runtime timeout setter if 2.1.2
exposes one; if it does not, `setTimeout` is honored at initialization only and
documented as such (implementer verifies against the pinned component; the console
never changes the timeout at runtime today). Verify that Kconfig
`CONFIG_FMB_MASTER_TIMEOUT_MS_RESPOND`'s upper bound does not clamp 3000 in
`sdkconfig.defaults`.

**Rationale**: FR-006 requires a 3000 ms default (parity). The legacy
"timeout extended while bytes arrive" behavior is provided equivalently by
esp-modbus's event-driven receive path (inter-character T1.5 / frame T3.5 timers
mean a trickling frame is not cut off mid-reception); the observable contract —
slow-but-arriving replies survive, absent replies fail at ~3000 ms — holds. Exactly
one bus attempt per call: esp-modbus master performs no application-level retry for
`send_request`, matching the no-retry parity rule; the implementer MUST NOT add
retry loops.

## R6: Error-code mapping — esp_err_t → legacy-shaped error codes

**Decision**: `EspModbusClient` maps esp-modbus results onto the `IModbusClient`
error contract: 0 = OK, distinct codes for timeout (`ESP_ERR_TIMEOUT`), invalid
response/CRC (`ESP_ERR_INVALID_RESP`-class), and Modbus slave exceptions. Slave
exceptions map to the legacy 100+n range when the underlying API surfaces the
exception code; if 2.1.2 reports exceptions only as a generic invalid-response
error, they map to a single documented "slave exception/invalid response" code.

**Rationale**: FR-010's binding requirement is *distinct from timeout* (fail-safe
diagnostics need to distinguish "sensor absent" from "sensor confused"). The legacy
100+exception granularity is preserved when the API allows; otherwise the coarser
mapping is recorded as a parity divergence in the PR (same mechanism as PR-06's
documented divergences). Statistics counters (FR-013) are counted in
`EspModbusClient` exactly like the legacy client: one success or one error per call.

## R7: Component layout & decode placement — new `sensors` component, pure logic base

**Decision**: Follow the PR-02 actuators pattern:
- `firmware/components/interfaces/include/interfaces/`: `IModbusClient.h`,
  `ISoilSensor.h` (ported, pure C++, host-includable; drop the legacy `ISensor`
  Arduino heritage in favor of the minimal base the new codebase needs).
- New `firmware/components/sensors/`: `ModbusSoilSensor` (pure logic: register
  decode incl. signed temperature, scaling, range validation, calibration factors,
  availability probe — depends only on `IModbusClient`), `LockedSoilSensor`
  (mutex decorator, REPL + future controller access), `testing/MockModbusClient.h`
  + `testing/MockSoilSensor.h`.
- `EspModbusClient` (the only file touching esp-modbus/UART/GPIO APIs) in the same
  `sensors` component but excluded from the linux/host build via the same
  CMake target-guard used by `storage` for esp_littlefs.
- esp-modbus dependency pinned `espressif/esp-modbus==2.1.2` in the component's
  `idf_component.yml`; `dependencies.lock` updated deliberately (constitution III).

**Rationale**: Constitution II — everything above `IModbusClient` is host-testable
by construction; the hardware-touching client contains no business logic. The
decode/validation/calibration logic is exactly what PR-11's fail-safe tests consume.

**Alternatives considered**: separate `modbus` component for the client (rejected
for now: one consumer; can be split when the level sensors/INA226 PR needs shared
bus infrastructure — noted for PR-05).

## R8: Calibration semantics (CP1 answer A) — legacy-exact

**Decision**: Port legacy behavior verbatim: `calibrateMoisture/PH/EC(reference)`
compute `factor = reference / rawReading` from a fresh read, apply the factor
locally to subsequent reads (EC and pH per parity map; moisture factor exists in
legacy and is ported as-is), and best-effort write the factor (×100, per legacy
encoding) to sensor registers 0x0100/0x0101/0x0102 via function 0x06 with echo
verification — a failed write logs a warning and returns success for the local
part (non-fatal, parity). Factors live in memory; persistence is wired when
configuration consumers land (PR-09/PR-11).

**Rationale**: CP1 decision (option A), parity checklist §5. Implementer ports the
exact legacy formula from `src/sensors/ModbusSoilSensor.cpp:207-322` (read-only
reference) rather than re-deriving it.

## R9: Console diagnostics — extend `diag_console.cpp`

**Decision**: Add `soil` (full decoded reading + validity/error), `rs485test` (raw
single-transaction bus probe + statistics counters dump) to the existing esp-console
REPL in `firmware/main/diag_console.cpp`, following the command style PR-02/PR-06
established. Calibration console commands (`soil_cal_moisture <ref>` etc.) included
since calibration is in scope.

**Rationale**: FR-013; same HIL surface Paul already uses on the rig. Access to the
shared sensor goes through `LockedSoilSensor` (REPL vs future main-loop, the PR-02
race lesson).
