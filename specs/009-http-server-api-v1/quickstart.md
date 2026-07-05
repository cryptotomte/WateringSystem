# Quickstart / Validation Guide: HTTP REST/JSON API v1

CI-tagged items gate the merge; HIL-tagged items run on the rev1 rig at Checkpoint 3.

## Prerequisites

- ESP-IDF v6.0.1 via Docker. Docker cannot mount OneDrive — rsync first:
  `rsync -a --delete --exclude build --exclude managed_components --exclude sdkconfig "$PWD/firmware/" /tmp/ws009-firmware/`
- Between board builds: `idf.py fullclean` + `rm -f sdkconfig` (a leftover sdkconfig is not overridden by
  `-DSDKCONFIG_DEFAULTS`).

## CI validation (host + both board builds)

### Host tests — `run_api_serialize_tests/ run_api_requests_tests / run_api_routes_tests`
- **Serializers**: golden/round-trip JSON for status/sensors/history/pumps/config/events/power DTOs; valid
  flags correct; wifi password never present; not-set time renders correctly.
- **Request parse/validate**: `parseConfigSet` accepts in-range subsets, rejects each out-of-range field
  (moisture 0..100, duration 1..300, interval ≥1, sensorRead ≥1000, dataLog ≥60000) with all-or-nothing;
  `parsePumpCommand` accepts start/run(1..300)/stop, rejects bad action / duration.
- **Route-table contract**: `matchRoute` resolves every documented route and returns NotFound for unknown
  `/api/*`; the route set equals the `docs/api/openapi.yaml` path set (FR-004).

```bash
docker run --rm -v /tmp/ws009-firmware:/project -w /project espressif/idf:v6.0.1 \
  bash -lc "cd test_apps/host && idf.py --preview set-target linux && idf.py build && ./build/pump_host_tests.elf"
```

### Both board targets build + size
```bash
for b in rev1_devkit rev2; do
  docker run --rm -v /tmp/ws009-firmware:/project -w /project espressif/idf:v6.0.1 \
    bash -lc "idf.py fullclean >/dev/null 2>&1; rm -f sdkconfig; \
      idf.py -DSDKCONFIG_DEFAULTS='sdkconfig.defaults;sdkconfig.board.$b' build"
done
```
- **[CI]** both targets build; `idf.py size` confirms the app fits the 1.5 MiB OTA slot; `dependencies.lock`
  + esp-modbus pins unchanged (only IDF built-ins `esp_http_server`/`json`/`esp_netif` added).
- **[CI]** OpenAPI sketch committed under `docs/api/` and referenced from `firmware/CLAUDE.md`.

## HIL validation (rev1 rig — Checkpoint 3, scripted curl)

1. **Status**: `curl /api/v1/status` → JSON with mode/wifi(ip,rssi,ssid)/time/uptime/reset/firmware/storage.
2. **Sensors (non-blocking)**: `curl /api/v1/sensors` returns fast (env + level fresh; soil valid=false
   until PR-11); confirm it does NOT stall while an RS485/console read is in progress (QUIRK 5).
3. **Pumps**: `GET /api/v1/pumps` lists the board's pumps; `POST` start a timed run → pump runs and
   auto-stops within the 300 s cap; stop works; start-on-running returns an explicit error.
4. **Config**: `GET /api/v1/config`; `POST` a valid change persists (survives reboot); an out-of-range value
   returns 400 with an error body and changes nothing; wifi password never appears anywhere.
5. **History / events**: `GET /api/v1/history?metric=env_temperature&range=24h` returns aligned series (or
   empty); `GET /api/v1/events` lists recent events newest-first.
6. **Self-test / OTA stub**: `POST /api/v1/selftest` returns a structured result; `POST /api/v1/ota`
   returns the defined stub.
7. **Robustness**: malformed JSON → 400; unknown `/api/*` → JSON 404; a stalled/flooding client causes zero
   disruption to watering timing (pull the trigger during an active pump run).
8. **rev2**: `/api/v1/power` returns INA226 data; on rev1 it is not-available per contract.

## Definition of done

- Host suite green (0 failures); both targets build + fit the slot.
- OpenAPI sketch reviewed and marked frozen at merge; referenced from `firmware/CLAUDE.md`.
- HIL curl checklist (`specs/009-http-server-api-v1/checklists/hil.md`) executed on the rig, or deferred
  with rationale (deferred-HIL register) — note the soil/power staleness limitation (PR-11).
