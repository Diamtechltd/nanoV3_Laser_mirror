# Nano SuperMini Aperture Driver

PlatformIO firmware for an Arduino Nano SuperMini based aperture driver using STEP/DIR motion with optional TMC2209 UART support.

The current firmware includes:

- build-time configuration from `pins.yaml` and `conf.yaml`
- active-low minimum endstop support on `D7`
- directional endstop gating so forward escape motion remains allowed at minimum
- a serial-command motion workflow with double-tap homing verification and absolute mm moves

## Repository layout

- `platformio.ini` defines the PlatformIO environment, dependencies, and pre-build scripts.
- `pins.yaml` is the editable source of truth for board pin inventory and active assignments.
- `conf.yaml` is the editable source of truth for motion defaults, homing defaults, TMC UART toggles, and Arduino-side behavior flags.
- `conf.yaml` also contains stroke calibration, position limits, aperture-iris calibration, speed limits, and per-microstep timing defaults.
- `connection_diagram.txt` mirrors the active wiring in a quick human-readable format.
- `scripts/generate_board_pins.py` validates `pins.yaml` and generates `GeneratedBoardPins.h` during build.
- `scripts/generate_build_config.py` validates `conf.yaml` and generates `GeneratedBuildConfig.h` during build.
- `include/BoardConfig.h` exposes board-level settings and reads generated pin constants.
- `include/DriverConfig.h` wraps generated build-time defaults plus fixed hardware constants.
- `src/main.cpp` provides the serial command interface, motion logic, endstop protection, and homing flow.
- `src/Tmc2209Driver.*` contains UART-specific TMC2209 setup and status access.

## Build and run

Prerequisite: PlatformIO must be installed locally.

```powershell
pio run
pio run --target upload
pio device monitor
```

Windows helper scripts are also included:

```powershell
.\compile.cmd
.\upload.cmd
```

`upload.cmd` currently targets `COM3`; adjust that if your board enumerates on a different port.

## Configuration

This project uses two build-time YAML files in the repo root.

### `pins.yaml`

Use `pins.yaml` for board pin inventory, reserved pins, and active assignments. Rebuild after any pin change so the generated pin header is refreshed.

Current active assignments:

- `D4` -> TMC2209 `EN`
- `D5` -> TMC2209 `STEP`
- `D6` -> TMC2209 `DIR`
- `D7` -> minimum endstop input, active-low, reserved for `endstop_pin`
- `D8` -> `1k` resistor -> TMC2209 `PDN_UART`

### `conf.yaml`

Use `conf.yaml` for tunable build-time defaults and feature toggles. Rebuild after any config change so the generated build config header is refreshed.

Current shape:

```yaml
tmc2209:
  uart_enabled: true
  uart_baud: 57600

arduino:
  debug_mode: false

motion:
  endstop_enabled: true
  minimum_position: 0
  maximum_position: 30
  maximum_speed: 50
  default_current_ma: 140
  default_microsteps: 4
  default_move_steps: 1000  # baseline distance at 1x microstepping
  auto_disable_after_move: true

aperture_iris:
  min_mm: 1
  max_mm: 17

homing:
  retract_steps: 0
  direction_negative: true
  double_tap_distance_mm: 2
  second_seek_delay_multiplier: 2

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

Section meanings:

- `motion` controls boot defaults for endstop protection, current, microsteps, default move distance baseline, soft travel range in mm, max speed in mm/s, and auto-disable.
- `aperture_iris` defines the linear user-facing iris opening range mapped onto carriage travel.
- `homing` controls retract behavior plus the double-tap homing verification pass.
- `stepper_motor` describes the motor, measured steps/mm calibration, reference stroke calibration, and the per-microstep delay table used for normal motion timing.
- `tmc2209` controls whether UART support is enabled and which baud rate is used for `PDN_UART`.
- `arduino` controls firmware-local behavior flags such as boot-time debug verbosity.

Timing behavior:

- normal motion delay is auto-selected from `stepper_motor.microsteps_delay` using the active microstep setting
- normal auto-selected delay is also clamped by `motion.maximum_speed` using the configured 30 mm stroke calibration
- homing delay is always derived as `2x` the normal delay for the active microstep setting
- the second homing touch runs slower using `homing.second_seek_delay_multiplier`
- `u <microsteps>` reapplies table-based timing automatically
- `v <delay_us>` is a temporary runtime override until the next successful `u` command

Position behavior:

- `stepper_motor.steps_per_mm` is the source of truth for raw travel conversion
- `stepper_motor.full_stroke_mm` and `full_stroke_steps_1x` are kept as a derived cross-check reference
- absolute position is tracked in `0.001 mm` units after homing or after backing into the minimum endstop
- the minimum endstop is the absolute origin at `0.000 mm`
- `g <mm>` absolute moves are allowed only when the position is known
- `A <mm>` maps aperture opening mm onto raw carriage travel using the configured linear iris range

## Endstop and homing behavior

- The minimum endstop is wired as `D7 -> switch -> GND` with the Arduino internal pull-up enabled.
- Active-low means switch open reads `HIGH`, and switch pressed reads `LOW`.
- The minimum endstop blocks backward motion into the stop, not forward escape motion away from it.
- Forward moves remain allowed while the minimum endstop is active so the mechanism can recover.
- Homing seeks backward toward minimum, stops on trigger, then retracts forward away from the switch.
- `H` now performs a double-tap cycle: first touch to zero, move forward `2.000 mm`, slower second touch, then optional retract.
- After homing, firmware prints the expected and actual second-touch distance in both steps and mm.

## Serial commands

The firmware uses hardware `Serial` at `115200` for the USB terminal.

- `h` or `?` prints help
- `s` prints status
- `e` enables the driver
- `d` disables the driver
- `f` moves forward using the default distance
- `b` moves backward using the default distance
- the no-argument `f` / `b` distance is computed as `default_move_steps * currentMicrosteps`
- `f 2000` moves forward a specific step count
- `b 2000` moves backward a specific step count
- `m <steps>` moves a signed step count
- `m 1000` means exactly `1000` steps forward at the current microstep mode
- `m -1000` means exactly `1000` steps backward at the current microstep mode
- `g <mm>` moves to an absolute position in millimeters from the minimum endstop origin
- `A <mm>` moves to an absolute aperture opening in millimeters
- `i <mA>` sets run current
- `u <microsteps>` sets microsteps
- `v <delay_us>` sets step delay temporarily
- `a` toggles auto-disable after each move
- `H` homes toward the minimum endstop
- `H <steps>` homes, verifies, and uses a one-shot retract override
- `E` toggles endstop protection on or off for normal manual motion
- `D` toggles runtime debug verbosity for move/homing chatter and TMC pulse diagnostics
- status output now includes `speed limit us` and `est max mm/s` for timing visibility
- `D` is session-only for now; boot default still comes from `arduino.debug_mode`

Iris behavior:

- `g <mm>` stays raw carriage travel
- `A <mm>` is the user-facing aperture-opening command
- with the current config, `A 1.000` maps to travel `0.000 mm` and `A 17.000` maps to travel `30.000 mm`
- after a successful `A` move, firmware prints the requested and resulting aperture opening size

## Pin and config workflow

When project wiring or boot defaults change:

1. Update `pins.yaml` for pin assignments and reserved-pin metadata.
2. Update `conf.yaml` for build-time defaults and feature toggles.
3. Update `connection_diagram.txt` when wiring or endstop semantics change.
4. Rebuild so `GeneratedBoardPins.h` and `GeneratedBuildConfig.h` are regenerated under `.pio/build/...`.

Do not edit generated headers directly.

## Hardware bring-up notes

- Confirm the Nano variant and whether it needs the old or new bootloader upload settings.
- Verify the actual Nano SuperMini header mapping before finalizing `pins.yaml`.
- Confirm the minimum endstop wiring and polarity before relying on homing.
- Confirm the TMC2209 `PDN_UART` wiring before relying on UART mode.
- Keep STEP/DIR bring-up working even if UART is temporarily disabled in `conf.yaml`.
