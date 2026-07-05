# Branch-coverage Checklist: Watering Controller (011)

**Purpose**: prove every decision + safety branch of the pure controllers is exercised by a host test (the
merge-gating deliverable — 100 % host-tested watering logic). Each row maps a branch from `data-model.md` /
the contracts to the covering test. All boxes are checked only because the full host suite is green.

**Verification run (this branch, `011-watering-controller-host-tests`):**

- [x] Full host suite green: **289 Tests, 0 Failures, 0 Ignored** (`pump_host_tests.elf`, exit 0)
      — of which 23 in `test_watering_controller.cpp` + 14 in `test_reservoir.cpp` are new for feature 011.
- [x] rev1 build green (`sdkconfig.board.rev1_devkit`) — `REV1_RC=0`, app 1037.3 KiB / 1536 KiB slot (32.5 %
      margin).
- [x] rev2 build green (`sdkconfig.board.rev2`) — `REV2_RC=0`, app 1039.4 KiB / 1536 KiB slot (32.3 % margin).
- [x] No new managed dependencies; `dependencies.lock` + esp-modbus (`==2.1.2`) unchanged (T018).

## WateringController — decision branches

- [x] Start burst: enabled + not running + moisture ≤ low + soak elapsed → `runFor(burst)`
      — `test_starts_burst_when_dry_and_enabled`
- [x] No automatic action when watering disabled (manual/suspended mode) — `test_no_start_when_disabled`
- [x] Stop at target: running + moisture ≥ high → `stop()`, arm soak from burst end
      — `test_stops_at_high_threshold`
- [x] Soak gate (FR-003): after a burst ends, no new burst until the pause elapses even while dry; then
      restart — `test_soak_pause_blocks_then_allows_restart`
- [x] Config thresholds/durations re-read each tick (runtime-tunable) — `test_config_change_picked_up_next_tick`
- [x] Low/high thresholds are inclusive boundaries — `test_boundaries_low_and_high_inclusive`

## WateringController — read gating

- [x] No action before the first successful read (gate on read, not placeholder) — FR-004
      — `test_no_action_before_first_successful_read`
- [x] Transient failed read while last-good is fresh → wait, no fail-safe — `test_gate_on_transient_failed_read`

## WateringController — fail-safe (safety branches, FR-005/006)

- [x] Soil unavailable while running → immediate stop + `logFailsafe` — `test_failsafe_unavailable_stops_running_pump`
- [x] Soil stale (> 30 000 ms / never) while running → stop + log — `test_failsafe_stale_stops_running_pump`
- [x] Moisture out of 0..100 → stop + log — `test_failsafe_invalid_moisture_stops_running_pump`
- [x] **Fail-safe is NOT delayed by the soak gate** (checked before it) — `test_failsafe_not_delayed_by_soak`
- [x] Fail-safe during a pending soak pause takes no watering action — `test_failsafe_during_pending_soak_takes_no_action`
- [x] Graceful degradation: constructs + never waters when a sensor failed init — `test_graceful_degradation_never_waters`

## WateringController — manual override (FR-007..010)

- [x] Manual run bypasses fail-safe (runs despite sensor failure) — `test_manual_run_bypasses_sensor_failure`
- [x] Manual duration capped at 300 s — `test_manual_run_capped_at_300s`
- [x] Manual duration lower-clamped to 1 s — `test_manual_run_lower_clamp`
- [x] Auto-started run is NOT flagged manual (fail-safe still applies) — `test_auto_run_is_not_flagged_manual`
- [x] `stop()` clears the override (automatic resumes) — `test_stop_clears_manual_override`

## WateringController — data logging (FR-014)

- [x] Logs at the data-log cadence (interval gate) — `test_data_log_cadence`
- [x] Epoch timestamp + NPK-only-when-≥0 filter — `test_data_log_epoch_and_npk_filter`
- [x] Gated on `isTimeSet()` (no bogus 1970) — `test_data_log_gated_on_time_set`
- [x] Telemetry logs even on the fail-safe path (env still logged, soil skipped) — `test_data_log_runs_on_failsafe_path`

## ReservoirController — truth table (FR-012/013)

- [x] Low invalid → no action (invalid ≠ dry) — `test_invalid_low_no_action`
- [x] High invalid → no action — `test_invalid_high_no_action`
- [x] Both invalid → no action — `test_both_invalid_no_action`
- [x] wet + wet (full) → ensure stopped — `test_full_wet_wet_ensures_stopped`
- [x] wet + dry (sufficient) → no action — `test_sufficient_wet_dry_no_action`
- [x] dry + dry → start fill — `test_low_dry_dry_starts_fill`
- [x] dry + wet (implausible) → no action — `test_implausible_low_dry_high_wet_no_action`

## ReservoirController — running safety + cooldown (FR-012a)

- [x] Stop on high-wet while running — `test_stop_on_high_wet_while_running`
- [x] Max-fill abort at the 300 s cap (MaxRuntimeForced) — `test_max_fill_abort_at_cap`
- [x] Post-abort cooldown blocks a new auto fill, then allows it — `test_cooldown_blocks_then_allows_auto_refill`
- [x] A normal high-wet stop does NOT arm the cooldown — `test_normal_high_wet_stop_does_not_arm_cooldown`
- [x] Manual fill bypasses the cooldown — `test_manual_fill_bypasses_cooldown`
- [x] Manual fill refused when already full (high wet) — `test_manual_fill_refused_when_full`
- [x] Feature disabled forces the pump off and skips all logic — `test_feature_disabled_forces_off_and_skips_logic`

## Not host-covered (target-only / HIL)

These branches are on-target integration, verified by the board builds + the HIL checklist, not the host suite:

- [ ] Periodic soil reader refreshes the `/sensors` cache (controller-as-reader) — HIL §A
- [ ] Watering task watchdog subscribe/feed — HIL (implicit: no TASK_WDT reboot during a cycle)
- [ ] `LockedModbusClient` serializes rs485test with the reader (no bus overlap) — HIL §H
- [ ] Isolation: watering unaffected by WiFi/HTTP load — HIL §G
