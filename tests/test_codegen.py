#!/usr/bin/env python3
"""Structural consistency checks for the tesla_ble_vehicle codegen.

The component's entities are data-driven: lists of dicts in __init__.py are
turned into ESPHome entities at build time. This script parses those lists with
the stdlib `ast` module (no esphome import needed) and checks that:

- every entity entry is well-formed: required keys present, entity ids unique
  across all lists
- the sensor ids used from C++ (publish_* calls in vehicle_state_manager.cpp,
  or a dedicated setter method) match the ids defined in the Python lists, in
  both directions - a typo on either side would silently produce a dead sensor

Run locally with:  make test-python   (or just:  make test)
"""

import ast
import re
import sys
from pathlib import Path

COMPONENT_DIR = (
    Path(__file__).resolve().parent.parent / "components" / "tesla_ble_vehicle"
)
COMPONENT = COMPONENT_DIR / "__init__.py"
STATE_MANAGER_CPP = COMPONENT_DIR / "vehicle_state_manager.cpp"

ENTITY_LISTS = {
    "BINARY_SENSORS": {"required": ("id", "name")},
    "SENSORS": {"required": ("id", "name")},
    "TEXT_SENSORS": {"required": ("id", "name")},
    "BUTTONS": {"required": ("id", "name", "class", "setter")},
    "SWITCHES": {"required": ("id", "name", "class", "setter")},
}

# These lists are published from C++; the other lists are controls created by
# their own setters and are not part of the sensor-wiring cross-check.
SENSOR_LISTS = ("BINARY_SENSORS", "SENSORS", "TEXT_SENSORS")

# publish_text_sensor("id", ...) / publish_binary_sensor("id", ...) / publish_sensor("id", ...)
PUBLISH_CALL = re.compile(r'publish_(?:binary_|text_)?sensor\("([a-z0-9_]+)"')


def find_list(tree: ast.Module, name: str):
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(
            isinstance(target, ast.Name) and target.id == name for target in node.targets
        ):
            if isinstance(node.value, ast.List):
                return node.value.elts
    raise AssertionError(f"list '{name}' not found in {COMPONENT}")


def collect_sensor_ids(tree: ast.Module):
    """Map each defined sensor id to its dedicated setter method name (or None)."""
    sensors = {}
    for list_name in SENSOR_LISTS:
        for entry in find_list(tree, list_name):
            if not isinstance(entry, ast.Dict):
                continue
            values = {
                key.value: value
                for key, value in zip(entry.keys, entry.values)
                if isinstance(key, ast.Constant) and isinstance(key.value, str)
            }
            if not isinstance(values.get("id"), ast.Constant):
                continue
            setter = values.get("setter")
            setter_name = (
                setter.value
                if isinstance(setter, ast.Constant) and isinstance(setter.value, str)
                else None
            )
            sensors[values["id"].value] = setter_name
    return sensors


def check_sensor_wiring(sensors: dict, checks: int, failures: int):
    """Cross-check the ids published from C++ against the defined sensor ids."""
    # Strip // comments so a commented-out publish_* call is not counted.
    cpp_text = re.sub(r"//.*", "", STATE_MANAGER_CPP.read_text(encoding="utf-8"))
    headers_text = "".join(
        path.read_text(encoding="utf-8")
        for path in sorted(COMPONENT_DIR.glob("*.h"))
    )

    published = set(PUBLISH_CALL.findall(cpp_text))

    for entity_id in sorted(published):
        checks += 1
        if entity_id not in sensors:
            print(f"  FAIL publish_* uses undefined sensor id '{entity_id}'")
            failures += 1

    for entity_id, setter in sorted(sensors.items()):
        checks += 1
        if entity_id in published:
            continue
        if setter and setter in headers_text:
            continue
        print(f"  FAIL sensor '{entity_id}' defined but not wired in C++ "
              f"(no publish_* call"
              + (f" and no '{setter}' method)" if setter else ")"))
        failures += 1

    return checks, failures


def main() -> int:
    tree = ast.parse(COMPONENT.read_text(encoding="utf-8"), filename=str(COMPONENT))

    checks = 0
    failures = 0
    seen_ids = {}

    for list_name, spec in ENTITY_LISTS.items():
        for entry in find_list(tree, list_name):
            if not isinstance(entry, ast.Dict):
                continue
            keys = {
                key.value
                for key in entry.keys
                if isinstance(key, ast.Constant) and isinstance(key.value, str)
            }

            for required in spec["required"]:
                checks += 1
                if required not in keys:
                    print(f"  FAIL {list_name}: entry missing required key '{required}'")
                    failures += 1

            id_node = next(
                (value for key, value in zip(entry.keys, entry.values)
                 if isinstance(key, ast.Constant) and key.value == "id"),
                None,
            )
            if isinstance(id_node, ast.Constant) and isinstance(id_node.value, str):
                entity_id = id_node.value
                checks += 1
                if entity_id in seen_ids:
                    print(f"  FAIL duplicate entity id '{entity_id}' (in {list_name}, "
                          f"already in {seen_ids[entity_id]})")
                    failures += 1
                else:
                    seen_ids[entity_id] = list_name

    checks, failures = check_sensor_wiring(collect_sensor_ids(tree), checks, failures)

    if failures:
        print(f"FAILED: {failures}/{checks} checks")
        return 1
    print(f"OK: {checks} checks passed across {len(ENTITY_LISTS)} entity lists")
    return 0


if __name__ == "__main__":
    sys.exit(main())
