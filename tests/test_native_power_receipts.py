#!/usr/bin/env python3
"""Compile native power publication against recording sensors, without hardware."""

import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import textwrap

from test_charging_power import COMPONENT, definition


def check_timestamp_lambda():
    package = (COMPONENT.parent.parent / "packages/charging-freshness.yml").read_text()
    timestamp = textwrap.dedent(package.split("state: !lambda |-\n", 1)[1].split("\n\ntext_sensor:", 1)[0])
    harness = r'''
#include <cassert>
#include <cmath>
#include <ctime>
#include <string>
struct ReceivedTime {
  bool valid = true;
  bool is_valid() const { return valid; }
  std::string strftime(const char *format) const {
    std::tm utc{};
    utc.tm_year = 126; utc.tm_mon = 10; utc.tm_mday = 6;
    utc.tm_hour = 1; utc.tm_min = 2; utc.tm_sec = 3;
    char buffer[64];
    std::strftime(buffer, sizeof(buffer), format, &utc);
    return buffer;
  }
};
struct Clock {
  ReceivedTime now;
  ReceivedTime utcnow() const { return now; }
} homeassistant_time;
#define id(name) name
std::string receipt(float x) { TIMESTAMP }
int main() {
  for (float valid : {0.0f, 7.0f, 500.0f})
    assert(receipt(valid) == "2026-11-06T01:02:03Z");
  for (float invalid : {-1.0f, 501.0f, NAN, INFINITY})
    assert(receipt(invalid) == "unavailable");
  homeassistant_time.now.valid = false;
  assert(receipt(7.0f) == "unavailable");
}
'''.replace("TIMESTAMP", timestamp)
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)
        (path / "timestamp.cpp").write_text(harness)
        subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
            "-std=c++17", "-Wall", "-Wextra", "-Werror", str(path / "timestamp.cpp"),
            "-o", str(path / "timestamp"),
        ], check=True)
        subprocess.run([str(path / "timestamp")], check=True)
    print("OK: package timestamp formatting, invalid clock and invalid power")


def main():
    source = (COMPONENT / "vehicle_state_manager.cpp").read_text()
    header = (COMPONENT / "vehicle_state_manager.h").read_text()
    native = definition(source, "if (charge_state.which_optional_charger_power)")
    publisher = definition(header, "template<typename T, typename V> static bool publish_sensor_state")
    publish = definition(source, "bool VehicleStateManager::publish_sensor").replace("VehicleStateManager::", "Manager::")
    setter = definition(header, "void set_charger_power_force_report")
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
  bool charger_power_force_report_ = false;
  SETTER
  PUBLISHER
  Sensor* get_sensor(const std::string &id) {
    return configured && id == "charger_power" ? &power : nullptr;
  }
  bool publish_sensor(const std::string &id, float value, bool force_report = false);
  void update(const ChargeState &charge_state) { NATIVE }
};
PUBLISH
int main() {
  Manager first, second, defaults;
  ChargeState sample;
  sample.which_optional_charger_power = true;
  sample.optional_charger_power.charger_power = 7.0f;
  defaults.update(sample);
  defaults.update(sample);
  assert(defaults.power.raw_samples == 1); // unchanged legacy samples stay suppressed
  first.set_charger_power_force_report(true);
  second.set_charger_power_force_report(true);
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
  defaults.update(sample); // changed values still publish without opting in
  defaults.update(sample);
  assert(defaults.power.raw_samples == 2 && defaults.power.state == 0.0f);
  first.set_charger_power_force_report(false);
  first.update(sample);
  assert(first.power.raw_samples == 4);
}
'''.replace("PUBLISHER", publisher).replace("NATIVE", native).replace("PUBLISH", publish).replace("SETTER", setter)
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory)
        (path / "check.cpp").write_text(harness)
        subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
            "-std=c++17", "-Wall", "-Wextra", "-Werror", str(path / "check.cpp"),
            "-o", str(path / "check"),
        ], check=True)
        subprocess.run([str(path / "check")], check=True)
    print("OK: opt-in repeated power, default deduplication, missing/invalid fields, zero, two bridges")
    check_timestamp_lambda()


if __name__ == "__main__":
    main()
