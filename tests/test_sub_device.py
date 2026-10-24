#!/usr/bin/env python3
"""Check that `device_id` puts every vehicle entity on the ESPHome sub-device.

Runs the real ESPHome code generator (`compile --only-generate`, no toolchain
or network needed) on tests/sub_device_config.yml twice:

- with `-s vehicle_device_id tesla`: every entity in the firmware (all entities
  from the tesla_ble_vehicle definition lists plus the two ble_client entities
  in packages/client.yml) must call set_device_(tesla)
- with the default empty substitution: no entity may call set_device_

Run locally with:  make test-sub-device
The ESPHome command comes from $ESPHOME (default: esphome).
"""

import ast
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

from test_codegen import COMPONENT, ENTITY_LISTS, find_entries

TESTS_DIR: Path = Path(__file__).resolve().parent
CONFIG: Path = TESTS_DIR / "sub_device_config.yml"
MAIN_CPP: Path = TESTS_DIR / ".esphome/build/tesla-ble-sub-device-test/src/main.cpp"
DEVICE: str = "tesla"
# ble_client RSSI sensor and connection switch from packages/client.yml
BLE_LINK_ENTITIES: int = 2

# App.register_sensor(var), App.register_switch(var), ... (not components/devices)
REGISTER_ENTITY = re.compile(r"App\.register_(?!component_|device)\w+\((\w+)[,)]")
SET_DEVICE = re.compile(r"(\w+)->set_device_\((\w+)\);")


def vehicle_entity_ids() -> set[str]:
    """Return the definition ids of every entity tesla_ble_vehicle creates."""
    tree = ast.parse(COMPONENT.read_text(encoding="utf-8"), filename=str(COMPONENT))
    return {
        value.value
        for list_name in ENTITY_LISTS
        for entry in find_entries(tree, list_name)
        if isinstance(entry, ast.Dict)
        for key, value in zip(entry.keys, entry.values)
        if isinstance(key, ast.Constant) and key.value == "id"
        and isinstance(value, ast.Constant)
    }


def generate(*substitutions: str) -> str:
    """Generate the fixture's C++ and return main.cpp."""
    esphome = shlex.split(os.environ.get("ESPHOME", "esphome"))
    result = subprocess.run(
        [*esphome, *substitutions, "compile", "--only-generate", str(CONFIG)],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise AssertionError(f"esphome failed:\n{result.stdout}{result.stderr}")
    return MAIN_CPP.read_text(encoding="utf-8")


def check(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        print(f"  FAIL {message}")
        failures.append(message)


def main() -> int:
    failures: list[str] = []
    expected_ids = vehicle_entity_ids()

    source = generate("-s", "vehicle_device_id", DEVICE)
    entities = set(REGISTER_ENTITY.findall(source))
    on_device = dict(SET_DEVICE.findall(source))
    check(f"App.register_device({DEVICE});" in source,
          f"sub-device '{DEVICE}' not registered", failures)
    for entity_id in sorted(expected_ids):
        check(any(var.startswith(f"tesla_{entity_id}_") for var in entities),
              f"entity '{entity_id}' not generated", failures)
    check(len(entities) == len(expected_ids) + BLE_LINK_ENTITIES,
          f"expected {len(expected_ids) + BLE_LINK_ENTITIES} entities, "
          f"got {len(entities)}", failures)
    for var in sorted(entities):
        check(on_device.get(var) == DEVICE,
              f"{var} not on device '{DEVICE}' (got {on_device.get(var)})", failures)

    source = generate()
    check(not SET_DEVICE.search(source),
          "entity placed on a sub-device without vehicle_device_id", failures)
    check(len(set(REGISTER_ENTITY.findall(source))) == len(entities),
          "entity count differs without vehicle_device_id", failures)

    if failures:
        print(f"FAILED: {len(failures)} sub-device checks")
        return 1
    print(f"OK: {len(entities)} entities on '{DEVICE}' with vehicle_device_id, "
          "none on a sub-device without it")
    return 0


if __name__ == "__main__":
    sys.exit(main())
