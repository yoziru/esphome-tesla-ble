#!/usr/bin/env python3
"""Structural consistency checks for the tesla_ble_vehicle codegen.

The component's entities are data-driven: lists of dicts in __init__.py are
turned into ESPHome entities at build time. This script parses those lists with
the stdlib `ast` module (no esphome import needed) and checks that every entry
is well-formed: required keys present, entity ids unique across all lists.

Run locally with:  make test-python   (or just:  make test)
"""

import ast
import sys
from pathlib import Path

COMPONENT = (
    Path(__file__).resolve().parent.parent
    / "components" / "tesla_ble_vehicle" / "__init__.py"
)

ENTITY_LISTS = {
    "BINARY_SENSORS": {"required": ("id", "name")},
    "SENSORS": {"required": ("id", "name")},
    "TEXT_SENSORS": {"required": ("id", "name")},
    "BUTTONS": {"required": ("id", "name", "class", "setter")},
    "SWITCHES": {"required": ("id", "name", "class", "setter")},
}


def find_list(tree: ast.Module, name: str):
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(
            isinstance(target, ast.Name) and target.id == name for target in node.targets
        ):
            if isinstance(node.value, ast.List):
                return node.value.elts
    raise AssertionError(f"list '{name}' not found in {COMPONENT}")


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

    if failures:
        print(f"FAILED: {failures}/{checks} checks")
        return 1
    print(f"OK: {checks} checks passed across {len(ENTITY_LISTS)} entity lists")
    return 0


if __name__ == "__main__":
    sys.exit(main())
