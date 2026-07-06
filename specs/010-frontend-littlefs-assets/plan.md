# Implementation Plan: Frontend served from littlefs (010)

## Approach

Extend the existing `esp_http_server` (the `api` component) with a static-file handler that serves gzipped
assets from the mounted littlefs volume, and produce those gzipped assets at build time from a new,
adapted-frontend source tree under `firmware/`. The frozen `data/` tree and the frozen `/api/v1/` contract are
untouched. Split as always into a small pure/host-tested core (path sanitization + content-type mapping) and a
target-only HTTP/file-I/O shell.

## Architecture

### Source of truth for the adapted frontend — `firmware/web/`
A new committed directory (NOT `data/`):
- `firmware/web/index.html` — copy of `data/index.html`, adapted: CDN `<script>/<link>` → local vendored paths.
- `firmware/web/script.js` — copy of `data/script.js`, adapted: the API layer rewritten to `/api/v1/` (methods, JSON bodies, field names/units, response shapes) per the contract mapping in research §2.
- `firmware/web/styles.css` — copy of `data/styles.css` (verbatim unless a class tweak is needed for the vendored Tailwind).
- `firmware/web/favicon.ico` — copy of `data/favicon.ico` (verbatim).
- `firmware/web/vendor/chart.min.js`, `firmware/web/vendor/chartjs-adapter-date-fns.min.js` — pinned static libs.
- `firmware/web/vendor/tailwind.css` — a pre-built/purged Tailwind stylesheet (committed vendored asset; regeneration documented in `firmware/web/README.md`, not a CI step).
- `firmware/web/README.md` — provenance + pinned versions of the vendored libs + the manual Tailwind regen command.

`data/wifi_setup.html` is NOT copied (orphaned against v1).

### Build-time gzip + littlefs packing
- A committed helper `firmware/tools/gzip_assets.py` (deterministic: gzip with `mtime=0`, level 9) compresses each file from a source dir into a staging dir as `<name>.gz`.
- In `firmware/main/CMakeLists.txt`: an `add_custom_command`/`add_custom_target` builds `${CMAKE_BINARY_DIR}/web_image/` = the `storage_image/` seed contents + the gzipped `firmware/web/*` assets. `littlefs_create_partition_image(storage <web_image dir> FLASH_IN_PROJECT)` is repointed at that staging dir and made to depend on the gzip target, so every `idf.py build` regenerates `build/storage.bin` with the assets (CI already checks the image builds). Uses `${Python3_EXECUTABLE}` (present in the IDF toolchain) — no `gzip` binary / node dependency.
- Result on device: `/storage/index.html.gz`, `/storage/script.js.gz`, `/storage/styles.css.gz`, `/storage/favicon.ico.gz`, `/storage/vendor/chart.min.js.gz`, etc., coexisting with runtime `/storage/hist/` + `/storage/events/`.

### Static-file handler — extend `ApiServer` (target-only)
- Pure helpers (host-tested), added to the `api` component's pure layer (e.g. `ApiStatic.{h,cpp}`):
  - `sanitizeAssetPath(requestUri) -> optional<string>`: map `/` → `index.html`; strip query; reject `..`/absolute-escape/NUL; return the safe relative path (or nullopt to 404).
  - `contentTypeForPath(path) -> const char*`: `.html→text/html`, `.js→application/javascript`, `.css→text/css`, `.ico→image/x-icon`, else `application/octet-stream`.
- Target-only in `ApiServer.cpp`: a `GET /*` catch-all handler registered AFTER the `/api/v1/*` routes (the server already uses `httpd_uri_match_wildcard`, so exact API routes match first). It:
  1. `sanitizeAssetPath` → 404 (small HTML) on reject.
  2. Opens `<StorageMount::kBasePath>/<path>.gz` (fopen). Missing → 404.
  3. Sets `Content-Type` (from the helper), `Content-Encoding: gzip`, a `Cache-Control` (e.g. `max-age=3600`).
  4. Streams the body with `httpd_resp_send_chunk` (small files; chunked is the idiomatic file pattern) + terminating empty chunk.
  - Unmatched `/api/v1/*` still hits the existing JSON-404 err handler (the catch-all only serves GETs; the err handler covers the API namespace and non-GET unknowns).
- Lifecycle unchanged: the ApiServer is still started lazily by `SystemObserver` on first `WifiState::Connected`. No new task, not watchdog-subscribed, shares no watering mutex (FR-012).
- `max_uri_handlers` is 16 with headroom for one more route.

### JS adaptation (the bulk of the work) — `firmware/web/script.js`
Rewrite ONLY the API-communication layer to the frozen contract (research §2 mapping). Keep DOM/UI logic:
- `GET /api/v1/sensors` — read `environmental.valid` (not `.success`); tolerate `soil.valid:false`/null; new `level.{low,high}`.
- `GET /api/v1/status` + `GET /api/v1/config` + `GET /api/v1/pumps` — status no longer carries pump/config/reservoir; fetch pumps + config separately; `storage` in bytes; `mode` string; `wifi{connected,ip}`.
- Mode toggle → `POST /api/v1/config {wateringEnabled}`; config save → `POST /api/v1/config` JSON with `wateringDurationS`/`minWateringIntervalS` (seconds) + range-error handling.
- Plant run/stop → `POST /api/v1/pumps/plant {action:"run",durationS}` / `{action:"stop"}`; 409/4xx surfaced.
- Reservoir → `POST /api/v1/pumps/reservoir` run/stop + level display only; hide the block when `/api/v1/pumps` has no `reservoir` (rev2). Drop enable/disable + auto-level UI.
- History → `GET /api/v1/history?metric=<...>&range=<...>` (map the old sensor+reading pair to a single `metric`); consume `{timestamps,values}`.
- OTA upload page: a placeholder wired to `POST /api/v1/ota` (currently a 501 stub) — non-functional until PR-13.

### Component boundaries
- `api` component: `+ApiStatic.{h,cpp}` (pure, host-tested) and the target-only static handler in `ApiServer.cpp`. `ApiServer` needs the littlefs base path (`StorageMount::kBasePath`) — a compile-time constant, no new dependency.
- `main`: the CMake gzip/staging pipeline; no app_main logic change (the handler registers inside `ApiServer::start`).

## Testing

- **Host (CI gate):** unit-test the pure helpers — `sanitizeAssetPath` (root→index, traversal/NUL/escape rejection, query strip) and `contentTypeForPath` (each extension + default). Extend `test_apps/host`.
- **CI build:** both boards build; `build/storage.bin` builds with the gz assets; report image size (SC-001).
- **HIL (rev1 rig):** browser loads dashboard, live values, pump start/stop + mode, config persists across reboot, phone browser (SC-002..005). Deferred to the rig; documented in `checklists/hil.md`.

## Risks / decisions

- **Tailwind vendoring:** a committed pre-purged CSS avoids a node toolchain in CI (Constitution III). Risk: if the UI markup changes, the purged CSS must be regenerated (documented). Acceptable for a minimal-adaptation PR.
- **Catch-all vs API 404:** ordering matters — register `/api/v1/*` first, `GET /*` last; verify unmatched `/api/v1/xyz` still returns the JSON envelope, not an asset 404.
- **Contract gaps (reservoir enable/auto-level, status restructuring):** handled by reducing/omitting UI, NOT by touching the frozen contract. If a genuine contract bug surfaces it is escalated as a PR-09 amendment, out of PR-10 scope.
- **Image size:** ~23 KB (base assets) + Chart.js (~60 KB gz) + adapter (~small) + purged Tailwind (small when purged) ≈ under ~120 KB gz — trivially within 960 KiB.

## Constitution check

- I Safety: no watering-path change; file serving is isolated on the httpd task, not watchdog-subscribed. ✅
- II Host-testability: path/content-type helpers are pure + host-tested. ✅
- III Reproducible builds: deterministic Python gzip; no node/Tailwind toolchain in CI; vendored libs pinned. ✅
- IV Frozen legacy: `data/` untouched (copies live in `firmware/web/`). ✅
- V Checkpoint-gated: CP2 stop before implementation. ✅
- VI English/outward. ✅
