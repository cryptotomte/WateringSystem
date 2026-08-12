# Quickstart: validate feature 012

All validation is host-side + containerized builds — no hardware. Commands
below mirror `.github/workflows/firmware-build.yml` (the canonical forms).

## 1. Host test suite (contract TUs compile the real header)

```bash
cd firmware/test_apps/host
idf.py --preview set-target linux
idf.py build
./build/pump_host_tests.elf
```

Expected: build succeeds (the contract TUs are compile-time — a contract
violation IS a build failure) and the runtime suite passes with no
regressions.

## 2. Both board targets, pinned container

```bash
docker run --rm -v "$PWD/firmware":/project -w /project espressif/idf:v6.0.1 \
  idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.board.rev1_devkit" build
docker run --rm -v "$PWD/firmware":/project -w /project espressif/idf:v6.0.1 \
  idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.board.rev2" build
```

(CI additionally greps `firmware/sdkconfig` for `CONFIG_BOARD_REV1_DEVKIT=y` /
`CONFIG_BOARD_REV2=y` to prove the right profile was built — do the same if
building both locally back-to-back, and clean between runs so the second
build does not inherit the first's sdkconfig.)

## 3. Truth checks (SC-004)

```bash
grep -rn "TODO(SYNC1)\|provisionally" firmware/components/board/ && echo FAIL || echo OK
```

## 4. Negative proof (optional, high-value)

Temporarily add an unguarded `BOARD_PIN_BTN_CONFIG` reference to the rev2
contract TU — the host build must FAIL. Revert. (Demonstrates the enforcement
pattern actually bites; the rev1 TU documents this technique.)
