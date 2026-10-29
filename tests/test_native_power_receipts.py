#!/usr/bin/env python3
"""Compile native power publication against recording sensors, without hardware."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile

from test_charging_power import COMPONENT, definition


def main():
    source = (COMPONENT / "vehicle_state_manager.cpp").read_text()
    header = (COMPONENT / "vehicle_state_manager.h").read_text()
    native = definition(source, "if (charge_state.which_optional_charger_power)")
    publisher = definition(header, "template<typename T, typename V> static bool publish_sensor_state")
    publish = definition(source, "bool VehicleStateManager::publish_sensor").replace("VehicleStateManager::", "Manager::")
    harness = r'''
#include <cassert>
#include <cmath>
#include <string>
#include <type_traits>
struct ChargeState {
  bool which_optional_charger_power = false;
  struct { float charger_power = 0; } optional_charger_power;
};
struct Sensor {
  float state = NAN;
  int raw_samples = 0;
  bool has_state() const { return raw_samples > 0; }
  void publish_state(float value) { state = value; ++raw_samples; }
};
struct Manager {
  Sensor power;
  bool configured = true;
  PUBLISHER
  Sensor* get_sensor(const std::string &id) {
    return configured && id == "charger_power" ? &power : nullptr;
  }
  bool publish_sensor(const std::string &id, float value, bool force_report = false);
  void update(const ChargeState &charge_state) { NATIVE }
};
PUBLISH
int main() {
  Manager first, second;
  ChargeState sample;
  sample.which_optional_charger_power = true;
  sample.optional_charger_power.charger_power = 7.0f;
  first.update(sample);
  assert(first.power.raw_samples == 1 && first.power.state == 7.0f);
  first.update(sample);
  assert(first.power.raw_samples == 2); // a confirmed unchanged native sample
  second.update(sample);
  assert(second.power.raw_samples == 1 && first.power.raw_samples == 2);
  sample.which_optional_charger_power = false; // omitted, including a stopped packet
  first.update(sample);
  assert(first.power.raw_samples == 2);
  sample.which_optional_charger_power = true;
  for (float invalid : {-1.0f, 501.0f, NAN, INFINITY}) {
    sample.optional_charger_power.charger_power = invalid;
    first.update(sample);
    assert(first.power.raw_samples == 2);
  }
  sample.optional_charger_power.charger_power = 0.0f;
  first.update(sample);
  first.update(sample);
  assert(first.power.raw_samples == 4 && first.power.state == 0.0f);
  second.configured = false;
  second.update(sample); // optional sensor absent
  assert(second.power.raw_samples == 1);
}
'''.replace("PUBLISHER", publisher).replace("NATIVE", native).replace("PUBLISH", publish)
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)
        (path / "check.cpp").write_text(harness)
        subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
            "-std=c++17", "-Wall", "-Wextra", "-Werror", str(path / "check.cpp"),
            "-o", str(path / "check"),
        ], check=True)
        subprocess.run([str(path / "check")], check=True)
    print("OK: repeated native power, missing/invalid fields, zero, two bridges")


if __name__ == "__main__":
    main()
