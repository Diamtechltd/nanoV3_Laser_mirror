# AGENTS.md

## Project goal
- Maintain a PlatformIO firmware project for an Arduino Nano SuperMini aperture driver.
- Keep STEP/DIR pulse generation as the motion baseline while requiring live TMC2209 UART communication before any motion, alongside active-low endstop protection, homing, and build-time YAML configuration.

## Expected layout
- `platformio.ini` contains board, dependency, and pre-build script configuration.
- `pins.yaml` is the editable source of truth for board pin inventory and active pin assignments.
- `conf.yaml` is the editable source of truth for tunable build-time defaults and feature toggles.
- `conf.yaml` also contains the `stepper_motor` motor profile, measured steps/mm calibration, reference stroke calibration, position limits, speed limits, and per-microstep timing defaults.
- `conf.yaml` also carries double-tap homing verification settings under `homing`.
- `conf.yaml` also carries linear aperture-iris calibration under `aperture_iris`.
- `conf.yaml` also carries Arduino-local behavior flags under `arduino`, including boot debug verbosity.
- `connection_diagram.txt` is the human-readable wiring reference and must match `pins.yaml` and endstop wiring semantics.
- `scripts/generate_board_pins.py` validates `pins.yaml` and generates `GeneratedBoardPins.h` into the build directory.
- `scripts/generate_build_config.py` validates `conf.yaml` and generates `GeneratedBuildConfig.h` into the build directory.
- `include/BoardConfig.h` consumes generated pin values and keeps only non-pin board settings.
- `include/DriverConfig.h` wraps generated tunable defaults plus fixed non-user hardware constants.
- `include/PersistentConfig.h` defines the typed onboard-EEPROM runtime config API.
- `include/UserCommands.h` exposes the serial CLI entry points used by the firmware lifecycle.
- `src/main.cpp` owns motion state, endstop logic, homing flow, mm position tracking, raw absolute travel moves, and aperture-opening command handling.
- `src/user_commands.cpp` owns the user-facing serial command loop, prompts, help text, and mode-aware command dispatch.
- `src/PersistentConfig.*` owns onboard EEPROM record layout, validation, CRC checks, and byte-wise save/load helpers only.
- `src/Tmc2209Driver.*` owns TMC2209 UART-specific setup, readback, and status helpers only.

## Working rules
- Keep hardware `Serial` reserved for the USB terminal at `115200`.
- Update `pins.yaml` when pin assignments change; do not edit `BoardConfig.h` for pin reassignment.
- Update `conf.yaml` when changing build-time defaults such as endstop protection, current, microsteps, move size, motor timing tables, homing defaults, or UART enablement.
- Keep the minimum endstop as the absolute `0.00 mm` origin for any position-tracking logic.
- Update `connection_diagram.txt` in the same change whenever pin assignments or endstop wiring semantics change.
- Do not edit generated headers directly; rebuild instead.
- Preserve reserved-pin policy in `pins.yaml` and keep the diagram synchronized with the YAML assignments.
- Do not spread TMC2209 register logic through the app; keep it behind the TMC wrapper so the driver can be swapped later.
- Do not move endstop or motion policy into the TMC wrapper; keep it in `main.cpp`.
- Preserve the command vocabulary unless a change is intentional and documented.

## Current behavior
- `D7` is the minimum endstop input, wired active-low with `INPUT_PULLUP`.
- The minimum endstop blocks backward motion into the stop only.
- Forward recovery motion remains allowed while the endstop is active.
- Homing seeks backward toward the minimum endstop and retracts forward away from it.
- `H` performs double-tap homing: first touch zeroes, a configured forward clearance move happens, then a slower second touch verifies repeatability.
- `config> endstop` toggles endstop protection for normal manual motion; it does not disable homing.
- `config> debug` toggles runtime debug verbosity; it boots from `arduino.debug_mode` and can be persisted with `write`.
- `name` prints the active device name in Normal mode, and `config> name <new_name>` stages a rename in RAM until `write`.
- The serial CLI has `Normal` and `Config` modes with prompts `> ` and `config> `.
- `config` and `con` enter Config mode; `exit` and `q` return to Normal mode.
- `driver` toggles the driver enable state in Normal mode; `driver on` and `driver off` set it explicitly.
- `status` prints the current firmware status in Normal mode.
- `i`, `u`, `v`, `iris`, `write`, `reload`, `reset defaults`, `read`, and `defaults` are exposed through Config mode rather than Normal mode.
- `a` and `endstop` are also exposed through Config mode rather than Normal mode.
- Normal step delay comes from the `stepper_motor.microsteps_delay` table for the active microstep setting.
- `v` applies a manual step-delay override; `u` clears it back to auto timing, and `write` can persist the current override.
- Homing step delay is derived automatically as `2x` the normal delay for the active microstep setting.
- Position becomes known after homing or after backing into the minimum endstop.
- Absolute `g <mm>` moves use fixed-point `0.001 mm` tracking and obey configured min/max limits.
- `stepper_motor.steps_per_mm` is the source of truth for raw travel conversion; `full_stroke_mm` and `full_stroke_steps_1x` are diagnostic cross-check values.
- `aperture <mm>` maps user-facing aperture opening mm onto raw travel using the configured linear iris range.
- `config> iris min <mm>` and `config> iris max <mm>` stage aperture-iris bounds in RAM until `write`.
- Raw step jogging uses `f <steps>` and `b <steps>`; no dedicated signed-step command exists.
- `write`, `reload`, `reset defaults`, `read`, and `defaults` manage the typed onboard-EEPROM runtime config record.
- `reboot` triggers an AVR watchdog reset, behaving like an MCU hard reset without external reset-control wiring.
- `reboot` aborts any active move or homing cycle before resetting.
- `driver off` aborts active motion before disabling the driver.
- Status output includes `speed limit us` and `est max mm/s` to explain the active timing cap.
- Movement is locked unless live TMC2209 UART communication is available; disabling UART in config or losing communication blocks all stepping motion, including homing.

## PlatformIO workflow
- Build: `pio run`
- Upload: `pio run --target upload`
- Monitor: `pio device monitor`
- Windows helper build: `.\compile.cmd`
- Windows helper upload: `.\upload.cmd`
- `upload.cmd` currently targets `COM3`; adjust if the board enumerates on a different port.

## First-hardware checklist
- Verify the actual Nano SuperMini header mapping before finalizing `pins.yaml`.
- Confirm the endstop wiring matches `D7 -> switch -> GND` with internal pull-up behavior.
- Confirm the TMC2209 UART wiring before relying on UART mode.
- Verify whether the board needs old or new bootloader upload settings.
