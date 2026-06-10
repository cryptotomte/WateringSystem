<!--
Sync Impact Report
- Version change: (template, unversioned) → 1.0.0 (initial ratification)
- Modified principles: n/a (initial adoption)
- Added sections: Core Principles (I–VI), Additional Constraints, Development Workflow, Governance
- Removed sections: none (template placeholders replaced)
- Templates requiring updates:
  ✅ .specify/templates/plan-template.md — generic Constitution Check gate, compatible as-is
  ✅ .specify/templates/spec-template.md — no constitution-specific tokens, compatible as-is
  ✅ .specify/templates/tasks-template.md — test-first task ordering aligns with Principle II, compatible as-is
- Follow-up TODOs: none
-->

# WateringSystem Constitution

## Core Principles

### I. Safety First (NON-NEGOTIABLE)

Pumps MUST be off at boot, after any reset (watchdog, brownout, panic), and across
OTA restarts — driving pump GPIOs to a safe OFF state is the first action of the
firmware. Invalid, stale, or missing sensor data in automatic mode MUST force a pump
stop. Manual mode is an explicit operator override and is the only path that bypasses
sensor-validity checks. All fail-safe behaviors are enumerated in
`docs/parity-checklist.md` and every one of them MUST be covered by host tests.
No feature, refactor, or optimization may weaken a fail-safe behavior; WiFi or
network failure MUST never affect watering safety.

### II. Host-Testability

All application logic — watering control, scheduling, safety conditions, reservoir
management — MUST be testable on the host against mock implementations. Hardware
access happens only behind the interface layer (`ISensor`, `IEnvironmentalSensor`,
`ISoilSensor`, `IActuator`, `IWaterPump`, `IModbusClient`, `IDataStorage`).
Code that touches IDF hardware APIs directly belongs in driver components that
implement these interfaces and contain no business logic. The host test suite runs
in CI on every push; watering logic and safety conditions target 100% host coverage.

### III. Reproducible Builds

The toolchain and every dependency are pinned: ESP-IDF v6.0.1 via the
`espressif/idf` Docker image in both local and CI builds, exact component versions
in `idf_component.yml` (with `dependencies.lock` tracked), and exact PlatformIO
platform/library versions on the legacy branch. CI MUST build all board targets
(`BOARD_REV1_DEVKIT`, `BOARD_REV2`) green from a clean checkout. A build that works
only on a developer machine does not exist; `idf.py build` in the pinned container
is the canonical build. Version bumps of toolchain or dependencies are deliberate,
reviewed changes — never floating ranges.

### IV. Frozen Legacy

The Arduino firmware (`src/`, `include/`, `data/`, `test/`, `platformio.ini`) runs
the production greenhouse unit and MUST NEVER be modified on `main` or on feature
branches. Legacy patches go exclusively through the `arduino-maintenance` branch
(via `git worktree`), are built by CI with pinned dependencies, and are tagged
`arduino-v2.3.x`. The `arduino-maintenance` branch is never merged into `main`;
the `arduino-final` tag is the permanent buildable reference.

### V. Checkpoint-Gated AI Workflow

Development is spec-kit driven: each PR derives from `docs/prd/PR-NN-*.md` and gets
`specs/NNN-*/` artifacts (spec, plan, tasks). Human checkpoints are mandatory stops:
CHECKPOINT 2 (plan approval before implementation) and CHECKPOINT 3 (review approval
before commit/PR) MUST NOT be auto-proceeded; CHECKPOINT 1 (clarify) stops only when
open questions exist. Code review (`pr-review-toolkit`) is never skipped. The
orchestrating session never writes implementation code directly — implementation is
delegated to subagents, which inherit the session model (`model: inherit`).
Merge commits always; squash merges are forbidden (they break spec-kit branch
chaining). One branch at a time; `git worktree` for true parallelism.

### VI. English Outward

Everything outward-facing is written in English: code, comments, commit messages,
PR titles and descriptions, issues, and documentation — this is a public repository.
This deliberately overrides any user-global Swedish-language conventions. Dialogue
with Paul (the project owner) remains in Swedish.

## Additional Constraints

- Target hardware: ESP32-WROOM-32E (4 MB flash); partition layout per
  `docs/partition-plan.md` (A/B OTA + NVS + littlefs) — changes to the partition
  table require an explicit migration plan.
- Language level: modern C++ on native ESP-IDF APIs; no Arduino compatibility
  layers in `firmware/`; `std::string` instead of Arduino `String`; RAII; include
  guards in the form `WATERINGSYSTEM_PATH_FILE_H`.
- Logging via `ESP_LOG*` with per-component tags; safety-relevant events (pump
  start/stop, failures, OTA) MUST be persisted.
- Board differences are expressed through the Kconfig board choice
  (`CONFIG_BOARD_REV1_DEVKIT` / `CONFIG_BOARD_REV2`) and the `board` component —
  never through scattered `#ifdef`s in application logic.
- License: AGPL-3.0-or-later. No secrets in the repository; configuration and
  credentials live in NVS on the device.

## Development Workflow

- Pipeline per PR: researcher (optional) → `speckit.specify` → `speckit.clarify`
  → CP1 → `speckit.plan` → `speckit.tasks` → `speckit.analyze` → CP2 →
  `speckit.implement` (via implementer subagent) → `pr-review-toolkit:review-pr`
  → CP3 → fixer (if findings) → verification re-review → commit/PR.
- Every implementation delivers a test checklist: host-test PRs are verified by CI;
  hardware-near PRs ship a HIL checklist that Paul executes on the bench rig at
  CHECKPOINT 3.
- `main` is protected: all changes via feature branch + PR with green CI.
- If review reveals the need for architectural change, return to `speckit.plan`
  rather than patching forward.

## Governance

This constitution supersedes all other practices in this repository, including
user-global instructions where they conflict (see Principle VI). Amendments are
made by PR that updates this file, must include a version bump per semantic
versioning (MAJOR: principle removal/redefinition; MINOR: new principle or
materially expanded guidance; PATCH: clarification), and require Paul's approval.
Every CHECKPOINT 2 plan review and CHECKPOINT 3 code review MUST verify compliance
with these principles; deviations require explicit, documented justification in the
plan's Complexity Tracking section. Runtime development guidance lives in
`CLAUDE.md` (root) and `firmware/CLAUDE.md`.

**Version**: 1.0.0 | **Ratified**: 2026-06-10 | **Last Amended**: 2026-06-10
