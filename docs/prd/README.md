# PRD breakdown — ESP-IDF migration

This directory breaks the master PRD ([`docs/PRD-esp-idf-migration.md`](../PRD-esp-idf-migration.md))
into PR-sized work packages for phases 1–6 of the Arduino → ESP-IDF firmware migration.

## How these files are used (spec-kit workflow)

Each `PR-NN-short-name.md` is a mini-PRD that seeds exactly one spec-kit feature run:

```
docs/prd/PR-NN-short-name.md
        │  speckit.specify / clarify / plan / tasks
        ▼
branch NNN-short-name  +  specs/NNN-short-name/ (spec.md, plan.md, tasks.md, checklists)
        │  implementation (subagents), checkpoints 1–3
        ▼
one reviewable PR to main (merge commit, never squash)
```

Rules (from the master PRD's working-method section):

- One PR-NN file → one feature branch → one PR. Each PR must merge with CI green.
- Implementer always delivers a test checklist: **CI**-tagged acceptance criteria are
  verified by GitHub Actions; **HIL**-tagged criteria become a checklist Paul runs on
  the bench rig (rev1 devkit + RS485 + soil sensor) at checkpoint 3.
- All commits, code, docs and issues in English (public repo).
- Order below keeps CI green at every merge point: no PR depends on unmerged work.

## Sync gates (parallel hardware track)

| Gate | Event | Gates |
|---|---|---|
| **SYNC 1** | rev2 pin map frozen (KiCad schematic done) | Final `BOARD_REV2` pin table in the board component (PR-02 ships rev1 table + provisional rev2 table; rev2 table is finalized at SYNC 1, at the latest before PR-14) |
| **SYNC 2** | rev2 boards delivered (JLCPCB) | All of phase 6 (PR-14) |

Phases 1–5 are developed and verified entirely on the rev1 devkit rig; the greenhouse
unit is never touched.

## Dependency graph

```
PR-01 phase0-foundation (done — this PR)
  │
  ├─► PR-02 pump-gpio-board ──◄ SYNC 1 (final rev2 pin table)
  │     ├─► PR-03 bme280-i2c
  │     ├─► PR-04 modbus-soil-sensor
  │     └─► PR-05 level-sensors-ina226
  │
  ├─► PR-06 nvs-littlefs-storage
  │     └─► PR-07 wifi-provisioning
  │           └─► PR-08 sntp-watchdog-logging
  │
  │   PR-06 + PR-07 ──► PR-09 http-server-api-v1 ──► PR-10 frontend-littlefs-assets
  │                          (live data: PR-03/04/05)
  │
  │   PR-02..05 + PR-06 + PR-08 ──► PR-11 watering-controller-host-tests
  │
  │   PR-09..11 ──► PR-12 parity-validation (HIL)
  │
  │   PR-06 + PR-09 ──► PR-13 ota-release
  │
  └────────────────► PR-14 rev2-bringup ◄── SYNC 2 (boards delivered)
                          (after PR-12 + PR-13)
```

## Work packages

| PR | Title | Phase | Depends on | Sync gates | Size | Status |
|---|---|---|---|---|---|---|
| PR-01 | [phase0-foundation](PR-01-phase0-foundation.md) | 0 | — | — | L | **done** (this PR) |
| PR-02 | [pump-gpio-board](PR-02-pump-gpio-board.md) | 1 | PR-01 | SYNC 1 (final rev2 table) | M | pending |
| PR-03 | [bme280-i2c](PR-03-bme280-i2c.md) | 1 | PR-02 | — | M | pending |
| PR-04 | [modbus-soil-sensor](PR-04-modbus-soil-sensor.md) | 1 | PR-02 | — | M | pending |
| PR-05 | [level-sensors-ina226](PR-05-level-sensors-ina226.md) | 1 | PR-02 | — | M | pending |
| PR-06 | [nvs-littlefs-storage](PR-06-nvs-littlefs-storage.md) | 2 | PR-01 | — | M | pending |
| PR-07 | [wifi-provisioning](PR-07-wifi-provisioning.md) | 2 | PR-06 | — | L | pending |
| PR-08 | [sntp-watchdog-logging](PR-08-sntp-watchdog-logging.md) | 2 | PR-06, PR-07 | — | M | pending |
| PR-09 | [http-server-api-v1](PR-09-http-server-api-v1.md) | 3 | PR-06, PR-07 (live data: PR-03/04/05) | — | L | pending |
| PR-10 | [frontend-littlefs-assets](PR-10-frontend-littlefs-assets.md) | 3 | PR-09 | — | M | pending |
| PR-11 | [watering-controller-host-tests](PR-11-watering-controller-host-tests.md) | 4 | PR-02..05, PR-06, PR-08 | — | L | pending |
| PR-12 | [parity-validation](PR-12-parity-validation.md) | 4 | PR-09, PR-10, PR-11 | — | M | pending |
| PR-13 | [ota-release](PR-13-ota-release.md) | 5 | PR-06, PR-09 | repo-publication decision | L | pending |
| PR-14 | [rev2-bringup](PR-14-rev2-bringup.md) | 6 | PR-05, PR-12, PR-13 | **SYNC 2** | L | pending |

## Notes on the split

- The breakdown follows the master PRD's phase definitions (Fasdefinitioner table)
  one-to-one; each phase's completion criterion is covered by the last PR of that phase.
- PR-02 carries the board component (pin maps, Kconfig feature flags) because the pump
  driver is the smallest driver and every other phase-1 PR consumes the board component.
- INA226 rides with the level sensors in PR-05 (both are small, rev2-relevant drivers;
  INA226 is driver + raw measurements only — protection features are an explicit
  non-goal, see master PRD "Ingår EJ").
- PR-12 (parity validation) is a thin PR by design: its deliverable is the ticked-off
  `docs/parity-checklist.md` plus gap fixes found on the rig — kept separate from PR-11
  so the host-tested controller port merges without waiting for rig time.
