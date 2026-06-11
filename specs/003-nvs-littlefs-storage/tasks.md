# Tasks: NVS Configuration and LittleFS Data Storage

**Input**: Design documents from `/specs/003-nvs-littlefs-storage/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/, quickstart.md

**Tests**: Host tests are explicitly required (spec SC-005, acceptance criteria, Constitution II) — within each story: interface header + skeleton first (tests must compile), then failing tests, then implementation.

**Organization**: Grouped by user story; each story phase is an independently testable increment.

## Format: `[ID] [P?] [Story] Description`

## Phase 1: Setup

**Purpose**: Branch baseline and component scaffolding

- [ ] T001 Merge current `origin/main` into `003-nvs-littlefs-storage` — PR-02 (#7) must be merged to main first (research D10). If #7 is still open: STOP and surface to Paul; the D10 fallback (create scaffolding here, reconcile at merge) requires his explicit approval. Then verify both target builds green per quickstart.md
- [ ] T002 Create `storage` component skeleton: `firmware/components/storage/CMakeLists.txt` with REQUIRES nvs_flash + interfaces, and the littlefs REQUIRES + `src/StorageMount.cpp` wrapped in `if(NOT ${IDF_TARGET} STREQUAL "linux")` (esp_littlefs has no linux port — research D4 mechanism); empty include/src tree per plan.md structure
- [ ] T003 [P] Create committed littlefs seed directory `firmware/storage_image/` (a single `README` placeholder file explaining the image's role)

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Host test app must be able to run NVS + new suites before any story's tests can be written

**⚠️ CRITICAL**: Blocks all user stories

- [ ] T004 Extend host test app for NVS on linux: add `nvs_flash` to REQUIRES in `firmware/test_apps/host/main/CMakeLists.txt`, add `firmware/test_apps/host/partitions_host.csv` with an `nvs` partition, set `CONFIG_PARTITION_TABLE_CUSTOM` in `firmware/test_apps/host/sdkconfig.defaults` (research D3; IDF's nvs_flash/host_test is the reference pattern)
- [ ] T005 Register two empty Unity suites `firmware/test_apps/host/main/test_config_store.cpp` and `firmware/test_apps/host/main/test_data_storage.cpp` in the host app build; verify the host app still builds and runs (exit 0) on the linux target

**Checkpoint**: Host harness ready — story implementation can begin

---

## Phase 3: User Story 1 — Configuration survives restarts and resets to safe defaults (Priority: P1) 🎯 MVP

**Goal**: Typed NVS-backed configuration with compiled-in defaults, validation, credential handling, and factory reset (FR-001..FR-006, FR13)

**Independent Test**: Host suite against real linux-target NVS: erased NVS → all defaults; set/get round-trips; out-of-range rejection and shadowing; factory reset (quickstart.md "Host tests")

### Contract & skeleton for User Story 1

- [ ] T006 [P] [US1] `IConfigStore` interface header per contracts/IConfigStore.md in `firmware/components/interfaces/include/interfaces/IConfigStore.h` (host-includable, no IDF includes; documents contract + divergences from legacy)
- [ ] T007 [P] [US1] Header-only `MockConfigStore` in `firmware/components/storage/include/storage/testing/MockConfigStore.h` (in-memory, instrumented; never compiled into target builds)
- [ ] T008 [US1] `NvsConfigStore` declaration + stub (compiles, all methods fail/return defaults) in `firmware/components/storage/include/storage/NvsConfigStore.h` + `firmware/components/storage/src/NvsConfigStore.cpp`

### Tests for User Story 1 (write against the stub, must fail)

- [ ] T009 [P] [US1] Default-on-erased-NVS tests (every item from data-model.md table) in `firmware/test_apps/host/main/test_config_store.cpp`
- [ ] T010 [P] [US1] Round-trip + persistence-across-reinit tests per item in `firmware/test_apps/host/main/test_config_store.cpp`
- [ ] T011 [P] [US1] Out-of-range write rejection (every documented bound) and out-of-range *stored* value shadowing (US1 scenario 3) in `firmware/test_apps/host/main/test_config_store.cpp`
- [ ] T012 [P] [US1] Factory reset semantics + credential set/clear/never-logged tests, plus `MockConfigStore` contract-conformance cases (same invariants as the real store — FR-012) in `firmware/test_apps/host/main/test_config_store.cpp`

### Implementation for User Story 1

- [ ] T013 [US1] Implement `NvsConfigStore` per data-model.md NVS schema (namespace `wscfg`, per-item entries, float-as-u32-bits, defaults/range constants, factory reset via `nvs_flash_erase_partition` + re-init) in `firmware/components/storage/src/NvsConfigStore.cpp`
- [ ] T014 [US1] Run US1 suites green on linux target; fix until exit 0

**Checkpoint**: Config layer fully verified on host — MVP of this feature

---

## Phase 4: User Story 2 — Sensor history is recorded, bounded, and retrievable (Priority: P2)

**Goal**: Chunked, ring-evicted per-metric history with ≥30-day retention and parity query semantics (FR-009, FR-010)

**Independent Test**: Host suite over POSIX temp dir: range filtering, bounded eviction at 10× retention, torn-tail handling, metric cap (quickstart.md)

### Contract & skeleton for User Story 2

- [ ] T015 [P] [US2] `IDataStorage` interface header (SensorReading/EventRecord/StorageStats types, full contract per contracts/IDataStorage.md) in `firmware/components/interfaces/include/interfaces/IDataStorage.h`
- [ ] T016 [P] [US2] Header-only `MockDataStorage` in `firmware/components/storage/include/storage/testing/MockDataStorage.h`
- [ ] T017 [US2] `LittleFsDataStorage` declaration + stub (injectable base path + stats provider, compiles) in `firmware/components/storage/include/storage/LittleFsDataStorage.h` + `firmware/components/storage/src/LittleFsDataStorage.cpp`

### Tests for User Story 2 (write against the stub, must fail)

- [ ] T018 [P] [US2] Range-query tests: chronological order, inclusive bounds, empty result on no-data/unknown-metric/t0>t1 (FR-009, edge cases), plus `MockDataStorage` contract-conformance cases (FR-012) in `firmware/test_apps/host/main/test_data_storage.cpp`
- [ ] T019 [P] [US2] Bounding tests: chunk sealing at 8 KiB, eviction at 11th chunk, ≥30-day retention guarantee at default interval, SC-004 10×-bound endurance, 11th-distinct-metric rejection in `firmware/test_apps/host/main/test_data_storage.cpp`
- [ ] T020 [P] [US2] Torn-tail tests: file size % 8 ≠ 0 → truncate-on-read, earlier records intact (research D5) in `firmware/test_apps/host/main/test_data_storage.cpp`

### Implementation for User Story 2

- [ ] T021 [US2] Implement history part of `LittleFsDataStorage` per data-model.md (8-byte LE records, `/hist/<metric>/<first_epoch>.dat` chunks, fsync-per-append, ring eviction, 10-metric cap) in `firmware/components/storage/src/LittleFsDataStorage.cpp`
- [ ] T022 [US2] Run US2 suites green on linux target

**Checkpoint**: History storage verified independently of US1

---

## Phase 5: User Story 3 — Safety-relevant events are persisted (Priority: P3)

**Goal**: Rotating two-file event log, newest always retained (FR-011)

**Independent Test**: Host suite: append/retrieve newest-first, rotation at cap, burst behavior, torn-tail skip

### Tests for User Story 3 (write first, must fail)

- [ ] T023 [P] [US3] Event tests: framed record round-trip, newest-first retrieval with maxCount, rotation truncates oldest half and never the newest, burst stays within 32 KiB budget, torn-tail marker/length detection, unknown-category passthrough, >120-byte detail truncated-not-rejected in `firmware/test_apps/host/main/test_data_storage.cpp`

### Implementation for User Story 3

- [ ] T024 [US3] Implement event-log part of `LittleFsDataStorage` per data-model.md (`/events/0.log`+`1.log`, 0xE7-framed records, 16 KiB cap, truncate-and-switch rotation, 120-byte detail truncation) in `firmware/components/storage/src/LittleFsDataStorage.cpp`
- [ ] T025 [US3] Run US3 suite green on linux target

**Checkpoint**: All host-testable behavior (US1–US3) green

---

## Phase 6: Concurrency decorators (FR-013, cross-story)

**Goal**: Safe cross-task use without polluting the unsynchronized base implementations (research D9, PR-02 CP3 precedent)

- [ ] T026 [P] Header-only `LockedConfigStore` decorator over `IConfigStore` in `firmware/components/storage/include/storage/LockedConfigStore.h`
- [ ] T027 [P] Header-only `LockedDataStorage` decorator over `IDataStorage` in `firmware/components/storage/include/storage/LockedDataStorage.h`
- [ ] T028 Concurrency host tests (delegation correctness for every method; mutex-held invariants per the mechanism PR-02's `LockedWaterPump` tests established) in `firmware/test_apps/host/main/test_data_storage.cpp` and `test_config_store.cpp`; suites green

---

## Phase 7: User Story 4 — Storage health is visible (Priority: P3, target integration)

**Goal**: Mount-or-format at boot, usage reporting, build-time partition image, HIL readiness (FR-007, FR-008)

**Independent Test**: [HIL] quickstart.md checklist on the rev1 rig (fresh flash → format+mount+usage; reboot persistence; corruption recovery via 0x2000 superblock erase)

### Implementation for User Story 4

- [ ] T029 [P] [US4] Target-only `StorageMount` (esp_vfs_littlefs_register with `partition_label="storage"`, `base_path="/storage"`, `format_if_mount_failed=true`; `esp_littlefs_info` stats provider wired into `LittleFsDataStorage`) in `firmware/components/storage/include/storage/StorageMount.h` + `firmware/components/storage/src/StorageMount.cpp` (research D2/D4; excluded from linux build per T002)
- [ ] T030 [P] [US4] Partition image in build: `littlefs_create_partition_image(storage ../storage_image FLASH_IN_PROJECT)` in `firmware/main/CMakeLists.txt` (research D1)
- [ ] T031 [US4] Boot wiring in `firmware/main/`: NVS init (with the standard erase-on-`NO_FREE_PAGES/NEW_VERSION` recovery), StorageMount at startup, one-line usage log (parity: serial status block) — keep ESP_LOG only, no business logic in main
- [ ] T032 [US4] HIL verification path for config persistence and factory reset on the rig (follow the verification mechanism PR-02 established for its HIL pass — extend it, don't invent a parallel one) in `firmware/main/` or the PR-02 test console location
- [ ] T033 [US4] CI: add `test -f firmware/build/storage.bin` to the verify-binaries step in `.github/workflows/firmware-build.yml`; confirm host-test job picks up the new suites; both target builds + host job green in CI

**Checkpoint**: Feature complete pending HIL sign-off at Checkpoint 3

---

## Phase 8: Polish & Cross-Cutting

- [ ] T034 [P] Update `docs/parity-checklist.md` §6: bounded history format (D6), settable interval items, dropped `getLastSensorReading`/`pruneOldReadings`, redesigned split contracts, event log as new surface, WiFi-unconfigured representation change (legacy `CONFIGURE_ME` sentinel in `/wifi_config.json` → empty-string NVS factory state) — each marked as deliberate divergence with rationale (spec FR-010/FR-012/FR-014)
- [ ] T035 [P] Update `firmware/CLAUDE.md` component list with the `storage` component and host-test pointers
- [ ] T036 Run full quickstart.md validation (both target builds + storage.bin check + host suite) in the pinned container; deliver test checklist incl. HIL items for Checkpoint 3

---

## Dependencies & Execution Order

- **Phase 1 → Phase 2 → stories**: T001 gates everything (PR-02 merge). T004–T005 gate all test tasks.
- **US1 (Phase 3)**: T006 → (T007, T008) → T009–T012 [P] → T013 → T014. Independent of US2–US4. **MVP.**
- **US2 (Phase 4)**: T015 → (T016, T017) → T018–T020 [P] → T021 → T022. Independent of US1 (different files; both stories append to suite files created in T005 — coordinate or sequence suite-file edits).
- **US3 (Phase 5)**: T023 → T024 → T025; depends on US2's T015/T017/T021 (same class/files) — sequential after US2.
- **Phase 6 (FR-013)**: T026/T027 depend on T006/T015 (interfaces); T028 after both. Can run any time after the interfaces exist; placed here to decorate finished implementations.
- **US4 (Phase 7)**: T029 depends on T021/T024 (stats provider into LittleFsDataStorage); T031 depends on T013/T029; T033 last.
- **Polish (Phase 8)**: after all stories; T034/T035 parallel.

### Parallel opportunities

- T006+T007 then T009–T012 (US1 tests) in parallel.
- T015+T016 then T018–T020 (US2 tests) in parallel.
- US1 and US2 phases can run concurrently after Phase 2 (coordinate the shared suite files).
- T026+T027, T029+T030, T034+T035 in parallel.

## Implementation Strategy

MVP = Phase 1–3 (config store host-verified). Incremental: each story phase ends
green and independently testable; stop at any checkpoint. Suggested single-agent
order: T001→T005, US1, US2, US3, decorators, US4, polish — with a commit after
each phase checkpoint (one branch, commit immediately per CLAUDE.md git rules).
