---
description: "Task list for the littlefs frontend (PR-10)"
---

# Tasks: Frontend served from littlefs (PR-10)

**Input**: `spec.md`, `plan.md`, research report.
**Tests**: the pure surface is thin (path + content-type helpers) and is host-tested; the real acceptance is
HIL (browser on the rig). CI gates on the pure tests + both board builds + the littlefs image building.

## Format: `[ID] [P?] [Story] Description` — [P] parallelizable; paths under repo root.

---

## Phase 1: Setup — adapted frontend source tree + vendored libs

- [ ] T001 Create `firmware/web/` and copy the assets to adapt: `index.html`, `script.js`, `styles.css`,
  `favicon.ico` from `data/` (COPIES — never edit `data/`). Do NOT copy `wifi_setup.html`.
- [ ] T002 [P] Add vendored libs under `firmware/web/vendor/`: `chart.min.js` + `chartjs-adapter-date-fns.min.js`
  (pinned versions) and `tailwind.css` (a pre-built/purged Tailwind stylesheet). Add `firmware/web/README.md`
  recording each lib's exact version/source URL and the manual Tailwind-purge regen command.
- [ ] T003 Edit `firmware/web/index.html` (the copy): replace the three CDN `<script>`/`<link>` references
  (Tailwind, Chart.js, chartjs-adapter) with local `vendor/…` paths; point the local `styles.css`/`script.js`
  refs as needed. No layout redesign.

**Checkpoint**: the adapted source tree exists; no CDN references remain in `firmware/web/index.html`.

---

## Phase 2: Build pipeline — gzip + littlefs packing

- [ ] T004 Add `firmware/tools/gzip_assets.py` — deterministic (gzip level 9, `mtime=0`) compressor: given a
  source dir + a staging dir, write `<name>.gz` for each file, preserving the `vendor/` subdir.
- [ ] T005 Wire the pipeline in `firmware/main/CMakeLists.txt`: an `add_custom_command`/`add_custom_target`
  that populates `${CMAKE_BINARY_DIR}/web_image/` with the `storage_image/` seed contents plus the gzipped
  `firmware/web/*` (via `${Python3_EXECUTABLE} gzip_assets.py`); repoint `littlefs_create_partition_image(storage
  <web_image dir> FLASH_IN_PROJECT)` at the staging dir with a dependency on the gzip target. Both boards.

**Checkpoint**: `idf.py build` produces `build/storage.bin` containing the gzipped assets; size reported.

---

## Phase 3: Foundational — pure helpers (host-tested)

- [ ] T006 [P] Add `firmware/components/api/include/api/ApiStatic.h` + `src/ApiStatic.cpp` (pure, builds on
  host): `sanitizeAssetPath(uri) -> std::optional<std::string>` (map `/`→`index.html`, strip query, reject
  `..`/leading-escape/NUL) and `contentTypeForPath(path) -> const char*` (html/js/css/ico + default). No IDF
  includes.
- [ ] T007 [P] Host tests `test_apps/host/main/test_api_static.cpp`: root→index; traversal (`/../x`, `a/../../b`),
  NUL, and query-string cases; content-type per extension + default. Register in the host suite + `test_main.cpp`.

**Checkpoint**: pure helpers host-tested green; existing suite unaffected.

---

## Phase 4: User Story 1 — dashboard loads from littlefs (P1) 🎯 MVP

- [ ] T008 [US1] Static handler in `firmware/components/api/src/ApiServer.cpp` (target-only): register `GET /*`
  AFTER the `/api/v1/*` routes; use `ApiStatic` helpers; open `<StorageMount::kBasePath>/<path>.gz`, set
  `Content-Type` + `Content-Encoding: gzip` + `Cache-Control`, stream with `httpd_resp_send_chunk` + terminating
  empty chunk; 404 (small HTML) on sanitize-reject or missing file. Confirm unmatched `/api/v1/*` still returns
  the JSON 404 envelope. Add `storage` include dir if needed for `StorageMount::kBasePath`.
- [ ] T009 [US1] Adapt the sensors + status read path in `firmware/web/script.js`: `GET /api/v1/sensors`
  (`environmental.valid`, tolerate null soil, new `level`), `GET /api/v1/status` + `GET /api/v1/config` +
  `GET /api/v1/pumps` (bytes storage, `mode`, `wifi`), populate the existing DOM. Graceful null/`valid:false`.

**Checkpoint**: rig browser loads the dashboard, live env/status values render (SC-002 shape).

---

## Phase 5: User Story 2 — pump + mode control (P2)

- [ ] T010 [US2] Adapt the control path in `firmware/web/script.js`: plant run/stop → `POST /api/v1/pumps/plant`
  JSON; mode toggle → `POST /api/v1/config {wateringEnabled}`; reservoir → `POST /api/v1/pumps/reservoir`
  run/stop + level display, block hidden when `/api/v1/pumps` has no `reservoir` (rev2); surface 409/4xx errors.

**Checkpoint**: pump start/stop + mode switch work from the UI.

---

## Phase 6: User Story 3 — config persistence + history (P2)

- [ ] T011 [US3] Adapt the config page in `firmware/web/script.js`: load `GET /api/v1/config`; save
  `POST /api/v1/config` with `wateringDurationS`/`minWateringIntervalS` (seconds) + range-error handling;
  values shown after reboot.
- [ ] T012 [US3] Adapt the history chart: `GET /api/v1/history?metric=&range=` (map old sensor+reading pair →
  single `metric`), consume `{timestamps,values}`; Chart.js loaded from the vendored path; empty-series safe.
- [ ] T013 [P] [US3] OTA upload page placeholder wired to `POST /api/v1/ota` (currently a 501 stub) — visible,
  non-functional until PR-13; shows the stub response cleanly.

**Checkpoint**: config persists across reboot; history renders offline; OTA placeholder present.

---

## Phase 7: Polish & cross-cutting

- [ ] T014 Add a "Frontend (feature 010)" section to `firmware/CLAUDE.md` (asset source `firmware/web/`, the
  gzip/staging build pipeline, static handler in ApiServer, vendored-libs/offline decision, contract-gap
  handling for reservoir/status).
- [ ] T015 `idf.py size` + report `build/storage.bin` size for both boards; confirm well within 960 KiB (SC-001).
- [ ] T016 Author `specs/010-frontend-littlefs-assets/checklists/hil.md` (browser loads dashboard, live values,
  pump/mode, config persists across reboot, phone browser, offline render with WiFi-router-but-no-internet).
- [ ] T017 Full host suite + both board builds green; confirm frozen `data/` + `docs/api/openapi.yaml` unchanged.

---

## Dependencies & order

- Setup (T001-T003) → Build pipeline (T004-T005) → Foundational pure (T006-T007) → US1 (T008-T009) → US2 (T010)
  → US3 (T011-T013) → Polish.
- T008 (static handler) unblocks browser testing; T009-T012 all edit `firmware/web/script.js` (sequence them).
- Pure/host-tested: T006-T007. Target/HIL: T008 + the JS (T009-T013).

## Implementation strategy

- MVP = US1 (assets serve from littlefs + dashboard loads + live values) — the phase-3 exit gate.
- Never edit frozen `data/` / the frozen `/api/v1/` contract. Keep the static handler isolated (httpd task,
  not watchdog-subscribed). Deterministic gzip; no node toolchain in CI.
- Build via Docker on an rsync'd `/tmp/ws010-firmware`; fullclean + rm sdkconfig between boards.
