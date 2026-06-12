# HIL Test Checklist: Pump Actuator Layer (CP3 — bench rig)

**Feature**: 002-pump-gpio-board | **Runner**: Paul | **Board**: rev1 devkit rig

**Setup**: Flash the rev1 build, scope/meter on GPIO 26 (plant) and GPIO 27
(reservoir), serial terminal at 115200 baud on UART0. Commands and exact
response formats per [contracts/serial-diagnostic.md](../contracts/serial-diagnostic.md).

## Flashing from the CI artifact (macOS, no ESP-IDF needed)

> ⚠️ Flash the **bench-rig devkit only** — never the greenhouse unit (it keeps
> running the Arduino firmware untouched).

1. **Get the binaries** — from the PR's green `firmware-build` run, download the
   artifact named **`wateringsystem-rev1_devkit`** (Actions → run → Artifacts),
   or from a terminal:
   ```bash
   gh run download --repo cryptotomte/WateringSystem -n wateringsystem-rev1_devkit -D ~/Downloads/ws-rev1
   ```
   The board name in the artifact IS the proof you took the right build — and
   step 4's boot banner double-checks it.
2. **Install esptool once** (small standalone flasher, no ESP-IDF):
   ```bash
   brew install esptool
   ```
3. **Connect the devkit (USB) and flash** — find the port with
   `ls /dev/cu.usbserial* /dev/cu.SLAB*`, then (adjust paths/port):
   ```bash
   cd ~/Downloads/ws-rev1
   esptool --chip esp32 --port /dev/cu.usbserial-0001 erase-flash   # wipes whatever ran before
   esptool --chip esp32 --port /dev/cu.usbserial-0001 write-flash \
     0x1000  bootloader/bootloader.bin \
     0x8000  partition_table/partition-table.bin \
     0xd000  ota_data_initial.bin \
     0x10000 wateringsystem.bin
   ```
   (Older esptool versions spell the commands `erase_flash`/`write_flash`.)
4. **Open the serial monitor and verify** — `screen /dev/cu.usbserial-0001 115200`
   (exit: `Ctrl-A` then `K`, confirm `y`). Press the EN/reset button. The boot
   banner must show **`Board: rev1_devkit`** and version `3.0.0-dev` — that is
   the "am I running the right firmware?" check. Then the `ws>` prompt appears;
   `help` lists the commands.

## Checklist

- [ ] **HIL-1: Boot fail-safe** — Power-cycle the rig and watch GPIO 26/27 from
  power-on. Expected: neither pump output goes high at any point before a
  command is issued; boot log shows `Pumps forced OFF (fail-safe boot state)`
  and the `ws>` prompt appears.

- [ ] **HIL-2: Timed run with self-stop** — `pump plant start 10`. Expected:
  response `OK plant running for 10 s`; GPIO 26 goes high immediately, then
  returns low after 10 s (±1 s) with no further command;
  `pump plant status` afterwards shows `last stop=duration_elapsed`.

- [ ] **HIL-3: Manual stop** — `pump plant start 10`, then within a few seconds
  `pump plant stop`. Expected: GPIO 26 drops low immediately on the stop
  command; response `OK plant stopped (reason=commanded, ran <X> s)` with a
  plausible elapsed time.

- [ ] **HIL-4: Max-runtime forced stop** — `pump reservoir start 300`. Expected:
  GPIO 27 high for 300 s, then forced low; an error/warning is logged on the
  console at the 300 s mark; `pump reservoir status` shows
  `last stop=max_runtime_forced`.

- [ ] **HIL-5: Rejected durations** — `pump plant start 0` and
  `pump plant start 301`. Expected: both rejected with
  `ERR duration must be 1..300 s`; GPIO 26 never moves; `pump plant status`
  unchanged by the rejected commands.

- [ ] **HIL-6: Hard reset mid-run** — `pump plant start 60`, then press the
  EN/reset button mid-run. Expected: GPIO 26 is low immediately from reboot
  and STAYS low (the interrupted run does not resume); boot log again shows
  the fail-safe message and the `ws>` prompt returns.

## Sign-off

- [ ] All six items pass — record date, firmware version (`version.txt` /
  boot banner) and any observations below.

| Item | Pass/Fail | Notes |
|---|---|---|
| HIL-1 | | |
| HIL-2 | | |
| HIL-3 | | |
| HIL-4 | | |
| HIL-5 | | |
| HIL-6 | | |
