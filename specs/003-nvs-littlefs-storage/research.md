# Research: NVS Configuration and LittleFS Data Storage

**Feature**: 003-nvs-littlefs-storage | **Date**: 2026-06-11

Facts below were verified against the pinned component archive
(joltwallet__littlefs 1.22.1, hash matching `firmware/dependencies.lock`; bundled
littlefs core v2.11), the ESP-IDF v6.0.1 tree inside the pinned
`espressif/idf:v6.0.1` Docker image, the frozen Arduino firmware, and the
PR-02 branch (`002-pump-gpio-board`).

## D1. Partition image creation in the build (CI acceptance)

**Decision**: Call `littlefs_create_partition_image(storage <dir> FLASH_IN_PROJECT)`
with a committed seed directory; extend the CI "verify binaries" step with
`test -f firmware/build/storage.bin`.

**Rationale**: The component's `project_include.cmake` provides
`littlefs_create_partition_image(<partition> <base_dir> [FLASH_IN_PROJECT]
[DEPENDS ...])` as an ALL target — it runs on every `idf.py build`, looks up the
`storage` partition (0xF0000 @ 0x310000 in `firmware/partitions.csv`), and emits
`build/storage.bin`. `FLASH_IN_PROJECT` attaches it to the flash target, which
gives HIL "fresh-flash boots with formatted FS" a deterministic starting image.

**Alternatives considered**: Letting first boot format an empty partition (no
image) — rejected: the acceptance criterion explicitly requires image creation in
the build, and PR-10 will need the same mechanism for web assets.

**Caveat**: The image step pip-installs `littlefs-python==0.15.0` into a build-dir
venv at build time (pinned by the component); CI needs PyPI access (GitHub runners
have it).

## D2. Mount and usage API

**Decision**: `esp_vfs_littlefs_register()` with `.partition_label = "storage"`,
`.base_path = "/storage"`, `.format_if_mount_failed = true`; usage via
`esp_littlefs_info("storage", &total, &used)`.

**Rationale**: Exactly matches the legacy mount-or-format parity behavior
(`LittleFS.begin(true)`) and the component's canonical example. The partition
**name** is `storage` (littlefs is the subtype) — the PR-06 PRD wording "label
littlefs" is a known imprecision, already corrected in the spec assumptions.

## D3. Host-testing strategy for NVS

**Decision**: Run the **real** `nvs_flash` implementation on the linux preview
target in the host test app. `NvsConfigStore` is tested directly against it; a
header-only `MockConfigStore` is still provided for later PRs' consumers.

**Rationale**: Verified in the IDF v6.0.1 tree: `nvs_flash` has an explicit linux
branch (drops esp_libc/esptool_py deps, `-DLINUX_TARGET`), and `esp_partition`
ships `partition_linux.c` (file-backed flash emulation); IDF's own
`nvs_flash/host_test` app proves the combination. Testing the real NVS engine
covers defaulting, round-trips, and factory-reset semantics with no mock skew —
strictly stronger than the "NVS layer mocked" fallback in the PR-06 acceptance
criteria.

**Requirements**: host test app needs `CONFIG_PARTITION_TABLE_CUSTOM` with an
`nvs` partition CSV and `nvs_flash_erase()` between tests for isolation.

**Alternatives considered**: KV-primitive abstraction with an in-memory fake —
rejected as the primary strategy (mock skew, more interface surface), kept as
non-goal.

## D4. Host-testing strategy for littlefs data storage

**Decision**: `LittleFsDataStorage` is written against POSIX stdio with an
injectable base path. On target it operates under the `/storage` VFS mount; in
host tests it operates on a temp directory on the build host. The mount/format/
usage wrapper (`StorageMount`) is the only target-only code; storage statistics
reach consumers through the data-storage interface and are mocked on host.

**Rationale**: Verified: the esp_littlefs component has **no** linux-target branch
in its CMakeLists — it cannot run on the host. All record-format, bounding,
eviction, and range-query logic is plain file I/O and runs identically over POSIX
on both targets; only mount and `esp_littlefs_info` are littlefs-specific.

**Mechanism**: the storage component's CMakeLists conditionally excludes the
littlefs REQUIRES and `StorageMount.cpp` when `IDF_TARGET=linux`
(`if(NOT ${IDF_TARGET} STREQUAL "linux")`), so the host test app can link
`NvsConfigStore` + `LittleFsDataStorage` without pulling in esp_littlefs.

## D5. Power-loss safety model

**Decision**: Append-only chunk files with `fflush`+`fsync` per record append;
chunk eviction by file delete; no in-place overwrites of committed data. Per-record
sentinel/length framing so a partially committed tail record is detected and
skipped on read. NVS handles its own atomicity.

**Rationale**: littlefs core v2.11 guarantees (README/DESIGN): rename and remove
are atomic under power loss; file updates are copy-on-write and not visible until
sync/close — interruption reverts to the last synced state (old data is never
corrupted, in-flight data is dropped). NVS entries are CRC-protected and
log-structured: a torn `nvs_set_*` yields old-or-new, never garbage;
`nvs_flash_erase_partition()` + `nvs_flash_init()` is the standard factory-reset
sequence (interrupted erase just fails init and is erased again — no key
resurrection).

**Consequence for the spec's torn-write edge case**: config = old-or-new by NVS
design; history/events = at most the in-flight record is lost, earlier records
intact by COW design. Both host-testable as behavior (write → simulated-crash →
re-open semantics) at the file level on POSIX.

## D6. History format and retention budget

**Decision**: Per-metric directory of append-only fixed-record chunk files,
ring-evicted per metric:

- Record: `{uint32 epoch_seconds, float value}` = 8 bytes, little-endian.
- Chunk file: `/storage/hist/<metric>/<first_epoch>.dat`, capped at 8 KiB
  (1024 records ≈ 3.55 days at the 5-min default log interval).
- Per metric: at most 10 chunks (80 KiB). Evicting the oldest full chunk when an
  11th would be created leaves ≥9 full chunks + the active one ⇒ **≥31.9 days**
  retained at default interval — meets the ≥30-day clarification with the worst
  case of all 10 metrics active staying within budget:
  10 metrics × 80 KiB = 800 KiB ≤ ~870 KiB available (partition plan), with the
  realistic case (7 always-on metrics) at 560 KiB.
- Query: select chunks by filename epoch + linear scan; records are
  chronological within and across chunks by construction.

**Rationale**: Append-only avoids header rewrite amplification; eviction is an
atomic file delete; chunk granularity keeps per-file littlefs block overhead
amortized (8 KiB files vs 4 KiB blocks — unlike daily files at 2.3 KiB, which
would waste a block each across ~310 files). Numbers documented as deliberate
divergence from the legacy unbounded JSON files (parity checklist update is part
of this feature).

**Alternatives considered**: single pre-sized ring file per metric (rejected:
in-place header updates on every append, harder torn-write reasoning); daily
files (rejected: block-overhead explosion); legacy JSON arrays (rejected:
unbounded, parse-buffer failure mode is the very defect this PR removes).

## D7. Event log format and budget

**Decision**: Two rotating append-only files (`/storage/events/0.log`,
`1.log`), 16 KiB each (~32 KiB total). Record: `{uint32 epoch, uint8 category,
uint8 detail_len, char detail[≤120]}` framed with a leading record marker for
torn-tail detection. When the active file reaches its cap, the other file is
truncated and becomes active (oldest-half rotation, newest events always
retained). Retrieval returns newest-first across both files.

**Rationale**: Satisfies FR-011 rotation semantics with two atomic primitives
(append, truncate-on-rotate); 32 KiB ≈ several hundred events — months of normal
operation; keeps total worst-case littlefs usage (800 + 32 KiB) inside the
~870 KiB budget. Categories per FR-011/PR-08 PRD: pump, fail-safe, connectivity,
OTA, reset/watchdog (stored as an enum byte; PR-08 may extend).

## D8. NVS schema

**Decision**: Single namespace `wscfg`; one NVS entry per configuration item with
short fixed keys (`moist_low`, `moist_high`, `water_dur`, `soak_pause`,
`water_en`, `read_iv`, `log_iv`, `wifi_ssid`, `wifi_pass`). Types: `float` stored
as u32 bit-pattern (NVS has no float type), durations/intervals as u32, bool as
u8, credentials as strings. Factory defaults compiled in as constants; reads
fall back to the default on missing key **or** out-of-range stored value
(spec FR-002); factory reset = `nvs_flash_erase_partition("nvs")` + re-init.

**Rationale**: Individual entries (vs one blob) give per-item atomic updates, no
read-modify-write of unrelated items, and trivial PR-05/PR-07 extensibility;
16 KiB ≈ 378 entries dwarfs the ~10 needed (partition plan). Key names ≤15 chars
(NVS limit).

**Alternatives considered**: one ~200 B struct blob (partition plan mentioned
both) — rejected: any single-item change rewrites the whole blob and a layout
change invalidates everything; per-entry layout is self-versioning by key.

## D9. Concurrency

**Decision**: Base implementations stay unsynchronized (host-test friendly);
cross-task use goes through header-only `LockedConfigStore`/`LockedDataStorage`
decorators over the interfaces, following the PR-02 CP3 precedent
(`LockedWaterPump` pattern). The decorators ship **in this PR** (FR-013 coverage)
even though the first concurrent consumer arrives in PR-08/PR-09.

**Rationale**: Spec FR-013 requires no corruption/torn values under concurrent
use; the established project pattern solves it without polluting pure logic with
RTOS primitives, keeping Constitution II (host-testability) intact.

## D10. Sequencing relative to PR-02

**Decision**: Implementation starts by merging current `main` into this branch
**after PR-02 (#7) merges** — the `interfaces` component and
`firmware/test_apps/host` app this feature extends live there. If PR-02 is still
unmerged when implementation is approved, the fallback is to create the same
scaffolding here and reconcile at merge (both are additive), but waiting is the
recommended path since #7 only awaits its HIL pass.

**CI note**: the host-test job pattern (esp-idf-ci-action with explicit
`target: linux` — the action's default `IDF_TARGET=esp32` otherwise aborts
`set-target linux`) also comes from the PR-02 branch and gets extended, not
duplicated.
