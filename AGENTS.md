# AGENTS.md

## Project goal
- Bootstrap a PlatformIO project for the Arduino Nano SuperMini aperture driver.
- Keep the firmware flashable early, with STEP/DIR motion as the stable baseline and TMC2209 UART support isolated for later refinement.

## Expected layout
- `platformio.ini` contains board, serial monitor, and dependency configuration.
- `pins.yaml` is the editable source of truth for board pin inventory and active pin assignments.
- `connection_diagram.txt` is the human-readable wiring reference and must match `pins.yaml`.
- `include/BoardConfig.h` consumes generated pin values and keeps only non-pin board settings.
- `include/DriverConfig.h` owns driver defaults and feature toggles only.
- `src/main.cpp` owns the user-facing serial command loop and motion flow.
- `src/Tmc2209Driver.*` owns TMC2209 UART-specific setup, readback, and status helpers.

## Working rules
- Keep hardware `Serial` reserved for the USB terminal at `115200`.
- Update `pins.yaml` when pin assignments change; do not edit `BoardConfig.h` for pin reassignment.
- Update `connection_diagram.txt` in the same change whenever `pins.yaml` assignments change.
- Preserve reserved-pin policy in `pins.yaml` and keep the diagram synchronized with the YAML assignments.
- Do not spread TMC2209 register logic through the app; keep it behind the TMC wrapper so the driver can be swapped later.
- Preserve the command vocabulary unless a change is intentional and documented.

## PlatformIO workflow
- Build: `pio run`
- Upload: `pio run --target upload`
- Monitor: `pio device monitor`

## First-hardware checklist
- Fill in real Nano pin assignments in `pins.yaml`.
- Verify whether the board needs old or new bootloader upload settings.
- Confirm the TMC2209 UART wiring before enabling `kDriverUartEnabled`.
