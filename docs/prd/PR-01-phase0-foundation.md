# PR-01: phase0-foundation

> Phase 0 — written retrospectively; this is the PR that introduces this breakdown.

## Goal

Stand up the ESP-IDF firmware skeleton, reproducible Docker/CI builds for both board
targets, the AI development workflow (CLAUDE.md, constitution, spec-kit), and freeze the
legacy Arduino firmware as a forever-buildable reference.

## Scope

- `firmware/` ESP-IDF project skeleton: CMake project, `main/` + `components/`
  structure (header-only `board` component), minimal `app_main` with log banner and
  **pumps-off-at-boot GPIO init** (fail-safe from day 1).
- Kconfig board selection: `CONFIG_BOARD_REV1_DEVKIT` / `CONFIG_BOARD_REV2` via
  `choice` in `main/Kconfig.projbuild`; per-board sdkconfig overlays
  (`sdkconfig.board.rev1_devkit`, `sdkconfig.board.rev2`).
- Pinned toolchain and dependencies: ESP-IDF **v6.0.1** (Dockerfile + CI),
  `espressif/esp-modbus==2.1.2`, `joltwallet/littlefs==1.22.1`.
- `partitions.csv` (4 MB): nvs 16 K, otadata 8 K, phy_init 4 K, 2 × app à 1.5 MB
  (ota_0/ota_1), littlefs 960 K — documented in `docs/partition-plan.md`.
- `firmware/Dockerfile` (`FROM espressif/idf:v6.0.1`) + build documentation.
- `.github/workflows/firmware-build.yml`: matrix build over both boards via
  `espressif/esp-idf-ci-action@v1`, firmware artifacts per board.
- New root `CLAUDE.md` (workflow, checkpoints, FROZEN marking of Arduino tree) and
  `firmware/CLAUDE.md` (build commands, conventions, test strategy); project
  constitution via `speckit.constitution`; tracked `.github/copilot-instructions.md`.
- `docs/prd/` PRD→PR breakdown with dependency graph (this directory).
- `docs/parity-checklist.md` extracted from the Arduino code (watering logic
  thresholds, fail-safe behaviors, reservoir state machine incl. 300 s max runtime,
  real web-route list, serial diagnostics, WiFi reconnect/AP mode, NTP, storage format).
- `.gitattributes` (`* text=auto eol=lf` + binary exceptions) and renormalization;
  `.gitignore` additions (`firmware/build/`, `sdkconfig`, `managed_components/`).
- Legacy freeze (separate PR to `arduino-maintenance`, never merged to main): pinned
  PlatformIO platform + lib_deps, `arduino-legacy.yml` CI building flashable
  `firmware.bin` + `littlefs.bin` artifacts, legacy CLAUDE.md with maintenance banner,
  `arduino-final` tag moved to the pinned commit.

## Out of scope

- Any driver or application logic (phases 1–4).
- Resolving the repo-publication / secrets-in-history question (tracked as open
  question in master PRD; required before PR-13 GitHub-OTA goes live).

## Functional requirements covered

None directly — enables all FRs. Implements master PRD phase 0 definition and
non-functional requirements on buildability (pinned, container-identical builds).

## Dependencies

None. Everything else depends on this PR.

## Acceptance criteria

- [CI] `idf.py build` succeeds from clean checkout for **both** board targets in
  GitHub Actions, producing downloadable firmware artifacts.
- [CI] Same build works locally in the Docker container with identical pinned versions.
- [CI] `arduino-maintenance` branch builds reproducibly in CI with pinned dependencies;
  `git checkout arduino-final` gives a buildable legacy reference.
- [CI] Partition table sums to exactly 0x400000 and is accepted by the build.
- Review: parity checklist approved by Paul as the phase 4 ground truth.

## Estimated size

L
