# Assembly Instructions

This folder is the canonical home for everything physical about the device:

- [BOM.txt](BOM.txt): parts list
- [connection_diagram.txt](connection_diagram.txt): wiring reference
- [URLS.txt](URLS.txt): source and reference links

The root `README.md` stays firmware-focused. If the hardware build changes, update this folder.

## Print settings

Current print guidance:

- `0.2 mm` layer height
- `0.4 mm` nozzle
- random seam position

## Bootloader requirement

The current firmware targets the Arduino Nano new bootloader (`nanoatmega328new`).

Important:

- flash the board with the new bootloader before relying on the `reboot` command
- if the board still has an old bootloader, watchdog reboot behavior will hang the device.

## Wiring highlights

Use [connection_diagram.txt](connection_diagram.txt) for the exact wiring.

Key points:

- TMC2209 enable/step/dir use `D4`, `D5`, and `D6`
- TMC2209 UART uses `D7` through a `1k` resistor
- the minimum endstop wiring is:
  - `NC -> D2`
  - `COM -> 5V`
  - `D2 -> 10k -> GND`

## Bring-up checklist

1. Confirm the board is flashed for the new bootloader target used by this repo.
2. Print and assemble the mechanical parts using the print settings above.
3. Gather the electronics from [BOM.txt](BOM.txt).
4. Wire the board, TMC2209, motor, and endstop exactly as shown in [connection_diagram.txt](connection_diagram.txt).
5. Upload firmware and open the serial monitor at `115200`.
6. Verify the board boots, prints its name, and responds to `status`.
7. Verify TMC UART is detected before testing motion.
8. Test `driver on`, `H`, and then position/aperture moves only after wiring is confirmed.

## Notes

- The physical docs in this folder should stay aligned with `pins.yaml` and the current firmware assumptions.
- If you change wiring, update both this folder and the firmware-facing docs/config in the same change.
