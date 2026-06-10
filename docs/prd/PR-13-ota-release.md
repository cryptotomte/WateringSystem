# PR-13: ota-release

> Phase 5 — OTA & release

## Goal

A/B OTA with automatic rollback, self-update from GitHub Releases via `esp_https_ota`,
manual web upload as fallback, and the GitHub release workflow — after this PR, no
cable is ever needed again (master PRD success criterion).

## Scope

- A/B app partitions are already in place (PR-01: ota_0/ota_1 à 1.5 MB + otadata);
  this PR activates them: `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`, app marks itself
  valid (`esp_ota_mark_app_valid_cancel_rollback`) only after a boot health check
  (pumps initialized OFF, storage mounted, controller task running, watchdog armed).
- **Automatic rollback:** failed verification or crash-before-valid ⇒ bootloader falls
  back to the previous slot. Must be demonstrated with a deliberately broken build
  (phase 5 exit criterion).
- `esp_https_ota` from **GitHub Releases**: version check against the latest release,
  download over TLS (server cert handling decided in plan — bundle vs ESP x509 store),
  triggered via the `/api/v1/` OTA endpoint stubbed in PR-09 (and optionally a
  periodic check, config-gated, default off).
- **Manual upload fallback:** firmware image upload through the web UI (page stubbed
  in PR-10) writing to the inactive slot — works without internet access.
- Safety: pumps forced OFF before reboot into new image; OTA refused while a pump is
  running unless explicitly forced; all OTA events persisted to the event log (PR-08).
- **Release workflow** (`.github/workflows/`): tag-triggered build of both board
  targets, attach `firmware-rev1_devkit.bin` / `firmware-rev2.bin` (+ littlefs image)
  to a GitHub Release with embedded version (`version.txt`/`esp_app_desc`).
- Gate noted from master PRD open questions: GitHub-OTA from a public repo requires
  the **repo-publication/secrets review decision** (AP_PASSWORD already in history;
  rotate before rev2 deployment). If unresolved at implementation time, ship with a
  token-based release fetch and document it.

## Out of scope

- Signed firmware / secure boot (not in master PRD scope). Frontend redesign of the
  update page. Migration of the Arduino unit (it keeps its own legacy flow).

## Functional requirements covered

- FR11 (A/B OTA, rollback, esp_https_ota from GitHub Releases, manual upload).

## Dependencies

- PR-06 (NVS/littlefs), PR-09 (OTA endpoint contract), PR-10 (upload page), PR-08
  (event log). External: repo-publication decision (see above).

## Acceptance criteria

- [CI] Release workflow produces a GitHub Release with flashable artifacts for both
  targets on tag push.
- [HIL] Rig self-updates from a real GitHub Release via the API trigger and boots the
  new version (version visible in `/api/v1/` system info).
- [HIL] Deliberately broken build (panics before marking valid) is flashed OTA; the
  rig automatically rolls back to the previous working version. **Phase 5 gate.**
- [HIL] Manual web upload of a .bin updates the rig without internet access.
- [HIL] OTA attempt while plant pump is running is refused; pumps are OFF immediately
  after the OTA reboot.

## Estimated size

L
