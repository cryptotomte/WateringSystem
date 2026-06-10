# Quickstart: Pump Actuator Layer and Board Abstraction

**Feature**: 002-pump-gpio-board — validation guide (no implementation details;
see [plan.md](plan.md) and [contracts/](contracts/)).

## Prerequisites

- Docker with the pinned image `espressif/idf:v6.0.1` (no local toolchain needed).
- Local note (macOS/OneDrive): copy the tree to `/tmp` before mounting in Docker —
  the OneDrive path cannot be docker-mounted.
- HIL: rev1 devkit rig, scope/meter on GPIO 26/27, serial terminal 115200 baud.

## 1. Build both boards (CI does the same)

```bash
cd firmware
docker run --rm -v "$PWD":/project -w /project espressif/idf:v6.0.1 \
  idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.board.rev1_devkit" build
# fullclean or fresh copy, then:
docker run --rm -v "$PWD":/project -w /project espressif/idf:v6.0.1 \
  idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.board.rev2" build
```

Expected: both green; boot banner reports the right board name.

## 2. Host tests (the new CI gate)

```bash
cd firmware/test_apps/host
docker run --rm -v "$PWD":/project -w /project espressif/idf:v6.0.1 bash -c \
  "idf.py --preview set-target linux && idf.py build && ./build/*.elf"
echo $?   # 0 = all tests pass; >0 = number of failures
```

Expected coverage: max-runtime enforcement, duration self-stop, rejected starts
(0, >300, already-running), paired output transitions, runtime statistics — all on
`FakeTimeProvider` (deterministic, no sleeps).

## 3. HIL on the rig (CP3 checklist)

Flash rev1 build, open serial terminal, then follow
[contracts/serial-diagnostic.md](contracts/serial-diagnostic.md) §HIL mapping:
boot-off check, timed self-stop, manual stop, 300 s forced stop, rejection cases.

## 4. CI

`firmware-build.yml` gains a `host-test` job; the build matrix is unchanged.
A PR is green when: both board builds pass + board-config grep passes + host test
executable exits 0.
