from pathlib import Path

from SCons.Script import DefaultEnvironment


ENV = DefaultEnvironment()
PROJECT_DIR = Path(ENV.subst("$PROJECT_DIR"))
BUILD_DIR = Path(ENV.subst("$BUILD_DIR"))
CONFIG_PATH = PROJECT_DIR / "conf.yaml"
GENERATED_HEADER_PATH = BUILD_DIR / "GeneratedBuildConfig.h"
SUPPORTED_MICROSTEPS = [1, 2, 4, 8, 16, 32, 64, 128, 256]

CONFIG_SCHEMA = {
    "motion": {
        "endstop_enabled": {
            "symbol": "kEndstopEnabled",
            "type": bool,
        },
        "minimum_position": {
            "symbol": "kMinimumPositionMm",
            "type": int,
        },
        "maximum_position": {
            "symbol": "kMaximumPositionMm",
            "type": int,
        },
        "maximum_speed": {
            "symbol": "kMaximumSpeedMmPerSec",
            "type": int,
            "min": 1,
        },
        "default_current_ma": {
            "symbol": "kDefaultCurrentMa",
            "type": int,
            "min": 1,
            "max": 500,
        },
        "default_microsteps": {
            "symbol": "kDefaultMicrosteps",
            "type": int,
            "allowed": SUPPORTED_MICROSTEPS,
        },
        "default_move_steps": {
            "symbol": "kDefaultMoveSteps",
            "type": int,
            "min": 1,
        },
        "auto_disable_after_move": {
            "symbol": "kAutoDisableAfterMove",
            "type": bool,
        },
    },
    "homing": {
        "retract_steps": {
            "symbol": "kHomingRetractSteps",
            "type": int,
            "min": 0,
        },
        "direction_negative": {
            "symbol": "kHomingDirectionNegative",
            "type": bool,
        },
        "double_tap_distance_mm": {
            "symbol": "kHomingDoubleTapDistanceMm",
            "type": int,
            "min": 1,
        },
        "second_seek_delay_multiplier": {
            "symbol": "kHomingSecondSeekDelayMultiplier",
            "type": int,
            "min": 1,
        },
    },
    "tmc2209": {
        "uart_enabled": {
            "symbol": "kDriverUartEnabled",
            "type": bool,
        },
        "uart_baud": {
            "symbol": "kDriverUartBaud",
            "type": int,
            "min": 1,
        },
    },
    "stepper_motor": {
        "steps_per_revolution": {
            "symbol": "kStepsPerRevolution",
            "type": int,
            "min": 1,
        },
        "full_stroke_steps_1x": {
            "symbol": "kFullStrokeSteps1x",
            "type": int,
            "min": 1,
        },
    },
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


def load_config():
    if not CONFIG_PATH.exists():
        fail(f"Missing build configuration file: {CONFIG_PATH}")

    try:
        data = parse_simple_yaml(CONFIG_PATH)
    except ValueError as exc:
        fail(str(exc))

    if not isinstance(data, dict):
        fail("conf.yaml must define a top-level mapping")

    return data


def validate_scalar(path, value, rules):
    expected_type = rules["type"]
    if expected_type is bool:
        if not isinstance(value, bool):
            fail(f"{path} must be true or false")
        return

    if not isinstance(value, int) or isinstance(value, bool):
        fail(f"{path} must be an integer")

    if "allowed" in rules and value not in rules["allowed"]:
        allowed_text = ", ".join(str(item) for item in rules["allowed"])
        fail(f"{path} must be one of: {allowed_text}")

    if "min" in rules and value < rules["min"]:
        fail(f"{path} must be >= {rules['min']}")

    if "max" in rules and value > rules["max"]:
        fail(f"{path} must be <= {rules['max']}")


def validate_and_resolve(data):
    resolved = {}

    for section_name, section_schema in CONFIG_SCHEMA.items():
        section = data.get(section_name)
        if not isinstance(section, dict):
            fail(f"conf.yaml must contain a {section_name} mapping")

        for key, rules in section_schema.items():
            if key not in section:
                fail(f"Missing {section_name}.{key} in conf.yaml")

            value = section[key]
            validate_scalar(f"{section_name}.{key}", value, rules)
            resolved[rules["symbol"]] = value

    if resolved["kMaximumPositionMm"] <= resolved["kMinimumPositionMm"]:
        fail("motion.maximum_position must be greater than motion.minimum_position")

    motor_section = data["stepper_motor"]
    delay_map = motor_section.get("microsteps_delay")
    if not isinstance(delay_map, dict):
        fail("stepper_motor.microsteps_delay must be a mapping")

    for microsteps in SUPPORTED_MICROSTEPS:
        yaml_key = str(microsteps)
        if yaml_key not in delay_map:
            fail(f"Missing stepper_motor.microsteps_delay.{yaml_key} in conf.yaml")

        value = delay_map[yaml_key]
        validate_scalar(
            f"stepper_motor.microsteps_delay.{yaml_key}",
            value,
            {"type": int, "min": 5, "max": 100000},
        )
        resolved[f"kStepDelayUsForMicrosteps{microsteps}"] = value

    return resolved


def cpp_type_and_value(symbol_name, value):
    if isinstance(value, bool):
        return "bool", "true" if value else "false"

    if symbol_name in {
        "kDefaultCurrentMa",
        "kDefaultMicrosteps",
        "kHomingRetractSteps",
        "kHomingDoubleTapDistanceMm",
        "kHomingSecondSeekDelayMultiplier",
        "kMinimumPositionMm",
        "kMaximumPositionMm",
        "kMaximumSpeedMmPerSec",
        "kStepsPerRevolution",
        "kFullStrokeSteps1x",
    }:
        return "uint16_t", str(value)

    if symbol_name in {
        "kDriverUartBaud",
        "kStepDelayUsForMicrosteps1",
        "kStepDelayUsForMicrosteps2",
        "kStepDelayUsForMicrosteps4",
        "kStepDelayUsForMicrosteps8",
        "kStepDelayUsForMicrosteps16",
        "kStepDelayUsForMicrosteps32",
        "kStepDelayUsForMicrosteps64",
        "kStepDelayUsForMicrosteps128",
        "kStepDelayUsForMicrosteps256",
    }:
        return "uint32_t", str(value)

    if symbol_name == "kDefaultMoveSteps":
        return "long", str(value)

    return "int", str(value)


def write_header(resolved_values):
    BUILD_DIR.mkdir(parents=True, exist_ok=True)

    lines = [
        "#pragma once",
        "",
        "#include <Arduino.h>",
        "",
        "namespace generated_build_config {",
    ]

    for symbol_name, value in resolved_values.items():
        cpp_type, cpp_value = cpp_type_and_value(symbol_name, value)
        lines.append(f"constexpr {cpp_type} {symbol_name} = {cpp_value};")

    lines.extend(["}", ""])
    GENERATED_HEADER_PATH.write_text("\n".join(lines), encoding="utf-8")


config_data = load_config()
resolved_config = validate_and_resolve(config_data)
write_header(resolved_config)

ENV.Append(CPPPATH=[str(BUILD_DIR)])
