# Contract: Provisioning portal (standalone HTTP)

Target-only class `ProvisioningPortal` (`firmware/components/network/.../ProvisioningPortal.{h,cpp}`),
excluded from the linux build. Runs a **minimal standalone `esp_http_server`** while the device is in
`Provisioning` (AP) mode. The full JSON status API and `/api/wifi/scan` are **out of scope** (PR-09).

## Endpoints

| Method | Path | Behavior |
|---|---|---|
| `GET` | `/` (and unknown paths) | Serve the WiFi setup page (HTML form: SSID text field, password field, submit). Small, self-contained, English. |
| `POST` | `/wifi/config` (path finalized at impl; parity used `/wifi/config`) | Accept form params `ssid`, `password`. See flow below. |

## POST /wifi/config flow

1. Parse `ssid` and `password` from the form body.
2. Call pure `validateWifiCredentials(ssid, password)`:
   - **Reject** (SSID not 1–32, or non-empty password < 8, or > 64): respond 4xx with a short error, do
     **not** persist, do **not** restart. Device stays in Provisioning (FR-005).
3. On accept: `IConfigStore::setWifiCredentials(ssid, password)`.
   - If persist fails (`false`): respond 5xx, stay provisionable.
4. On persist success: respond 200 with a success page indicating `restartRequired`, then **schedule a
   restart ~3 s later** (so the response is delivered before reboot) — FR-007.

## Guards

- Credential submission is honored **only in AP/provisioning mode** (FR-006). (In this design the portal
  server only runs while provisioning, which structurally enforces it; if the server could ever be up in
  STA mode, the handler must reject.)
- The portal never echoes the submitted password back and never logs credential values (FR-004 / PR-06
  convention).
- Setup page + assets must keep the app binary within the 1.5 MiB OTA slot (research R2).

## Parity / overlap notes

- Legacy served `wifi_setup.html` as the AP-mode default file at 192.168.4.1 with a ~3 s post-save
  restart (`docs/parity-checklist.md` §7). This portal reproduces that reachable-provisioning-UI parity
  requirement; exact page markup is an implementation detail.
- The diag console `config wifi <ssid> <pass>` / `wifi-clear` path writes the **same** `IConfigStore`
  credentials — both paths must agree (HIL check).
- Captive-portal DNS redirect and mDNS/hostname niceties are **not** implemented unless free with the
  chosen components (PR brief: do not gold-plate).
