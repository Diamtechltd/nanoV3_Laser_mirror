# Nano SuperMini Aperture Driver

PlatformIO firmware for an Arduino Nano SuperMini aperture driver using STEP/DIR motion with TMC2209 UART-verified motion safety.

## What this repo covers

- firmware build, upload, and serial CLI workflow
- build-time config from `pins.yaml` and `conf.yaml`
- runtime EEPROM-backed config staging and save/reload behavior
- motion, homing, aperture mapping, and UART safety behavior

Physical assembly, BOM, wiring, and sourcing live under [Assembly_Instructions](Assembly_Instructions/README.md).

## Repository layout

- `platformio.ini` defines the PlatformIO environment, board target, dependencies, and pre-build scripts.
- `pins.yaml` is the editable source of truth for the board pin inventory and active firmware assignments.
- `conf.yaml` is the editable source of truth for tunable build defaults and feature toggles.
- `scripts/generate_board_pins.py` validates `pins.yaml` and generates `GeneratedBoardPins.h`.
- `scripts/generate_build_config.py` validates `conf.yaml` and generates `GeneratedBuildConfig.h`.
- `include/BoardConfig.h` exposes board-level settings and generated pin constants.
- `include/DriverConfig.h` wraps generated build defaults plus fixed hardware constants.
- `include/PersistentConfig.h` defines the typed onboard-EEPROM runtime config API.
- `include/UserCommands.h` exposes the serial CLI entry points.
- `src/main.cpp` owns motion, homing, endstop policy, reboot behavior, and runtime state.
- `src/user_commands.cpp` owns the serial CLI, prompts, help, and mode-aware command dispatch.
- `src/PersistentConfig.*` owns EEPROM validation, CRC checks, and save/load helpers.
- `src/Tmc2209Driver.*` owns UART-specific TMC2209 setup and status helpers.
- `Assembly_Instructions/` is the canonical home for physical build notes, wiring, BOM, and sourcing links.

## Physical build docs

Use these files for anything mechanical or electrical:

- [Assembly_Instructions/README.md](Assembly_Instructions/README.md): hardware checklist and bring-up notes
- [Assembly_Instructions/BOM.txt](Assembly_Instructions/BOM.txt): parts list
- [Assembly_Instructions/connection_diagram.txt](Assembly_Instructions/connection_diagram.txt): wiring reference
- [Assembly_Instructions/URLS.txt](Assembly_Instructions/URLS.txt): source and reference links

## Build and run

Prerequisite: PlatformIO installed locally.

```powershell
pio run
pio run --target upload
pio device monitor
```

Windows helper scripts:

```powershell
.\compile.cmd
.\upload.cmd
```

Current defaults:

- the repo targets `board = nanoatmega328new`
- serial monitor uses `115200`
- `upload.cmd` currently uploads to `COM3`

If your hardware needs a different COM port or bootloader configuration, update the local upload settings accordingly.

## Configuration

This project uses two build-time YAML files in the repo root.

### `pins.yaml`

Use `pins.yaml` for firmware-visible pin inventory, reserved pins, and active assignments. Rebuild after any pin change so the generated board header is refreshed.

Current active firmware assignments:

- `D4` -> TMC2209 `EN`
- `D5` -> TMC2209 `STEP`
- `D6` -> TMC2209 `DIR`
- `D7` -> minimum endstop input
- `D8` -> `PDN_UART`

For exact physical wiring, use [Assembly_Instructions/connection_diagram.txt](Assembly_Instructions/connection_diagram.txt).

### `conf.yaml`

Use `conf.yaml` for tunable firmware defaults and feature toggles. Rebuild after any config change so the generated build config header is refreshed.

Current shape:

```yaml
aperture_iris:
  min_mm: 1.5
  max_mm: 17

motion:
  endstop_enabled: true
  minimum_position: 0
  maximum_position: 22.23
  maximum_speed: 50
  default_current_ma: 140
  default_microsteps: 4
  default_move_steps: 1000
  auto_disable_after_move: true

homing:
  retract_steps: 0
  direction_negative: true
  double_tap_distance_mm: 2
  second_seek_delay_multiplier: 2

tmc2209:
  uart_enabled: true
  uart_baud: 57600

arduino:
  name: "Aperture Driver #1"
  debug_mode: false
  save_config_to_eeprom: true

stepper_motor:
  steps_per_mm: 193.333
  full_stroke_mm: 30
  full_stroke_steps_1x: 5800
  microsteps_delay:
    1: 275
    2: 140
    4: 70
    8: 25
    16: 12
    32: 6
    64: 5
    128: 5
    256: 5
```

Key meanings:

- `motion` controls soft range, move defaults, current, microsteps, speed limit, and auto-disable.
- `aperture_iris` defines the user-facing aperture-opening range mapped onto carriage travel.
- `homing` controls retract behavior and the double-tap verification pass.
- `tmc2209` controls UART enablement and baud rate.
- `arduino` controls board name, boot debug default, and EEPROM persistence enablement.
- `stepper_motor` defines the measured steps/mm and timing table used for motion.

## Runtime behavior

### Motion and position

- raw jogging uses `f <steps>` and `b <steps>`
- absolute carriage moves use `g <mm>`
- user-facing aperture moves use `aperture <mm>`
- homing uses `H` and performs a double-tap verification sequence
- position becomes known after homing or after backing into the minimum endstop
- the minimum endstop is the `0.000 mm` origin for position tracking

### UART safety

- motion is locked unless live TMC2209 UART communication is working
- if UART is disabled in config, the firmware still builds, but motion commands refuse to start

### Timing

- normal step delay comes from the `stepper_motor.microsteps_delay` table for the active microstep setting
- normal auto timing is clamped by `motion.maximum_speed`
- homing timing is derived automatically from the active microstep timing
- `v <delay_us>` applies a manual runtime override until the next successful `u`, `reload`, or `reset`

### EEPROM persistence

- runtime config is staged in RAM first
- `write` saves staged values to the MCU EEPROM
- `reload` discards unsaved changes and reloads EEPROM or defaults
- `reset` loads compile-time defaults into RAM without writing EEPROM
- boot falls back to compile-time defaults if EEPROM is empty, corrupt, or incompatible

Saved runtime fields include:

- endstop enable
- debug mode
- run current
- microsteps
- step delay override
- auto-disable
- device name
- iris min/max

## Serial CLI

The firmware uses hardware `Serial` at `115200`.

### Normal mode

Prompt: `> `

- `h`, `help`, `?`
- `status`
- `name`
- `config`, `con`
- `driver`, `driver on`, `driver off`
- `f [steps]`
- `b [steps]`
- `g <mm>`
- `aperture <mm>`
- `H [steps]`
- `reboot`

Notes:

- `driver off` aborts active motion before disabling the driver
- `reboot` uses an AVR watchdog reset and is not a literal external power cycle

### Config mode

Prompt: `config> `

- `h`, `help`, `?`
- `exit`, `q`
- `name [new name]`
- `debug`
- `endstop`
- `a`
- `i <mA>`
- `u <microsteps>`
- `v <delay_us>`
- `iris`
- `iris min <mm>`
- `iris max <mm>`
- `write`
- `reload`
- `reset`
- `read`
- `defaults`

Notes:

- `debug` and `v` take effect immediately and can be made persistent with `write`
- `name`, `iris`, `endstop`, `a`, `i`, `u`, and `v` all stage changes in RAM first

## Firmware workflow notes

When firmware behavior changes:

1. Update `pins.yaml` for pin assignment changes.
2. Update `conf.yaml` for build-default changes.
3. Update `Assembly_Instructions/` if the physical build or wiring also changes.
4. Rebuild so generated headers are refreshed under `.pio/build/...`.

Do not edit generated headers directly.

## Troubleshooting

- If `reboot` hangs the board, confirm the target bootloader matches the current `nanoatmega328new` configuration.
- If motion is locked, check TMC2209 UART wiring and `tmc2209.uart_enabled`.
- If homing or position behavior is wrong, check `conf.yaml` limits and the physical endstop wiring in `Assembly_Instructions/connection_diagram.txt`.
