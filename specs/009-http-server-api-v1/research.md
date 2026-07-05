# Phase 0 Research: HTTP REST/JSON API v1

Grounded in the verified codebase map (origin/main, PR-01..08 merged) + `docs/prd/PR-09-*.md` +
`docs/parity-checklist.md` §4/QUIRK 5. No open `NEEDS CLARIFICATION`.

## D1 — `api` component: pure serialize/parse/route-table + target-only `ApiServer`

- **Decision**: New `api` component split like sensors/network — pure JSON serializers, request
  parser/validators, and the route table (a plain data structure) compiled on host + target and
  host-tested with cJSON; a thin target-only `ApiServer` wires `esp_http_server` to them.
- **Rationale**: `esp_http_server` has no linux port (that is why `ProvisioningPortal.cpp` is target-only),
  but `json` (cJSON) does link on the linux preview target. Putting all logic behind plain structs makes
  serialization/validation/route coverage host-testable (Constitution II), mirroring the established
  pure/hardware split.
- **Alternatives**: serialize directly in handlers from `httpd_req_t` — rejected (untestable on host,
  couples logic to IDF).

## D2 — Separate `esp_http_server` in station mode; cJSON via managed `espressif/cjson`

> **Correction (implementation, 2026-07-05):** IDF v6.0.1 has NO built-in `json` component (removed →
> registry). cJSON is the pinned managed dep `espressif/cjson ==1.7.19~2` in
> `firmware/components/api/idf_component.yml`; the api component `REQUIRES ... cjson ...`. Pure C, builds on
> both esp32 targets and the linux preview target (verified: host 191/0, rev1+rev2 green). `dependencies.lock`
> gains cjson (deliberate pinned addition; esp-modbus pins untouched). The `REQUIRES json` note below is
> superseded by `cjson`.

- **Decision**: The API is a **separate** `esp_http_server` instance started in the station branch of
  `app_main` (gated on the first `WifiState::Connected`, mirroring how `SystemObserver` starts SNTP). cJSON
  via `REQUIRES json`. Follow the `ProvisioningPortal` server idiom: opaque `void*` handle in the header,
  `HTTPD_DEFAULT_CONFIG()` with `max_uri_handlers` raised for the route set, `httpd_req_recv` body loop
  with a size cap, `user_ctx=this`, `application/json` responses.
- **Rationale**: the provisioning portal is AP-mode-only and never runs in STA (confirmed) — the two
  servers are mode-exclusive, so no port conflict. Reusing IDF built-ins keeps `dependencies.lock` clean.
- **Alternatives**: one shared server across AP+STA — rejected (unneeded coupling; AP/STA are exclusive).
  A registry-managed JSON lib — rejected (cJSON ships with IDF, no new managed dep).

## D3 — Non-blocking reads only (QUIRK 5); soil/power served last-good with a valid flag

- **Decision**: Handlers read ONLY the non-blocking cached getters (`getTemperature/…`, soil `getMoisture/
  …`, power `getBusVoltage/…`, level `isValid/isWaterPresent/rawState`) through the `Locked*` wrappers.
  They MUST NOT call `read()`/`isAvailable()` (those block on the bus). Each sensor section carries a
  `valid` flag derived from the cached-value validity (`getLastError()` / NaN check).
- **Rationale**: QUIRK 5 — the legacy handler blocked on a synchronous Modbus read; the target must not.
  Env is refreshed by `sensor_task` (5 s) and level by the 10 Hz loop, so those are fresh. **Soil &amp; power
  have no periodic refresher today** (soil's reader lands in PR-11; power is on-demand only), so the API
  serves their last-good/NaN values with `valid=false` until a fresh read exists. This is honest and
  in-scope (PR-09 doc defers soil polling to PR-11).
- **Alternatives**: a blocking `read()` in the handler — rejected (violates FR-015/QUIRK 5, could stall the
  httpd task); adding soil/power poll tasks now — rejected as PR-11 scope creep (recorded as a limitation).

## D4 — "mode" maps onto `wateringEnabled` (no new state)

- **Decision**: The manual/automatic mode switch maps onto the existing `IConfigStore::getWateringEnabled/
  setWateringEnabled` flag (automatic ⇒ enabled). Status reports the flag as `mode`.
- **Rationale**: no system-mode interface exists; `wateringEnabled` is the persisted flag PR-11's
  controller will consume. Introducing a parallel mode state would duplicate it.
- **Alternatives**: a new mode enum in config — rejected (redundant with `wateringEnabled`, and PR-11 owns
  the semantics).

## D5 — `LockedWifiManager` for the cross-task status read; device IP via `esp_netif`

- **Decision**: Add a `LockedWifiManager` decorator (network component) wrapping `snapshot()` under a mutex,
  and route the API status handler + the console + the wifi task through the appropriate accessor. The
  status handler obtains the device IP via `esp_netif_get_ip_info` (target-only, in the handler) since the
  snapshot carries only `ipAcquired`.
- **Rationale**: `WifiState.h:49` already books `TODO(PR-09): LockedWifiManager` — PR-09 is the first
  cross-task reader beyond the single-writer wifi task, so the synchronized snapshot is now load-bearing.
  IP is a target-only concern; keeping it in the handler avoids polluting the pure snapshot.
- **Alternatives**: extend `WifiConnectionSnapshot` with an IP string — rejected (pushes a target concern
  into the pure struct; the handler can query netif directly).

## D6 — Envelope, errors, validation, no secrets

- **Decision**: A consistent JSON envelope (`{ "success": bool, "error"?: string, ...payload }`). Unknown
  `/api/*` routes → JSON 404. Config-set validates via the `IConfigStore` setters (range constants in the
  header) — any out-of-range/malformed field → 4xx error body, whole request rejected, nothing changed.
  `getWifiPassword()` is NEVER serialized (FR-004); status reports SSID/configured-state only.
- **Rationale**: matches parity §4 (JSON 404 for `/api/*`) and the config setters' existing validation;
  all-or-nothing config apply avoids inconsistent partial state (spec edge case).

## D7 — Frozen OpenAPI 3 sketch + route-table contract test

- **Decision**: Commit `docs/api/openapi.yaml` (OpenAPI 3) describing every endpoint; mark frozen at merge.
  The route table (`ApiRoutes.h`, pure data) is asserted in a host test against the set of paths in the
  sketch, so "every documented endpoint has a registered handler" is CI-verified without hardware.
- **Rationale**: FR-003/FR-004; the contract is itself a deliverable and the input to the frontend (PR-10).
- **Alternatives**: generate handlers from the sketch — rejected (over-engineering; a static route table +
  a contract assertion is enough).

## D8 — OTA-trigger endpoint is a stub

- **Decision**: The OTA-trigger endpoint is defined in the contract and returns a defined stub response
  (e.g. `501`/"not implemented" or an accepted-but-noop shape agreed in the OpenAPI sketch); no OTA logic.
- **Rationale**: PR-13 owns OTA execution; PR-09 only freezes the contract surface.

## Open risks

- **R1 — soil/power staleness** (D3): until PR-11's soil reader, those sections report `valid=false` / NaN
  on the rig. HIL should confirm the endpoint shape is correct and env/level are fresh; note the limitation.
- **R2 — httpd RAM** (`max_uri_handlers`, per-request buffers): size the route count and body cap to the
  device RAM; flash headroom is fine (~470 KiB), RAM is the constraint to watch at HIL.
- **R3 — binary size**: confirm `idf.py size` both boards still fit 1.5 MiB after cJSON + the API.
