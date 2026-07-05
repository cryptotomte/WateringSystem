# Contract: `ApiServer` (target-only esp_http_server plumbing)

`firmware/components/api/include/api/ApiServer.h` (+ `src/ApiServer.cpp`), excluded from the linux build.
Models the `ProvisioningPortal` server precisely (opaque handle; logic in the pure layer). No host tests —
HIL-verified via the curl checklist. No business/watering logic here.

## Construction & lifecycle

- Constructed as a function-local static in the `app_main` STATION branch, holding references to the
  `Locked*` decorators (`LockedWaterPump` plant [+ reservoir], `LockedEnvironmentalSensor`,
  `LockedSoilSensor`, `LockedLevelSensor` ×2, `LockedPowerSensor` rev2, `LockedConfigStore`,
  `LockedDataStorage`), `LockedWifiManager`, `IWallClock`/`SntpClient`, `ITimeProvider` (uptime), and the
  app descriptor (version). Same wiring style as `diag_console_register_*`.
- `bool start()` — `HTTPD_DEFAULT_CONFIG()` with `max_uri_handlers` raised to cover the route set; a bounded
  request-body cap; `httpd_start`; register each `ApiRoutes` entry via `httpd_register_uri_handler` with
  `user_ctx = this`. Idempotent; non-fatal on failure (log + continue — never blocks watering).
- Started on the first `WifiState::Connected` transition (via `SystemObserver`, mirroring SNTP start) or in
  the station branch after `begin(Station)`. Never started in AP/provisioning mode (the provisioning portal
  owns AP). `stop()` on disconnect is optional (esp_http_server tolerates STA down).

## Per-request flow (thin)

1. Recover `ApiServer*` from `req->user_ctx`.
2. For POST: read the body with a `httpd_req_recv` loop up to the size cap (reject oversize → 400).
3. Call the **pure** parser/validator (`parseConfigSet`/`parsePumpCommand`) → struct or error.
4. Read the relevant `Locked*` interface(s) into a plain DTO (non-blocking getters only) / call the
   actuator command.
5. Call the **pure** serializer (`serialize*Dto`) → `std::string`.
6. `httpd_resp_set_type(req, "application/json")`, set the status line, `httpd_resp_sendstr`.
- Unknown `/api/*` → the JSON 404 (register a wildcard/`404` handler that emits the error envelope).
- Device IP for `/status` via `esp_netif_get_ip_info` here (target-only).

## Safety / isolation (FR-015)

- Runs on the `esp_http_server` task(s); NOT WDT-subscribed; shares no mutex with the watering loop beyond
  the `Locked*` wrappers. A slow/stalled/flooding client cannot delay the 10 Hz loop or the sensor task.
- Handlers make NO watering decisions and never call blocking `read()`/`isAvailable()` (except the explicit,
  bounded `/selftest`).
- `pumps_force_off()` remains the first `app_main` action; the server is constructed strictly after it.

## `LockedWifiManager` (network component, NEW)

Thin mutex decorator over `WifiManager` exposing a synchronized `snapshot()` for the console + API readers
(the wifi task remains the single writer). Resolves `WifiState.h:49 TODO(PR-09)`.
