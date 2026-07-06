# Contract: consistent-snapshot helpers on the Locked* sensor wrappers

Resolves the booked `TODO(PR-11)` on `LockedEnvironmentalSensor`/`LockedLevelSensor` (+ the documented
cross-call gap on `LockedSoilSensor`). Each adds ONE locked call returning validity + values together, so a
reader (controller / periodic soil reader / API status / console) never observes a torn read from the
`read()`-then-getters two-lock sequence. Target-safe (`std::mutex`); the underlying pure sensors are
unchanged.

## Helpers (added to the existing wrappers)
```cpp
// LockedSoilSensor
struct SoilSnapshot { bool readOk; bool available; int lastError;
                      float moisture, temperature, humidity, ph, ec, nitrogen, phosphorus, potassium; };
SoilSnapshot snapshot();      // one lock: read() (or last-good) + all getters + availability + error

// LockedEnvironmentalSensor
struct EnvSnapshot { bool valid; float temperature, humidity, pressure; };
EnvSnapshot snapshot();

// LockedLevelSensor
struct LevelSnapshot { bool valid; bool waterPresent; };
LevelSnapshot snapshot();     // update() need not be here; snapshot reads current validity + logical state
```

Semantics: the snapshot is taken under a single mutex acquisition so `valid`/`readOk` and the values are
mutually consistent. For soil, whether `snapshot()` performs a fresh `read()` or returns the last-good
snapshot is decided at implementation — the periodic reader owns the `read()` cadence; the controller/API
consume the last-good snapshot (non-blocking; no bus I/O in a status/API path — QUIRK 5 still holds).

## Consumers updated
- `WateringController` + `ReservoirController` read via these snapshots (no two-lock races).
- The API `/sensors` + `/status` and the diag console read via the snapshot (consistent tuple).
- `sensor_task` may switch its read+error to the snapshot (resolving the `sensor_task.cpp:79` TODO).

## rs485test race fix (D8)
The diag-console `rs485test` currently drives the raw `EspModbusClient` beneath `LockedSoilSensor`'s mutex.
Once the periodic soil reader runs, that is a real RS485 bus race. Route `rs485test` through a locked path
(via the sensor or a locked client) so the two tasks never drive the UART/Modbus concurrently.

## MockSoilSensor coherence
Add `scriptSuccessfulRead(moisture, temp, humidity, ph, ec, n, p, k)` and `scriptFailedRead(error)` to
`MockSoilSensor` (mirroring `MockEnvironmentalSensor`) so host tests script coherent soil state (readOk +
values + availability move together) instead of hand-setting fields into an incoherent combination.
