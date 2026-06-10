# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

WateringSystem is an ESP32-based automated greenhouse watering system. The repository contains **two firmware tracks**:

1. **ESP-IDF firmware** (`firmware/`) — the NEW firmware under active development. All current and future work happens here. See `firmware/CLAUDE.md` for build details, component structure, and code conventions.
2. **Arduino legacy firmware** (`src/`, `include/`, `data/`, `test/`, `platformio.ini`) — the FROZEN v2.3 firmware that still runs the production greenhouse unit. See the frozen-code rules below.

Key documents:

- **Master PRD:** `docs/PRD-esp-idf-migration.md` — scope, requirements (FR1–FR13), phase plan.
- **PR breakdown + dependency graph:** `docs/prd/` — one `PR-NN-*.md` per planned PR.
- **Parity contract:** `docs/parity-checklist.md` — behavior the new firmware must match.
- **Partition plan:** `docs/partition-plan.md` — flash layout calculation (A/B OTA + NVS + littlefs).
- **Constitution:** `.specify/memory/constitution.md` — project principles for spec-kit.

## FROZEN Legacy Code — Read This First

`src/`, `include/`, `data/`, `test/`, and `platformio.ini` **MUST NEVER be modified on `main` or on feature branches**. The production greenhouse unit runs this exact code. Treat these paths as read-only reference material.

If a legacy bug must be patched, it goes ONLY through the `arduino-maintenance` branch, worked in a separate worktree:

```bash
git worktree add ../WateringSystem-arduino arduino-maintenance
# fix → build via CI (pinned PlatformIO deps) → deploy → tag arduino-v2.3.x
```

- **Never merge `arduino-maintenance` into `main`.**
- Tag `arduino-final` is the permanent, buildable reference of the legacy firmware (`git checkout arduino-final` must always build).

## Language Policy

**English for everything outward-facing** — code, comments, commit messages, PR titles and descriptions, issues, and documentation. The repository is public; this overrides the user-global rule about Swedish commit messages. Dialogue with Paul (chat) stays in Swedish.

## Development Workflow

Development is spec-kit driven and orchestrated with subagents. Each PR gets a `specs/NNN-*/` directory (spec, plan, tasks, checklists) generated from its `docs/prd/PR-NN-*.md`. Both `specs/` and `docs/prd/` are **versioned in git** — never gitignore them.

### Phases

1. `researcher` agent — optional codebase/docs exploration before specifying.
2. `speckit.specify` — generate the feature spec from the PR description.
3. `speckit.clarify` — resolve underspecified areas.
4. **CHECKPOINT 1** — stop only if open questions remain after clarify; otherwise pass silently.
5. `speckit.plan` → `speckit.tasks` → `speckit.analyze`.
6. **CHECKPOINT 2** — ALWAYS stop. Present plan + tasks and wait for explicit approval before implementing.
7. `speckit.implement` via the `implementer` agent. The orchestrator NEVER writes implementation code in the main session.
8. `pr-review-toolkit:review-pr` — run all review agents.
9. **CHECKPOINT 3** — ALWAYS stop. Present review findings. Fixes go through the `fixer` agent followed by a verification re-review. Commit/PR only after approval.

### Workflow Rules

- The review step is never skipped.
- If review uncovers an architectural problem, go back to `speckit.plan` — do not patch around it.
- Checkpoints 2 and 3 are mandatory stops; never auto-proceed past them.
- Checkpoint 1 passes silently when clarify produced no open questions.
- The implementer always delivers a test checklist with its work:
  - **Host-test PRs** (logic, parsing, controllers) are verified by the CI host-test suite.
  - **Hardware-near PRs** (drivers, RS485, GPIO) get a HIL checklist that Paul runs on the bench rig (rev1 devkit) at Checkpoint 3.

### Subagent Model Policy

Subagents (`researcher`, `implementer`, `fixer`) inherit the main session's model — `model: inherit` in `.claude/agents/*.md`. The model chosen for the session drives the entire flow; no agent pins a specific model.

## Git Strategy

- **Merge commits ALWAYS — never squash.** Squash merges break spec-kit branch chaining.
- Branch naming: `NNN-short-name` for spec-kit features (e.g. `001-phase0-foundation`); `chore/...`, `fix/...` prefixes otherwise.
- **One branch at a time.** Never switch branches with uncommitted changes; commit immediately after implementation completes.
- Use `git worktree` for true parallelism. Never use `git checkout -- .` or `git clean -fd` as "cleanup" — they destroy other agents' work.
- `main` is protected: all changes go through a feature branch + PR.

## Build Commands

### ESP-IDF firmware (active)

```bash
docker run --rm -v "$PWD/firmware":/project -w /project espressif/idf:v6.0.1 idf.py build
```

Board selection (Kconfig: `BOARD_REV1_DEVKIT` / `BOARD_REV2`), flashing, host tests, and all other build details are documented in `firmware/CLAUDE.md`.

### Arduino legacy (frozen)

Built by CI only, on the `arduino-maintenance` branch (pinned PlatformIO platform + library versions). Do not build it from `main`.

## Hardware Summary

Two board revisions, abstracted via Kconfig in `firmware/components/board/`:

- **rev1 — devkit rig:** ESP32 devkit + RS485 transceiver behind a TXS0108E level shifter (manual DE direction control), BME280 on I2C, pump MOSFETs on GPIO. This is the bench rig for phases 1–5. (The rev1 RS485 module's actual IC is an ADM3485ARZ on a MikroE RS485 5 Click; "SP3485" survives only as a legacy class name in the Arduino code.)
- **rev2 — custom PCB:** THVD1426 auto-direction RS485 (no DE pin), INA226 current/voltage monitoring per pump, CP2102N USB-C serial, JTAG header.

Pin tables live in `firmware/components/board/` — do not duplicate them elsewhere. Source of truth for the rev1 pins is `docs/parity-checklist.md` (extracted from `src/main.cpp`); note that `docs/hardware.md` lists RS485 TX/RX swapped (checklist QUIRK 6).

**Level sensor polarity (FR5):** the XKC-Y26 output is active HIGH (water present = HIGH). On rev1 (non-inverting TXS0108E path) the GPIO is active HIGH; on rev2 (2N7002 inverter) the GPIO is active LOW. The Arduino code on this branch already reads active HIGH (`src/main.cpp:504-506`; the 2026-04-12 fix is merged — PRD FR5's "reads active LOW" describes the pre-fix state). Target is board-configured polarity, verified by bench measurement in phase 1; see `docs/parity-checklist.md` for the verified facts.

## Safety Invariants

Pumps are always OFF at boot, after watchdog reset, and across OTA restarts. Invalid sensor data in automatic mode stops the pumps (fail-safe); manual mode is an explicit override. These behaviors are part of the parity contract and must be host-tested.
