# Contract: Serial Diagnostic (esp_console REPL)

**Feature**: 002-pump-gpio-board — rig-testing tool (HIL acceptance). Temporary
scope: full FR12 diagnostics arrive in later phases.

## Transport

UART0 console (same port as logs), `esp_console` REPL, prompt `ws>`.
`help` is provided by the console component automatically.

## Command grammar

```
pump <plant|reservoir> start <seconds>   # timed run; 1..300
pump <plant|reservoir> stop
pump <plant|reservoir> status
pump status                              # both pumps
```

## Responses (line-oriented, human-readable, stable enough for HIL checklist)

| Command outcome | Output (example) |
|---|---|
| start accepted | `OK plant running for 10 s` |
| start rejected — already running | `ERR plant already running (12 s elapsed)` |
| start rejected — bad duration | `ERR duration must be 1..300 s` |
| stop | `OK plant stopped (reason=commanded, ran 4.2 s)` |
| stop when stopped | `OK plant already stopped` |
| status | `plant: stopped, last stop=duration_elapsed, total runtime 34.5 s` / `reservoir: running 12.0/60.0 s` |

Exit codes: console handler returns 0 on OK, 1 on ERR (esp_console convention).

## HIL mapping (spec SC-005, acceptance scenarios US1)

1. Boot rig → no pump output before any command (scope/meter GPIO 26/27).
2. `pump plant start 10` → pump on; self-stop after 10 s (±1 s).
3. `pump plant start 10` then `pump plant stop` → immediate stop.
4. `pump reservoir start 300` → forced stop at 300 s, `ERR`/warning logged,
   `status` shows `last stop=max_runtime_forced`.
5. `pump plant start 0` and `pump plant start 301` → rejected with clear error.
6. `pump plant start 60`, then hard-reset the board mid-run (EN/reset button) →
   pump output is off immediately from boot and stays off (boot fail-safe; spec
   US1 scenario 1 / edge case "reset while running").
