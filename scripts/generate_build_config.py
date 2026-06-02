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
            "symbol": "kMinimumPositionMilliMm",
            "type": "milli_mm",
            "min": 0,
        },
        "maximum_position": {
            "symbol": "kMaximumPositionMilliMm",
            "type": "milli_mm",
            "min": 0,
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
    "arduino": {
        "debug_mode": {
            "symbol": "kDebugMode",
            "type": bool,
        },
        "save_config_to_eeprom": {
            "symbol": "kSaveConfigToEeprom",
            "type": bool,
        },
    },
    "stepper_motor": {
        "steps_per_mm": {
            "symbol": "kStepsPerMmX1000",
            "type": "fixed_3dp",
            "min": 1,
        },
        "full_stroke_mm": {
            "symbol": "kFullStrokeMilliMm",
            "type": "milli_mm",
            "min": 1,
        },
        "full_stroke_steps_1x": {
            "symbol": "kFullStrokeSteps1x",
            "type": int,
            "min": 1,
        },
    },
    "aperture_iris": {
        "min_mm": {
            "symbol": "kApertureIrisMinMilliMm",
            "type": "milli_mm",
            "min": 0,
        },
        "max_mm": {
            "symbol": "kApertureIrisMaxMilliMm",
            "type": "milli_mm",
            "min": 1000,
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
        try:
            return float(value)
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

    if expected_type == "fixed_3dp":
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            fail(f"{path} must be a number")
        scaled = round(float(value) * 1000.0)
        if abs(float(value) * 1000.0 - scaled) > 1e-6:
            fail(f"{path} must use at most 3 decimal places")
        if "min" in rules and scaled < rules["min"]:
            fail(f"{path} must be >= {rules['min'] / 1000.0:.3f}")
        if "max" in rules and scaled > rules["max"]:
            fail(f"{path} must be <= {rules['max'] / 1000.0:.3f}")
        return

    if expected_type == "milli_mm":
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            fail(f"{path} must be a number")
        scaled = round(float(value) * 1000.0)
        if abs(float(value) * 1000.0 - scaled) > 1e-6:
            fail(f"{path} must use at most 3 decimal places")
        if "min" in rules and scaled < rules["min"]:
            fail(f"{path} must be >= {rules['min'] / 1000.0:.3f}")
        if "max" in rules and scaled > rules["max"]:
            fail(f"{path} must be <= {rules['max'] / 1000.0:.3f}")
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
            if rules["type"] in {"milli_mm", "fixed_3dp"}:
                resolved[rules["symbol"]] = int(round(float(value) * 1000.0))
            else:
                resolved[rules["symbol"]] = value

    if resolved["kMaximumPositionMilliMm"] <= resolved["kMinimumPositionMilliMm"]:
        fail("motion.maximum_position must be greater than motion.minimum_position")
    if resolved["kApertureIrisMaxMilliMm"] <= resolved["kApertureIrisMinMilliMm"]:
        fail("aperture_iris.max_mm must be greater than aperture_iris.min_mm")
    if resolved["kFullStrokeMilliMm"] <= 0:
        fail("stepper_motor.full_stroke_mm must be greater than 0")

    derived_steps_per_mm_x1000 = int(
        round(
            resolved["kFullStrokeSteps1x"] * 1000000.0 /
            resolved["kFullStrokeMilliMm"]
        )
    )
    resolved["kDerivedStepsPerMmX1000"] = derived_steps_per_mm_x1000

    difference = abs(resolved["kStepsPerMmX1000"] - derived_steps_per_mm_x1000)
    if difference > 100:
        print(
            "WARNING: stepper_motor.steps_per_mm differs from "
            "full_stroke_steps_1x/full_stroke_mm by more than 0.100 steps/mm. "
            f"configured={resolved['kStepsPerMmX1000'] / 1000.0:.3f}, "
            f"derived={derived_steps_per_mm_x1000 / 1000.0:.3f}"
        )

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
        "kMaximumSpeedMmPerSec",
        "kFullStrokeSteps1x",
    }:
        return "uint16_t", str(value)

    if symbol_name in {
        "kMinimumPositionMilliMm",
        "kMaximumPositionMilliMm",
        "kApertureIrisMinMilliMm",
        "kApertureIrisMaxMilliMm",
        "kFullStrokeMilliMm",
    }:
        return "int32_t", str(value)

    if symbol_name in {
        "kDriverUartBaud",
        "kStepsPerMmX1000",
        "kDerivedStepsPerMmX1000",
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
