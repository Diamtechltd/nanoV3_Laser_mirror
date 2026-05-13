# Nano SuperMini Aperture Driver

PlatformIO firmware scaffold for an Arduino Nano SuperMini based aperture driver using a TMC2209 stepper driver.

The project keeps STEP/DIR motion as the stable baseline and isolates TMC2209 UART behavior behind a small wrapper so the firmware stays flashable while hardware wiring is still being verified.

## Repository layout

- `platformio.ini` defines the PlatformIO environment, serial monitor settings, and library dependencies.
- `pins.yaml` is the editable source of truth for board pin inventory and active assignments.
- `connection_diagram.txt` mirrors the active wiring from `pins.yaml` in a quick human-readable format.
- `scripts/generate_board_pins.py` validates `pins.yaml` and generates `GeneratedBoardPins.h` during build.
- `include/BoardConfig.h` exposes board-level settings and reads generated pin constants.
- `include/DriverConfig.h` contains TMC2209 defaults and feature toggles.
- `src/main.cpp` provides the serial command interface and STEP/DIR motion flow.
- `src/Tmc2209Driver.*` contains UART-specific TMC2209 setup and status access.

## Build and run

Prerequisite: PlatformIO must be installed locally.

```powershell
pio run
pio run --target upload
pio device monitor
```

Helper scripts are also included for Windows:

```powershell
.\compile.cmd
.\upload.cmd
```

`upload.cmd` currently targets `COM3`; adjust that if your board enumerates on a different port.

## Serial command baseline

The firmware uses hardware `Serial` at `115200` for the USB terminal. Current command set:

- `h` or `?` prints help
- `s` prints status
- `e` enables the driver
- `d` disables the driver
- `f` / `b` move the default step count
- `f 2000` / `b 2000` move a specific step count
- `m <steps>` moves a signed step count
- `i <mA>` sets run current
- `u <microsteps>` sets microsteps
- `v <delay_us>` sets step delay
- `a` toggles auto-disable after each move

## Pin assignment workflow

When pin assignments change:

1. Update `pins.yaml`.
2. Update `connection_diagram.txt` in the same change.
3. Rebuild so the generated pin header is refreshed under `.pio/build/...`.

Do not edit generated pin values in `BoardConfig.h`.

## First hardware bring-up checklist

- Confirm the Nano variant and whether it needs the old or new bootloader upload settings.
- Verify the actual Nano SuperMini header pin mapping before finalizing `pins.yaml`.
- Confirm the TMC2209 `PDN_UART` wiring before relying on UART mode.
- Keep STEP/DIR bring-up working even if UART is temporarily disabled in `include/DriverConfig.h`.
