# Contract: IConfigStore

**Component**: `firmware/components/interfaces/include/interfaces/IConfigStore.h`
**Feature**: 003-nvs-littlefs-storage

Typed, validated access to persisted configuration. Replaces the config half of
the legacy `IDataStorage` (string-keyed `storeConfig`/`getConfig` over one JSON
blob) — a deliberate contract redesign, same approach as PR-02's `IWaterPump`.

## Operations

| Operation | Contract |
|---|---|
| `getMoistureThresholdLow() -> float` (and analogous typed getters for every item in data-model.md) | Never fails. Returns the stored value if present and in range, otherwise the compiled-in factory default. |
| `setMoistureThresholdLow(float) -> bool` (and analogous typed setters) | Validates against the item's documented range. In-range: persists atomically, returns true, value survives power cycle. Out-of-range: returns false, stored value untouched. |
| `getWifiSsid() / getWifiPassword() -> std::string` | Empty string = unconfigured (factory state). |
| `setWifiCredentials(ssid, password) -> bool` | Length-validated (≤32 / ≤64 bytes). Implementations MUST NOT log the values. |
| `clearWifiCredentials() -> bool` | Returns both items to factory (empty) state. |
| `factoryReset() -> bool` | Every item reads its factory default afterwards; credentials removed. Equivalent to erasing the underlying config storage. |

## Invariants

1. A getter never returns an out-of-range value (defaults shadow invalid storage).
2. A failed/rejected write never alters the stored value (old-or-new, never torn).
3. No operation blocks on or is affected by network state (Constitution I).
4. Header is host-includable: no IDF/hardware includes (Constitution II).
5. Credentials never appear in any diagnostic/log output (spec FR-004).

## Implementations

- `NvsConfigStore` (target + linux-target NVS emulation): one NVS entry per item,
  namespace `wscfg`, schema in data-model.md; factory reset =
  `nvs_flash_erase_partition` + re-init.
- `MockConfigStore` (header-only, `testing/`): in-memory map + call/limit
  instrumentation for consumer tests in later PRs (PR-07, PR-09, PR-11).
- Concurrency: implementations are unsynchronized; cross-task consumers wrap in
  the `Locked*` decorator (research D9, PR-02 CP3 precedent).
