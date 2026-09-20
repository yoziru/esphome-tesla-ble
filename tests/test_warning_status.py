#!/usr/bin/env python3
"""Regression checks for the tesla_ble_vehicle warning lifecycle."""

import sys
from pathlib import Path


COMPONENT_CPP = (
    Path(__file__).resolve().parent.parent
    / "components"
    / "tesla_ble_vehicle"
    / "tesla_ble_vehicle.cpp"
)


def main() -> int:
    source = COMPONENT_CPP.read_text(encoding="utf-8")
    checks = 0
    failures = 0

    command_result = source[source.index("void TeslaBLEVehicle::handle_command_result") :]
    command_result = command_result[: command_result.index("void TeslaBLEVehicle::send_command_with_tracking")]
    checks += 1
    if 'status_momentary_warning("command-failed-warning")' not in command_result:
        print("  FAIL command failures must use a self-clearing warning")
        failures += 1

    checks += 1
    if 'status_set_warning("Command failed")' in command_result:
        print("  FAIL command failures must not set a persistent warning")
        failures += 1

    connection_lost = source[source.index("void TeslaBLEVehicle::handle_connection_lost") :]
    checks += 1
    if 'cancel_timeout("command-failed-warning")' not in connection_lost:
        print("  FAIL connection loss must cancel the transient command warning")
        failures += 1

    if failures:
        print(f"FAILED: {failures}/{checks} checks")
        return 1
    print(f"OK: {checks} warning lifecycle checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
