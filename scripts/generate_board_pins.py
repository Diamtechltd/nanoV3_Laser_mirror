from pathlib import Path
import re

from SCons.Script import DefaultEnvironment


ENV = DefaultEnvironment()
PROJECT_DIR = Path(ENV.subst("$PROJECT_DIR"))
BUILD_DIR = Path(ENV.subst("$BUILD_DIR"))
PIN_CONFIG_PATH = PROJECT_DIR / "pins.yaml"
GENERATED_HEADER_PATH = BUILD_DIR / "GeneratedBoardPins.h"

ASSIGNMENT_TO_SYMBOL = {
    "endstop_pin": "kEndstopPin",
}

AXIS_KEY_PATTERN = re.compile(
    r"^axis(\d+)_(step_pin|dir_pin|uart_pin|driver_address)$"
)
AXIS_ROLE_SUFFIX_TO_SYMBOL = {
    "step_pin": "kAxisStepPins",
    "dir_pin": "kAxisDirPins",
    "uart_pin": "kAxisTmcUartPins",
    "driver_address": "kAxisTmcDriverAddresses",
}


def parse_scalar(value):
    value = value.strip()
    if value.startswith('"') and value.endswith('"'):
        return value[1:-1]
    if value.startswith("'") and value.endswith("'"):
        return value[1:-1]
    if value.lower() == "true":
        return True
    if value.lower() == "false":
        return False
    if value.startswith("[") and value.endswith("]"):
        inner = value[1:-1].strip()
        if not inner:
            return []
        return [parse_scalar(part.strip()) for part in inner.split(",")]
    try:
        return int(value)
    except ValueError:
        return value


def parse_simple_yaml(path):
    root = {}
    stack = [(-1, root)]

    for lineno, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw_line.split("#", 1)[0].rstrip()
        if not line.strip():
            continue

        indent = len(line) - len(line.lstrip(" "))
        if indent % 2 != 0:
            raise ValueError(f"{path.name}:{lineno}: indentation must use multiples of two spaces")

        content = line.strip()
        if ":" not in content:
            raise ValueError(f"{path.name}:{lineno}: expected key: value entry")

        key, value = content.split(":", 1)
        key = key.strip()
        value = value.strip()

        while stack and indent <= stack[-1][0]:
            stack.pop()
        if not stack:
            raise ValueError(f"{path.name}:{lineno}: invalid indentation")

        parent = stack[-1][1]
        if not isinstance(parent, dict):
            raise ValueError(f"{path.name}:{lineno}: parent container must be a mapping")

        if value == "":
            new_dict = {}
            parent[key] = new_dict
            stack.append((indent, new_dict))
        else:
            parent[key] = parse_scalar(value)

    return root


def fail(message):
    print(f"ERROR: {message}")
    raise SystemExit(1)


def load_pin_config():
    if not PIN_CONFIG_PATH.exists():
        fail(f"Missing pin configuration file: {PIN_CONFIG_PATH}")

    try:
        data = parse_simple_yaml(PIN_CONFIG_PATH)
    except ValueError as exc:
        fail(str(exc))

    if not isinstance(data, dict):
        fail("pins.yaml must define a top-level mapping")

    boards = data.get("boards")
    active_board = data.get("active_board")

    if boards is None and active_board is None:
        return data

    if not isinstance(boards, dict):
        fail("pins.yaml boards must be a mapping when active_board is used")
    if not isinstance(active_board, str) or not active_board:
        fail("pins.yaml active_board must be a non-empty board key")
    if active_board not in boards:
        fail(f"pins.yaml active_board references unknown board profile {active_board}")

    selected = boards[active_board]
    if not isinstance(selected, dict):
        fail(f"pins.yaml boards.{active_board} must be a mapping")
    return selected


def normalize_board_pins(data):
    board_pins = data.get("board_pins")
    if not isinstance(board_pins, dict):
        fail("pins.yaml must contain a board_pins mapping")

    normalized = {}
    for label, meta in board_pins.items():
        if not isinstance(meta, dict):
            fail(f"board_pins.{label} must be a mapping")

        arduino_number = meta.get("arduino_number")
        reserved = meta.get("reserved", False)
        aliases = meta.get("aliases", [])
        assignable_roles = meta.get("assignable_roles", [])

        if not isinstance(arduino_number, int):
            fail(f"board_pins.{label}.arduino_number must be an integer")
        if not isinstance(reserved, bool):
            fail(f"board_pins.{label}.reserved must be true or false")
        if not isinstance(aliases, list):
            fail(f"board_pins.{label}.aliases must be a list when present")
        if not isinstance(assignable_roles, list):
            fail(f"board_pins.{label}.assignable_roles must be a list when present")

        normalized[label] = {
            "arduino_number": arduino_number,
            "reserved": reserved,
            "aliases": aliases,
            "assignable_roles": assignable_roles,
        }

    return normalized


def resolve_assignments(data, board_pins):
    assignments = data.get("assignments")
    if not isinstance(assignments, dict):
        fail("pins.yaml must contain an assignments mapping")

    resolved = {}
    used_labels = {}

    for assignment_name, symbol_name in ASSIGNMENT_TO_SYMBOL.items():
        if assignment_name not in assignments:
            fail(f"Missing assignments.{assignment_name} in pins.yaml")

        label = assignments[assignment_name]
        if not isinstance(label, str):
            fail(f"assignments.{assignment_name} must be a pin label like D4 or A0")
        if label not in board_pins:
            fail(f"assignments.{assignment_name} references unknown pin label {label}")
        if board_pins[label]["reserved"] and assignment_name not in board_pins[label]["assignable_roles"]:
            fail(f"assignments.{assignment_name} uses reserved pin {label}")
        if label in used_labels:
            fail(
                f"assignments.{assignment_name} duplicates {label}, already used by "
                f"{used_labels[label]}"
            )

        used_labels[label] = assignment_name
        resolved[symbol_name] = {
            "label": label,
            "arduino_number": board_pins[label]["arduino_number"],
        }

    axis_role_map = {}
    for key, value in assignments.items():
        match = AXIS_KEY_PATTERN.match(str(key))
        if not match:
            continue

        axis_index = int(match.group(1))
        role_suffix = match.group(2)
        axis_role_map.setdefault(axis_index, {})[role_suffix] = value

    if not axis_role_map:
        fail(
            "pins.yaml assignments must define per-axis pins using "
            "axis0_step_pin/axis0_dir_pin/axis0_uart_pin/axis0_driver_address"
        )

    axis_indices = sorted(axis_role_map.keys())
    for expected, actual in enumerate(axis_indices):
        if expected != actual:
            fail("Axis assignments must be contiguous starting at axis0")

    axis_assignments = []
    for axis_index in axis_indices:
        role_values = axis_role_map[axis_index]
        for required_role in ("step_pin", "dir_pin", "uart_pin", "driver_address"):
            if required_role not in role_values:
                fail(
                    f"Missing assignments.axis{axis_index}_{required_role} in pins.yaml"
                )

        axis_meta = {}
        for role_suffix in ("step_pin", "dir_pin", "uart_pin"):
            label = role_values[role_suffix]
            if not isinstance(label, str):
                fail(
                    f"assignments.axis{axis_index}_{role_suffix} must be a pin label"
                )
            if label not in board_pins:
                fail(
                    f"assignments.axis{axis_index}_{role_suffix} references unknown pin {label}"
                )
            if board_pins[label]["reserved"]:
                fail(f"assignments.axis{axis_index}_{role_suffix} uses reserved pin {label}")
            if label in used_labels:
                fail(
                    f"assignments.axis{axis_index}_{role_suffix} duplicates {label}, already used by "
                    f"{used_labels[label]}"
                )

            used_labels[label] = f"axis{axis_index}_{role_suffix}"
            axis_meta[role_suffix] = {
                "label": label,
                "arduino_number": board_pins[label]["arduino_number"],
            }

        driver_address = role_values["driver_address"]
        if not isinstance(driver_address, int):
            fail(
                f"assignments.axis{axis_index}_driver_address must be an integer"
            )
        if driver_address < 0 or driver_address > 3:
            fail(
                f"assignments.axis{axis_index}_driver_address must be between 0 and 3"
            )
        axis_meta["driver_address"] = driver_address

        axis_assignments.append(axis_meta)

    resolved["kAxisCount"] = len(axis_assignments)
    resolved["kAxisStepPins"] = [
        axis["step_pin"]["arduino_number"] for axis in axis_assignments
    ]
    resolved["kAxisDirPins"] = [
        axis["dir_pin"]["arduino_number"] for axis in axis_assignments
    ]
    resolved["kAxisTmcUartPins"] = [
        axis["uart_pin"]["arduino_number"] for axis in axis_assignments
    ]
    resolved["kAxisTmcDriverAddresses"] = [
        axis["driver_address"] for axis in axis_assignments
    ]

    return resolved


def write_header(resolved_assignments):
    BUILD_DIR.mkdir(parents=True, exist_ok=True)

    lines = [
        "#pragma once",
        "",
        "#include <Arduino.h>",
        "",
        "namespace generated_board_pins {",
    ]

    axis_count = resolved_assignments["kAxisCount"]
    lines.append(f"constexpr uint8_t kAxisCount = {axis_count};")

    def format_array(symbol_name):
        values = ", ".join(str(item) for item in resolved_assignments[symbol_name])
        lines.append(
            f"constexpr uint8_t {symbol_name}[kAxisCount] = {{{values}}};"
        )

    format_array("kAxisStepPins")
    format_array("kAxisDirPins")
    format_array("kAxisTmcUartPins")
    format_array("kAxisTmcDriverAddresses")

    for scalar_symbol in ("kEndstopPin",):
        meta = resolved_assignments[scalar_symbol]
        lines.append(
            f"constexpr uint8_t {scalar_symbol} = {meta['arduino_number']};"
            f"  // {meta['label']}"
        )

    lines.extend(["}", ""])
    GENERATED_HEADER_PATH.write_text("\n".join(lines), encoding="utf-8")


pin_config = load_pin_config()
board_pin_map = normalize_board_pins(pin_config)
resolved_assignment_map = resolve_assignments(pin_config, board_pin_map)
write_header(resolved_assignment_map)

ENV.Append(CPPPATH=[str(BUILD_DIR)])
