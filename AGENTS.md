# AGENTS.md

## Project goal

- Maintain a PlatformIO firmware project for a multi-axis aperture controller.
- Keep STEP/DIR motion as the baseline while requiring live TMC2209 UART communication before motion, alongside endstop protection, homing, EEPROM-backed runtime config, and a serial CLI.

## Canonical sources

- `platformio.ini`: PlatformIO environment, board target, dependencies, and pre-build scripts.
- `pins.yaml`: firmware pin inventory and active firmware pin assignments.
- `conf.yaml`: build-time defaults and feature toggles.
- `include/BoardConfig.h`: generated pin consumers plus fixed board settings.
- `include/DriverConfig.h`: generated build defaults plus fixed hardware constants.
- `include/PersistentConfig.h`: typed EEPROM runtime config API.
- `include/UserCommands.h`: CLI entry points.
- `src/main.cpp`: motion state, endstop policy, homing, reboot behavior, and runtime state ownership.
- `src/user_commands.cpp`: CLI prompts, help, parsing, and mode-aware command dispatch.
- `src/PersistentConfig.*`: EEPROM layout, validation, CRC checks, and save/load helpers only.
- `src/Tmc2209Driver.*`: TMC2209 UART setup, readback, and status helpers only.

## Physical build docs

- `Assembly_Instructions/README.md` is the overview for all physical build notes.
- `Assembly_Instructions/BOM.txt` is the canonical parts list.
- `Assembly_Instructions/connection_diagram.txt` is the canonical wiring reference.
- `Assembly_Instructions/URLS.txt` is the canonical source/reference link list.

If a change affects anything physical, update the matching file under `Assembly_Instructions/` in the same change.

## Working rules

- Keep hardware `Serial` reserved for the USB terminal at `115200`.
- Do not edit generated headers directly; rebuild instead.
- Update `pins.yaml` when firmware pin ownership changes; do not reassign pins by editing generated headers.
- Update `conf.yaml` when build defaults change.
- Keep firmware-facing pin ownership in `pins.yaml` aligned with the physical wiring docs in `Assembly_Instructions/`.
- Do not spread TMC2209 register logic through the app; keep it behind the TMC wrapper.
- Do not move motion policy or endstop policy into the TMC wrapper; keep it in `main.cpp`.
- Preserve command vocabulary unless a change is intentional and documented in both `README.md` and this file.

## Current firmware behavior

- `pins.yaml` supports board profiles and currently includes `nano_supermini` and `esp32_devkitc_v4`.
- `active_board` in `pins.yaml` chooses the board pin profile.
- `motion.axis_count` in `conf.yaml` defines the configured axis count for CLI validation.
- Axis-indexed jog commands can target any configured STEP/DIR axis.
- The minimum endstop is the `0.00 mm` origin for position tracking.
- Backward motion into the minimum endstop is blocked; forward recovery motion remains allowed.
- `H` performs double-tap homing: first touch zeroes, a forward clearance move happens, then a slower second touch verifies repeatability.
- Position becomes known after homing or after backing into the minimum endstop.
- Absolute carriage moves use `g[axis] <mm>` and are currently supported on axis `0` only.
- User-facing aperture moves use `aperture[axis] <mm>` or `A[axis] <mm>` and are currently supported on axis `0` only.
- Raw jogging uses `f[axis] <steps>` and `b[axis] <steps>`.
- Motion is locked unless live TMC2209 UART communication is available.
- `stepper_motor.steps_per_mm` and `stepper_motor.gear_ratio` are the source of truth for travel conversion.
- Normal step delay comes from the active microstep timing table unless `v` overrides it.
- `u` clears any manual delay override back to automatic timing.
- Runtime config is staged in RAM and persisted only by `write`.
- `reload` discards unsaved staged changes.
- `reset` loads compile-time defaults into RAM without writing EEPROM.
- `reboot` uses an AVR watchdog reset.
- CLI prompts include the device name prefix (for monitor timestamp readability).
- Axis selection is stateful via `config <axis>`, `config<axis>`, `config[axis]`, or `motor <axis>`.
- Axis-less move/driver commands target the selected axis.

## Current CLI shape

- The serial CLI has `Normal` and `Config` modes with prompts `<device-name> > ` and `<device-name> config<axis> > `.
- `config` and `con` enter Config mode.
- `exit` and `q` return to Normal mode.
- `config <axis>`, `config<axis>`, `config[axis]`, and `motor <axis>` update the selected axis in both modes.
- Normal-mode user commands include `status`, `name`, `driver`, `enable`, `disable`, `f[axis]`, `b[axis]`, `g[axis]`, `aperture[axis]`, `A[axis]`, `H[axis]`, and `reboot`.
- Config-mode user commands include `driver`, `enable`, `disable`, `debug`, `endstop`, `a`, `i`, `u`, `v`, `iris`, `name`, `write`, `reload`, `reset`, `read`, and `defaults`.

## PlatformIO workflow

- Build: `pio run`
- Upload: `pio run --target upload`
- Monitor: `pio device monitor`
- Windows helper build: `.\compile.cmd`
- Windows helper upload: `.\upload.cmd`

Current repo defaults:

- `board = nanoatmega328new`
- monitor `COM3`
- upload helper targets `COM3`

Only change the bootloader target if the actual hardware requires it.
