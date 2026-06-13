# Implementation Plan: NVS Configuration and LittleFS Data Storage

**Branch**: `003-nvs-littlefs-storage` | **Date**: 2026-06-11 | **Spec**: [spec.md](spec.md)

**Input**: Feature specification from `/specs/003-nvs-littlefs-storage/spec.md`

## Summary

Port the storage layer to ESP-IDF as two redesigned contracts: `IConfigStore`
(typed, validated configuration in NVS with compiled-in factory defaults and
factory reset — FR13) and `IDataStorage` (bounded sensor history, rotating event
log, and usage statistics on a littlefs partition). Real NVS runs in host tests
via the IDF linux target; littlefs-specific code is confined to a target-only
mount helper, with all file logic exercised over POSIX on the host. Formats and
budgets are fixed in [data-model.md](data-model.md); verified groundwork in
[research.md](research.md) (D1–D10).

## Technical Context

**Language/Version**: C++ (modern, RAII), native ESP-IDF v6.0.1 APIs only — no
Arduino layers; `std::string` not `String`

**Primary Dependencies**: `nvs_flash` (IDF built-in), `joltwallet/littlefs`
==1.22.1 (already pinned in `firmware/main/idf_component.yml` +
`dependencies.lock`), Unity (host tests)

**Storage**: `nvs` partition 16 KiB @ 0x9000 (namespace `wscfg`, one entry per
item); `storage` partition (littlefs subtype) 960 KiB @ 0x310000, VFS at
`/storage` — both per `firmware/partitions.csv` (PR-01, unchanged)

**Testing**: Unity host test app on the IDF linux preview target
(`firmware/test_apps/host`, exit code = failure count, CI job from the PR-02
branch); real nvs_flash on linux (research D3); POSIX temp-dir for file logic
(D4); HIL checklist on the rev1 rig at Checkpoint 3

**Target Platform**: ESP32-WROOM-32E (rev1 devkit + rev2 PCB Kconfig targets);
host tests on linux target. This component is board-agnostic (no pins) — both
board targets build identically

**Project Type**: ESP-IDF component set within the existing `firmware/` project

**Performance Goals**: append path (1 record / dataLogInterval, default 5 min)
trivially exceeds need; worst-case range query (10 chunks ≈ 80 KiB scan) well
under one second — no further targets warranted

**Constraints**: power-loss safety (config old-or-new; history/events lose at
most the in-flight record — research D5); history+events ≤ ~832 KiB worst case
within the ~870–900 KiB available after web assets; ≥30-day retention at default
log interval; no migration of Arduino-era data (FR-014)

**Scale/Scope**: 9 config items (7 watering + 2 WiFi credential), ≤10 metrics ×
80 KiB history (= 800 KiB worst case), 32 KiB event log; 2 new interface
headers, 1 new `storage` component, 2 header-only mocks + 2 `Locked*`
decorators, host test suites + CI hook

## Constitution Check

*GATE: evaluated pre-Phase 0 and re-checked post-design — PASS (no violations).*

- **I. Safety First**: No pump/actuator interaction. Storage never blocks on
  network (contract invariant); invalid stored config shadows to safe defaults
  (FR-002) which feeds the fail-safe story of later PRs. ✅
- **II. Host-Testability**: Both contracts are host-includable headers in
  `components/interfaces`; all logic (validation, defaulting, bounding, eviction,
  rotation, range queries) runs in the host suite — real NVS on linux target,
  POSIX file I/O for data storage; only `StorageMount` touches esp_littlefs and
  contains no logic. Mocks provided for downstream consumers. ✅
- **III. Reproducible Builds**: No new dependencies; littlefs already pinned
  exactly; builds stay inside the pinned Docker image; CI extended, not altered
  (D1 caveat: image step pip-installs pinned littlefs-python at build time —
  accepted, noted). ✅
- **IV. Frozen Legacy**: Legacy code read-only reference; parity divergences
  (bounded history, settable intervals, dropped dead methods, redesigned
  contracts) are documented in the parity checklist as part of this feature. ✅
- **V. Checkpoint-Gated AI Workflow**: This plan is CP2 material; implementation
  via `implementer` subagent only; review never skipped. ✅
- **VI. English Outward**: All artifacts English. ✅

## Project Structure

### Documentation (this feature)

```text
specs/003-nvs-littlefs-storage/
├── spec.md              # Feature specification (clarified 2026-06-11)
├── plan.md              # This file
├── research.md          # Verified decisions D1–D10
├── data-model.md        # NVS schema, file formats, budgets, state transitions
├── quickstart.md        # Build/host-test/HIL validation guide
├── contracts/
│   ├── IConfigStore.md
│   └── IDataStorage.md
├── checklists/
│   └── requirements.md
└── tasks.md             # /speckit-tasks output (next phase)
```

### Source Code (repository root)

```text
firmware/
├── components/
│   ├── interfaces/                          # exists after PR-02 merge (D10)
│   │   └── include/interfaces/
│   │       ├── IConfigStore.h               # NEW — contract per contracts/IConfigStore.md
│   │       └── IDataStorage.h               # NEW — contract per contracts/IDataStorage.md
│   └── storage/                             # NEW component
│       ├── CMakeLists.txt                   # littlefs REQUIRES + StorageMount.cpp
│       │                                    #   excluded when IDF_TARGET=linux
│       │                                    #   (esp_littlefs has no linux port)
│       ├── include/storage/
│       │   ├── NvsConfigStore.h
│       │   ├── LittleFsDataStorage.h
│       │   ├── StorageMount.h               # target-only mount/format/info wrapper
│       │   ├── LockedConfigStore.h          # header-only Locked* decorators
│       │   ├── LockedDataStorage.h          #   (FR-013, PR-02 CP3 precedent)
│       │   └── testing/
│       │       ├── MockConfigStore.h        # header-only, never in target builds
│       │       └── MockDataStorage.h
│       └── src/
│           ├── NvsConfigStore.cpp
│           ├── LittleFsDataStorage.cpp
│           └── StorageMount.cpp             # target-only (see CMakeLists)
├── main/
│   └── CMakeLists.txt                       # + littlefs_create_partition_image(storage ...)
├── storage_image/                           # NEW — committed seed dir for the image
└── test_apps/host/                          # exists after PR-02 merge; extended:
    ├── main/                                #   + nvs_flash REQUIRES, custom partition CSV
    │   ├── test_config_store.cpp            # NEW suites
    │   └── test_data_storage.cpp
    └── partitions_host.csv                  # NEW — nvs partition for linux emulation

.github/workflows/firmware-build.yml         # + test -f firmware/build/storage.bin
docs/parity-checklist.md                     # + §6 divergence notes (bounded history,
                                             #   settable intervals, dropped methods)
```

**Structure Decision**: One new `storage` component (one concern per component,
`firmware/CLAUDE.md`); contracts join the shared `interfaces` component from
PR-02. Implementation begins by merging `main` after PR-02 (#7) lands — fallback
per research D10 if it hasn't.

### Phases

- **Phase A — contracts & mocks**: interface headers + header-only mocks +
  contract docs cross-references. Host-includable, no IDF includes.
- **Phase B — NvsConfigStore**: schema per data-model.md; host suite against real
  linux-target NVS (defaults, round-trips, rejection, shadowing, factory reset).
- **Phase C — LittleFsDataStorage**: chunked history + rotating events over POSIX
  with injectable base path; host suite (bounding, eviction, rotation, torn-tail,
  range queries, metric-cap).
- **Phase D — target integration**: StorageMount, partition image in build, CI
  image check, parity-checklist updates, HIL checklist delivery.

Each phase maps to independently testable user stories (US1 ↔ B, US2/US3 ↔ C,
US4 ↔ D); detailed ordering is `/speckit-tasks`' job.

## Complexity Tracking

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| Principle III (borderline): littlefs partition-image step pip-installs `littlefs-python==0.15.0` from PyPI at build time, inside the otherwise fully pinned container build (research D1) | The pinned joltwallet/littlefs component's own `project_include.cmake` does this; the version is exactly pinned by the component | Pre-baking the wheel into a custom Docker image forks the canonical `espressif/idf:v6.0.1` image — a larger Principle III deviation than a pinned, component-managed pip install. Accepted with the precondition that CI has PyPI access. |
