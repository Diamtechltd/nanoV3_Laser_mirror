# AGENTS.md

## Project goal
- Maintain a PlatformIO firmware project for an Arduino Nano SuperMini aperture driver.
- Keep STEP/DIR motion as the stable baseline while supporting optional TMC2209 UART, active-low endstop protection, homing, and build-time YAML configuration.

## Expected layout
- `platformio.ini` contains board, dependency, and pre-build script configuration.
- `pins.yaml` is the editable source of truth for board pin inventory and active pin assignments.
- `conf.yaml` is the editable source of truth for tunable build-time defaults and feature toggles.
- `conf.yaml` also contains the `stepper_motor` motor profile, measured steps/mm calibration, reference stroke calibration, position limits, speed limits, and per-microstep timing defaults.
- `conf.yaml` also carries double-tap homing verification settings under `homing`.
- `conf.yaml` also carries linear aperture-iris calibration under `aperture_iris`.
- `connection_diagram.txt` is the human-readable wiring reference and must match `pins.yaml` and endstop wiring semantics.
- `scripts/generate_board_pins.py` validates `pins.yaml` and generates `GeneratedBoardPins.h` into the build directory.
- `scripts/generate_build_config.py` validates `conf.yaml` and generates `GeneratedBuildConfig.h` into the build directory.
- `include/BoardConfig.h` consumes generated pin values and keeps only non-pin board settings.
- `include/DriverConfig.h` wraps generated tunable defaults plus fixed non-user hardware constants.
- `src/main.cpp` owns the user-facing serial command loop, motion state, endstop logic, and homing flow.
- `src/main.cpp` also owns mm position tracking, raw absolute travel moves, and aperture-opening command handling.
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
- `E` toggles endstop protection for normal manual motion; it does not disable homing.
- Normal step delay comes from the `stepper_motor.microsteps_delay` table for the active microstep setting.
- Homing step delay is derived automatically as `2x` the normal delay for the active microstep setting.
- Position becomes known after homing or after backing into the minimum endstop.
- Absolute `g <mm>` moves use fixed-point `0.001 mm` tracking and obey configured min/max limits.
- `stepper_motor.steps_per_mm` is the source of truth for raw travel conversion; `full_stroke_mm` and `full_stroke_steps_1x` are diagnostic cross-check values.
- `A <mm>` maps user-facing aperture opening mm onto raw travel using the configured linear iris range.
- `m <steps>` is always a literal signed step command and is not scaled by the default-distance baseline.
- Status output includes `speed limit us` and `est max mm/s` to explain the active timing cap.
- UART support is optional and controlled through build-time config.

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
