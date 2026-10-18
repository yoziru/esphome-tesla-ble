#!/usr/bin/env python3
"""Run production power publishing against recording sensors, including NaN recovery."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile

COMPONENT = Path(__file__).resolve().parent.parent / "components/tesla_ble_vehicle"


def definition(source, marker):
    start = source.index(marker)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main():
    header = (COMPONENT / "vehicle_state_manager.h").read_text()
    source = (COMPONENT / "vehicle_state_manager.cpp").read_text()
    publisher = definition(header, "template<typename T, typename V> static bool publish_sensor_state")
    update = definition(source, "void VehicleStateManager::update_charging_power")
    harness = r'''
#include <cassert>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
namespace TeslaBLE {
struct ChargingPower {
  std::optional<double> power_w;
  std::optional<int> effective_phases;
  std::optional<bool> is_ac;
};
}
struct Sensor {
  float state = NAN;
  bool has_state() const { return true; }
  void publish_state(float value) { state = value; }
};
struct VehicleStateManager {
  std::map<std::string, Sensor> sensors;
  void update_charging_power(const TeslaBLE::ChargingPower &);
  PUBLISHER
  void publish_sensor(const std::string &id, float value) { publish_sensor_state(&sensors[id], value); }
};
UPDATE
int main() {
  VehicleStateManager manager;
  TeslaBLE::ChargingPower power;
  power.power_w = 11184;
  power.effective_phases = 3;
  power.is_ac = true;
  manager.update_charging_power(power);
  assert(std::abs(manager.sensors["charger_power_estimated"].state - 11.184f) < 0.0001f);
  assert(manager.sensors["charger_effective_phases"].state == 3);
  manager.update_charging_power({});
  assert(std::isnan(manager.sensors["charger_power_estimated"].state));
  assert(std::isnan(manager.sensors["charger_effective_phases"].state));
  manager.update_charging_power(power);
  assert(manager.sensors["charger_effective_phases"].state == 3);
  assert(std::abs(manager.sensors["charger_power_estimated"].state - 11.184f) < 0.0001f);
  power.is_ac = false;
  power.power_w = 80000;
  power.effective_phases.reset();
  manager.update_charging_power(power);
  assert(std::isnan(manager.sensors["charger_power_estimated"].state));
  assert(std::isnan(manager.sensors["charger_effective_phases"].state));
}
'''.replace("PUBLISHER", publisher).replace("UPDATE", update)
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)
        (path / "check.cpp").write_text(harness)
        subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
            "-std=c++17", "-Wall", "-Wextra", "-Werror", str(path / "check.cpp"),
            "-o", str(path / "check"),
        ], check=True)
        subprocess.run([str(path / "check")], check=True)
    print("OK: shared power publishing, unknown transitions, recovery and DC exclusion")


if __name__ == "__main__":
    main()
