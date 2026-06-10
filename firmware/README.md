# WateringSystem firmware (ESP-IDF)

ESP-IDF based firmware for the WateringSystem greenhouse controller
(ESP32-WROOM-32E, 4MB flash). This is the successor to the frozen
Arduino/PlatformIO firmware in the repository root.

- **ESP-IDF version:** v6.0.1 (pinned — in `Dockerfile` and CI)
- **Chip target:** `esp32`
- **Language:** C++ (IDF defaults, ~C++23 feature level)

## Building without a local toolchain (recommended)

No local ESP-IDF installation is needed. Always build with an **explicit
board selection**. From this `firmware/` directory:

```bash
# Rev 1 — ESP32 devkit + breakout (TXS0108E + SP3485)
docker run --rm -v "$PWD":/project -w /project espressif/idf:v6.0.1 \
  idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.board.rev1_devkit" build

# Rev 2 — custom PCB (THVD1426 auto-direction, INA226, CP2102N)
docker run --rm -v "$PWD":/project -w /project espressif/idf:v6.0.1 \
  idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.board.rev2" build
```

> **Warning:** omitting `SDKCONFIG_DEFAULTS` silently builds **rev1_devkit**
> (the Kconfig default), and the binaries are **identically named** for both
> boards. The two boards differ in safety-relevant ways (e.g. inverted level
> sensor polarity on rev2). Before flashing rev2 hardware, always verify the
> `Board:` line in the boot log matches the hardware.
>
> **Warning:** when switching boards, delete the generated `sdkconfig` (or
> run `idf.py fullclean`) first — overlays are only applied when `sdkconfig`
> is regenerated; otherwise the previous board's config is silently reused.

The first run pulls the image (~2 GB) and resolves managed components into
`managed_components/` (gitignored; `dependencies.lock` is tracked).

Pin mappings and board feature flags live in
`components/board/include/board/board.h` behind
`CONFIG_BOARD_REV1_DEVKIT` / `CONFIG_BOARD_REV2`.

## Flash and monitor (initial bring-up only)

Routine updates will be delivered via OTA (phase 5). For initial bring-up,
flash over USB serial:

```bash
# With a local ESP-IDF installation:
idf.py -p /dev/ttyUSB0 flash monitor      # Linux
idf.py -p /dev/cu.usbserial-0001 flash monitor  # macOS

# Exit monitor with Ctrl-]
```

Docker on macOS cannot pass through USB serial devices; for bring-up either
use a Linux host (`docker run --device /dev/ttyUSB0 ...`) or flash the
CI-built binaries with esptool:

```bash
esptool.py --chip esp32 -p <PORT> write_flash \
  0x1000  build/bootloader/bootloader.bin \
  0x8000  build/partition_table/partition-table.bin \
  0x10000 build/wateringsystem.bin
```

## Partition layout (4MB)

| Name     | Type | SubType  | Offset   | Size     | Purpose                  |
|----------|------|----------|----------|----------|--------------------------|
| nvs      | data | nvs      | 0x9000   | 0x4000   | Configuration storage    |
| otadata  | data | ota      | 0xd000   | 0x2000   | OTA slot selection       |
| phy_init | data | phy      | 0xf000   | 0x1000   | RF calibration data      |
| ota_0    | app  | ota_0    | 0x10000  | 0x180000 | Application slot A       |
| ota_1    | app  | ota_1    | 0x190000 | 0x180000 | Application slot B       |
| storage  | data | littlefs | 0x310000 | 0xF0000  | Web assets, data logging |

A/B app slots with `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` provide
automatic rollback after a failed OTA boot (OTA itself arrives in phase 5).

## CI

`.github/workflows/firmware-build.yml` builds both board variants with
ESP-IDF v6.0.1 and uploads the binaries as artifacts. The push trigger is
**main-only**; feature branches build via PR or manual `workflow_dispatch`.
