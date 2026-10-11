#!/usr/bin/env python3
"""Run the component's command-result methods with recording platform adapters.

Compile production method bodies, not copies of their decisions. ESPHome/ESP-IDF
are unavailable to the plain host compiler; firmware compilation covers wiring.
"""

import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

COMPONENT = Path(__file__).resolve().parent.parent / "components/tesla_ble_vehicle"


def method(source, name):
    start = re.search(r"^[\w:<>]+ TeslaBLEVehicle::" + name + r"\(", source, re.M).start()
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


HARNESS = r'''
#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
namespace TeslaBLE {
struct Error {
  bool may_have_succeeded() const { return false; }
  std::string message() const { return "rejected"; }
};
struct OperationResult {
  int outcome;
  bool is_success() const { return outcome == 0; }
  bool is_skipped() const { return outcome == 1; }
  bool is_failure() const { return outcome == 2; }
  const Error *error() const { return nullptr; }
};
struct Command { using OperationResultCallback = std::function<void(OperationResult)>; };
}
struct Platform {
  bool warning = false, timeout = false;
  void status_momentary_warning(const char *) { warning = timeout = true; }
  void cancel_timeout(const char *) { timeout = false; }
  void status_clear_warning() { warning = false; }
  void status_set_warning(const char *) { warning = true; }
  void expire() { if (timeout) { timeout = warning = false; } }
};
struct StateRecorder {
  int publishes = 0, republishes = 0;
  float last = 0;
  int get_charging_amps_max() const { return 32; }
  void update_charging_control_state(bool v) { ++publishes; last = v; }
  void update_charging_amps(float v) { ++publishes; last = v; }
  void update_charging_limit(float v) { ++publishes; last = v; }
  void update_steering_wheel_heat(bool v) { ++publishes; last = v; }
  void update_sentry_mode(bool v) { ++publishes; last = v; }
  void republish_charging_amps() { ++republishes; }
  void republish_charging_limit() { ++republishes; }
};
struct VehicleAdapter {
  int outcome = 0;
  void set_connected(bool) {}
  template<typename V> void set_charging_state(V, TeslaBLE::Command::OperationResultCallback cb) { cb({outcome}); }
  template<typename V> void set_charging_amps(V, TeslaBLE::Command::OperationResultCallback cb) { cb({outcome}); }
  template<typename V> void set_charging_limit(V, TeslaBLE::Command::OperationResultCallback cb) { cb({outcome}); }
  template<typename V> void set_steering_wheel_heat(V, TeslaBLE::Command::OperationResultCallback cb) { cb({outcome}); }
  template<typename V> void set_sentry_mode(V, TeslaBLE::Command::OperationResultCallback cb) { cb({outcome}); }
};
struct BleAdapter { void clear_queues() {} };
struct PollPolicy { void reset() {} };
struct TextSensor { void publish_state(const std::string &) {} };
constexpr int MIN_CHARGING_LIMIT = 50, MAX_CHARGING_LIMIT = 100;
struct TeslaBLEVehicle : Platform {
  StateRecorder state_manager_;
  std::shared_ptr<VehicleAdapter> vehicle_ = std::make_shared<VehicleAdapter>();
  std::shared_ptr<BleAdapter> ble_adapter_;
  TextSensor *last_command_sensor_ = nullptr;
  int last_vcsec_poll_ = 0;
  PollPolicy poll_policy_;
  void handle_command_result(const std::string &, TeslaBLE::OperationResult);
  void handle_connection_lost();
  void track(const std::string &, std::function<void(TeslaBLE::Command::OperationResultCallback)>,
             std::function<void(bool)> = nullptr);
  int set_charging_state(bool);
  int set_charging_amps(int);
  int set_charging_limit(int);
  void set_steering_wheel_heat(bool);
  void set_sentry_mode(bool);
};
'''

CHECKS = r'''
int main() {
  TeslaBLEVehicle component;
  component.handle_command_result("limit", {2});
  assert(component.warning && component.timeout);
  component.expire();
  assert(!component.warning);
  for (int healthy : {0, 1}) {
    component.handle_command_result("limit", {2});
    component.handle_command_result("poll", {healthy});
    assert(!component.warning && !component.timeout);
  }
  component.handle_command_result("limit", {2});
  component.handle_connection_lost();
  component.expire();
  assert(component.warning && !component.timeout);
  for (int outcome : {0, 1, 2}) {
    component.vehicle_->outcome = outcome;
    component.state_manager_ = {};
    component.set_charging_state(true);
    component.set_charging_amps(50);
    if (outcome == 0) assert(component.state_manager_.last == 32);
    component.set_charging_limit(80);
    if (outcome == 0) assert(component.state_manager_.last == 80);
    component.set_steering_wheel_heat(true);
    component.set_sentry_mode(true);
    assert(component.state_manager_.publishes == (outcome == 0 ? 5 : 0));
    assert(component.state_manager_.republishes == (outcome == 0 ? 0 : 2));
  }
}
'''


def main():
    source = (COMPONENT / "tesla_ble_vehicle.cpp").read_text()
    names = (
        "handle_command_result", "handle_connection_lost", "track",
        "set_charging_state", "set_charging_amps", "set_charging_limit",
        "set_steering_wheel_heat", "set_sentry_mode",
    )
    with tempfile.TemporaryDirectory() as directory:
        cpp = Path(directory) / "command_results.cpp"
        binary = Path(directory) / "command_results"
        cpp.write_text(HARNESS + "\n".join(method(source, name) for name in names) + CHECKS)
        subprocess.run([
            *shlex.split(os.environ.get("CXX", "c++")), "-std=c++17", "-Wall", "-Wextra",
            str(cpp), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
    print("OK: component command outcomes, number rollback, warning expiry/disconnect")


if __name__ == "__main__":
    main()
