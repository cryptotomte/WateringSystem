# Quickstart: NVS Configuration and LittleFS Data Storage

**Feature**: 003-nvs-littlefs-storage

## Prerequisites

- Docker with the pinned `espressif/idf:v6.0.1` image (Constitution III).
- PR-02 merged into the branch (provides `components/interfaces` and
  `firmware/test_apps/host`; research D10).
- For HIL: the rev1 devkit bench rig.

## Build (both targets)

```bash
docker run --rm -v "$PWD/firmware":/project -w /project espressif/idf:v6.0.1 \
  idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.board.rev1_devkit" build
docker run --rm -v "$PWD/firmware":/project -w /project espressif/idf:v6.0.1 \
  idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.board.rev2" build
```

When switching boards, delete the generated `firmware/sdkconfig` (or run
`idf.py fullclean`) first — stale sdkconfig silently keeps the previous board
(see `firmware/CLAUDE.md`).

**Expected**: green build AND `firmware/build/storage.bin` exists (littlefs image
created by `littlefs_create_partition_image` — CI verifies this file).

## Host tests (linux preview target)

```bash
docker run --rm -v "$PWD/firmware":/project -w /project/test_apps/host \
  espressif/idf:v6.0.1 bash -c \
  "idf.py --preview set-target linux && idf.py build && ./build/*_host_tests.elf"
```

**Expected**: exit code 0 (= failure count). Suites cover, per spec SC-005:

- every factory default read on erased NVS (FR-001/FR-002)
- set/get round-trip per item; rejection per documented out-of-range case (FR-003)
- out-of-range *stored* value shadowed by default (FR-002, US1.3)
- factory reset semantics incl. credential removal (FR-005, SC-003)
- history range filtering incl. empty/no-data/t0>t1 cases (FR-009)
- bounded eviction: simulate 10× retention bound, verify budget + retrievability
  (FR-010, SC-004)
- event rotation: newest retained at budget, torn-tail skip (FR-011)
- 17th-metric rejection, unknown-category passthrough (data-model guards)

## HIL checklist (Paul, bench rig, Checkpoint 3)

1. **Fresh flash** (`idf.py erase-flash flash monitor`): boot log shows littlefs
   formatted + mounted, usage reported (total ≈ 960 KiB, used small) → SC-001,
   US4.1.
2. **Config persistence**: change a threshold (serial/test console), power-cycle,
   verify value retained → SC-002, US1.2.
3. **Factory reset**: invoke reset operation, verify all defaults restored and
   WiFi credentials gone → SC-003, US1.4.
4. **Reboot persistence**: write a few readings/events, reboot (no erase), verify
   data survives and usage figures reflect it → US4.3.
5. **Corruption recovery**: deliberately corrupt the storage partition
   (`esptool.py erase_region 0x310000 0x2000` — both littlefs superblock blocks,
   a single-block erase would still mount via the redundant superblock), boot,
   verify reformat-and-continue (no boot loop) → US4.2, FR-007.

## Artifacts

- Spec: [spec.md](spec.md) — requirements and scenarios
- Plan: [plan.md](plan.md) — component layout and phases
- Research: [research.md](research.md) — verified facts D1–D10
- Data model: [data-model.md](data-model.md) — NVS schema, file formats, budgets
- Contracts: [contracts/IConfigStore.md](contracts/IConfigStore.md),
  [contracts/IDataStorage.md](contracts/IDataStorage.md)
