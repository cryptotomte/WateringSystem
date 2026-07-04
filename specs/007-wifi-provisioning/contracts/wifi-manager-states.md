# Contract: `WifiManager` state machine & reconnect timing

Pure class (`firmware/components/network/include/network/WifiManager.h`, compiled on host + target).
Dependencies (constructor-injected): `IWifiDriver&`, `IConfigStore&`, `ITimeProvider&`, `ReconnectPolicy`
(defaults = parity constants). **No watering/pump/sensor dependency** — this is the FR-014 guarantee and a
host-test assertion (the type does not compile with such a reference; tests document the dependency set).

## Public surface (proposed)

| Member | Contract |
|---|---|
| `void begin(WifiBootMode mode)` | Enter `Provisioning` or `Station` (from `decideBootMode`). In Station, issues the first `staConnect`. In Provisioning, `apStart` + (portal started by caller). |
| `void tick()` | Advance the state machine using `ITimeProvider::nowMs()` and drained `pollEvent()`s. Non-blocking; called at a fixed cadence from the wifi task. |
| `WifiConnectionSnapshot snapshot() const` | Single-acquisition consistent copy of state+counters+rssi for status/LED consumers. |

## Timing contract (parity — the core host-test target)

Using `FakeTimeProvider` (no wall clock), with `MockWifiDriver` scripting outcomes:

1. **Connect success**: `begin(Station)` → `staConnect` called once → script `Connected`+`GotIp` →
   `tick()` → state `Connected`, `consecutiveFailures == 0`.
2. **Single retry cadence**: from `Connecting`, script `ConnectFailed` → state `Reconnecting`; **no** new
   `staConnect` until `advance(10_000)`; at exactly 10 s a new attempt is issued. Assert no attempt at
   9 999 ms, one attempt at 10 000 ms.
3. **Pause after 5 failures**: script 5 consecutive `ConnectFailed` (advancing 10 s each) → after the 5th,
   state `ReconnectPaused`; assert **no** `staConnect` during the pause; the next attempt occurs only
   after `advance(60_000)`, and `consecutiveFailures` resets to 0 for the new round.
4. **Monitor cadence**: in `Connected`, monitoring evaluates every 5 s; a scripted `Disconnected` →
   `Reconnecting`, `disconnectCount` incremented.
5. **No boot loop (FR-013)**: script an infinite `ConnectFailed` stream over many rounds → the machine
   only ever cycles `Reconnecting`↔`ReconnectPaused`; assert it never requests a restart and memory/state
   stays bounded.
6. **AP monitoring suspended**: in `Provisioning`, `tick()` performs no STA monitoring or reconnect
   attempts regardless of elapsed time.
7. **Isolation (FR-014)**: a driver whose `pollEvent()` always returns `None` and whose `staConnect`
   "hangs" (never events) must not cause `tick()` to block — `tick()` returns promptly every call.

## Boot-mode contract

`decideBootMode(bool credentialsPresent, bool configButtonHeld) -> WifiBootMode`:

| credentialsPresent | configButtonHeld | result |
|---|---|---|
| false | false | Provisioning |
| false | true  | Provisioning |
| true  | false | Station |
| true  | true  | Provisioning (emergency; caller clears credentials first) |

All four rows are host-tested.
