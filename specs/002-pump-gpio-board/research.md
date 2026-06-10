# Research: Pump Actuator Layer and Board Abstraction

**Feature**: 002-pump-gpio-board | **Date**: 2026-06-10

All decisions verified empirically inside the pinned `espressif/idf:v6.0.1` Docker
image where noted.

## D1. Host-test mechanism

- **Decision**: Separate IDF project `firmware/test_apps/host/` using the IDF
  **linux preview target** (`idf.py --preview set-target linux`) with the
  IDF-bundled **Unity** framework in plain runner style:
  `UNITY_BEGIN()` / `RUN_TEST()` / `std::exit(UNITY_END())` — exit code equals the
  failure count, directly usable as CI gate. Build output is a native executable
  (`build/<name>.elf`) that runs inside the same CI container.
- **Rationale**: Zero external packages and zero vendored code (constitution III);
  uses only IDF-bundled tooling; the pinned IDF version freezes any
  "experimental target" instability; keeps a path open for testing code that uses
  IDF APIs (log, NVS, esp_event are simulated on linux). Verified end-to-end in the
  v6.0.1 image: passing run → exit 0, failing assertion → exit 1.
- **Alternatives considered**: (a) Plain CMake host build vendoring Unity's three
  source files — simpler build, but vendors code unnecessarily; kept as documented
  fallback (~1 h migration) if a future IDF bump breaks the preview target.
  (b) pytest-embedded + `unity_run_menu()` — interactive/stdin-driven, adds Python
  test deps; rejected.
- **Caveat noted**: `esp_timer` is NOT simulated on the linux target in v6.0.1
  (mock-only, needs Ruby/CMock). Consequence: code under host test must never call
  `esp_timer` directly → time is injected (D3).
- **CI**: one extra job in `firmware-build.yml` via `espressif/esp-idf-ci-action`
  with `command: idf.py --preview set-target linux && idf.py build && ./build/<name>.elf`.

## D2. Actuator architecture — template method, logic in the base

- **Decision**: `WaterPump` (pure C++, implements `IWaterPump`) contains ALL timing
  and safety logic: timed-run state, max-runtime enforcement, runtime statistics,
  driven by polled `update()` and an injected time source. One pure virtual
  `applyOutput(bool on)` is the only hardware touchpoint. `GpioWaterPump` overrides
  it with `gpio_set_level` (active HIGH); `MockWaterPump` (host tests) overrides it
  with state recording. Interfaces live in a dedicated header-only component
  `firmware/components/interfaces/` (`IActuator.h`, `IWaterPump.h`,
  `ITimeProvider.h`) with no IDF dependencies.
- **Rationale**: Host tests exercise the REAL enforcement logic (not a parallel
  mock implementation) — constitution II; PR-11 reuses the same interfaces
  component for sensors/controller. Mirrors the Arduino class layout
  (`WaterPump.cpp` logic + GPIO) so the parity checklist maps 1:1.
- **Alternatives**: free-standing `PumpEngine` + composition — equivalent
  testability, one more type with no added value here; rejected for simplicity.
  Putting interfaces inside `actuators` — couples PR-03..05 to the actuator
  component; rejected.
- **Target-conditional build**: `GpioWaterPump.cpp` and the `esp_driver_gpio`
  dependency are excluded when `IDF_TARGET=linux` (CMake conditional in the
  component's `idf_component_register`).

## D3. Time source — injected monotonic milliseconds, polled enforcement

- **Decision**: `ITimeProvider::nowMs()` returning monotonic `int64_t` ms.
  Target implementation `EspTimeProvider` wraps `esp_timer_get_time()/1000`;
  host tests use `FakeTimeProvider` (manual advance). Max-runtime/timed-run
  enforcement happens in `WaterPump::update()`, polled from the main loop
  (and later from the controller task, PR-11).
- **Rationale**: Deterministic host tests (advance fake clock, assert state);
  `int64_t` µs→ms monotonic cannot wrap in device lifetime (edge case in spec
  satisfied by type choice); polled update matches the Arduino pattern
  (`WaterPump::update()`), keeping parity reasoning simple.
- **Alternatives**: `esp_timer` one-shot callbacks — introduces timer-task
  concurrency into a safety path and is not host-simulatable in v6.0.1; rejected.

## D4. Serial diagnostic — esp_console REPL

- **Decision**: IDF-bundled `console` component, UART REPL
  (`esp_console_new_repl_uart` + `esp_console_cmd_register`), prompt `ws>`,
  one command: `pump <plant|reservoir> <start <seconds>|stop|status>`.
  Lives in `firmware/main/diag_console.cpp`.
- **Rationale**: Bundled (constitution III), all-in-one setup, gives `help` for
  free; replaces the Arduino ad-hoc serial parser as the rig-testing tool (full
  FR12 diagnostics arrive in later phases). No mandatory Kconfig on classic ESP32.
- **Alternatives**: raw UART read loop (parity with Arduino) — more code, no
  completion/help; rejected.

## D5. Constants, not Kconfig (yet)

- **Decision**: Max runtimes are `constexpr` in the actuators component
  (`kMaxRunTimeMs = 300'000` per pump instance config); pin numbers and feature
  flags stay exclusively in `components/board`. No new Kconfig options.
- **Rationale**: Runtime configurability arrives with NVS config (PR-06);
  build-time board facts belong in board.h (FR-001); adding Kconfig now would
  create a third configuration source to reconcile later.

## D6. Mocks placement

- **Decision**: `MockWaterPump` and `FakeTimeProvider` are header-only under
  `firmware/components/actuators/include/actuators/testing/` — reusable by PR-11's
  host suite, zero cost on target (never included there).
- **Alternatives**: separate `mocks` component (more plumbing, no benefit now);
  test-app-local copies (blocks PR-11 reuse); both rejected.
