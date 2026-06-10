# PR-10: frontend-littlefs-assets

> Phase 3 — web (completes phase 3 exit criteria)

## Goal

Serve the existing web frontend from littlefs with minimal adaptation to `/api/v1/`,
acting as the test client that proves the API contract end to end.

## Scope

- Asset pipeline: existing frontend files (≈118 KB in `data/` today) copied into the
  firmware littlefs image, **gzip-compressed** at build time (expected ~30–40 KB;
  the partition plan's headroom assumes gzipped assets).
- Static file serving from littlefs via `esp_http_server` with correct
  `Content-Encoding: gzip`, content types, and cache headers.
- **Minimal** JS adaptation: point existing fetch/XHR calls at `/api/v1/` endpoints
  and adjust to the JSON shapes frozen in PR-09. No redesign, no framework — full
  frontend modernization is explicitly a separate PRD (master PRD "Ingår EJ").
- Required UI functions (parity): dashboard with live sensor readings, pump
  manual control + mode switch, configuration page, system status. Plus a manual OTA
  upload page placeholder wired up in PR-13.
- Build integration: littlefs image generated and flashed/embedded via the standard
  `idf.py` flow; CI artifact includes the assets image.

## Out of scope

- New UI features, visual refresh, frontend framework adoption (separate PRD).
  API changes (contract is frozen; if adaptation reveals a contract bug, fix goes
  through an explicit PR-09 contract amendment).

## Functional requirements covered

- FR8 (assets from littlefs, frontend minimally adapted as test client).

## Dependencies

- PR-09 (frozen `/api/v1/` contract), PR-06 (littlefs mount).

## Acceptance criteria

- [CI] littlefs image with gzipped assets builds in CI for both targets; image size
  reported and comfortably within the 960 K partition.
- [HIL] Browser on the LAN loads the dashboard from the rig; live sensor values
  update; pump can be started/stopped and mode switched from the UI.
- [HIL] Configuration changes made in the UI persist across reboot.
- [HIL] Page load works on a phone browser (the realistic greenhouse client).

## Estimated size

M
