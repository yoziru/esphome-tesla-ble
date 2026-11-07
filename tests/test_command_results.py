#!/usr/bin/env python3
"""Run the component's command-result and polling methods with recording adapters.

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
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include "polling_policy.h"
#include <string>
#include <utility>
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
uint32_t clock_ms = 100;
unsigned millis() { return clock_ms; }
struct VCSEC_ClosureStatuses { int chargePort = 0; };
struct VCSEC_VehicleStatus {
  int vehicleSleepStatus = 1;
  bool has_closureStatuses = false;
  VCSEC_ClosureStatuses closureStatuses;
};
constexpr int VCSEC_ClosureState_E_CLOSURESTATE_OPEN = 1;
enum Keys_Role { Keys_Role_ROLE_OWNER, Keys_Role_ROLE_DRIVER, Keys_Role_ROLE_CHARGING_MANAGER };
namespace TeslaBLE {
namespace state_text {
std::optional<bool> sleep_status(int status) {
  if (status == 1) return false;
  if (status == 2) return true;
  return std::nullopt;
}
}
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
  bool asleep = false;
  bool charging = false, sentry = false;
  int get_charging_amps_max() const { return 32; }
  bool is_asleep() const { return asleep; }
  bool is_charging() const { return charging; }
  bool is_sentry_mode() const { return sentry; }
  void update_vehicle_status(const VCSEC_VehicleStatus &s) { asleep = s.vehicleSleepStatus != 1; }
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
  int polls = 0;
  int infotainment_polls = 0;
  int wake_capable_polls = 0;
  int pending_command_count() const { return 0; }
  void set_connected(bool) {}
  void vcsec_poll(TeslaBLE::WakePolicy policy = TeslaBLE::WakePolicy::WAKE_IF_NEEDED) {
    ++polls;
    if (policy == TeslaBLE::WakePolicy::WAKE_IF_NEEDED) ++wake_capable_polls;
  }
  void infotainment_poll(TeslaBLE::WakePolicy policy) {
    ++infotainment_polls;
    vcsec_poll(policy);
  }
  void pair(Keys_Role, TeslaBLE::Command::OperationResultCallback cb) { cb({outcome}); }
  template<typename V> void set_charging_state(V, TeslaBLE::Command::OperationResultCallback cb) { cb({outcome}); }
  template<typename V> void set_charging_amps(V, TeslaBLE::Command::OperationResultCallback cb) { cb({outcome}); }
  template<typename V> void set_charging_limit(V, TeslaBLE::Command::OperationResultCallback cb) { cb({outcome}); }
  template<typename V> void set_steering_wheel_heat(V, TeslaBLE::Command::OperationResultCallback cb) { cb({outcome}); }
  template<typename V> void set_sentry_mode(V, TeslaBLE::Command::OperationResultCallback cb) { cb({outcome}); }
};
struct BleAdapter { void clear_queues() {} };
struct TextSensor { std::string state; void publish_state(const std::string &v) { state = v; } };
struct BinarySensor { bool state = false; void publish_state(bool v) { state = v; } };
constexpr int MIN_CHARGING_LIMIT = 50, MAX_CHARGING_LIMIT = 100;
struct TeslaBLEVehicle : Platform {
  StateRecorder state_manager_;
  std::shared_ptr<VehicleAdapter> vehicle_ = std::make_shared<VehicleAdapter>();
  std::shared_ptr<BleAdapter> ble_adapter_;
  TextSensor *last_command_sensor_ = nullptr;
  BinarySensor *pairing_required_sensor_ = nullptr;
  bool key_rejected_ = false;
  std::string role_ = "DRIVER";
  uint32_t last_vcsec_poll_ = 0;
  uint32_t vcsec_poll_interval_ = 10000;
  TeslaBLE::InfotainmentPollPolicy poll_policy_;
  bool probe_started_ = false;
  bool was_asleep_ = false;
  bool vehicle_status_known_ = false;
  uint32_t last_vehicle_status_ = 0, last_probe_ = 0;
  bool probe_poll_pending_ = false;
  std::optional<uint32_t> last_charge_state_, last_closures_state_;
  bool charge_port_known_ = false;
  bool charge_port_open_ = false;
  bool is_connected() const { return true; }
  void update();
  void handle_vehicle_status(const VCSEC_VehicleStatus &);
  void handle_command_result(const std::string &, TeslaBLE::OperationResult);
  void handle_key_not_paired();
  void handle_connection_established();
  void handle_connection_lost();
  int start_pairing();
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
  BinarySensor pairing;
  TextSensor last_command;
  component.pairing_required_sensor_ = &pairing;
  component.last_command_sensor_ = &last_command;
  component.handle_key_not_paired();
  assert(pairing.state && component.warning && !component.timeout);
  assert(last_command.state.find("tap a key card") != std::string::npos);
  component.expire();
  component.handle_command_result("poll", {0});
  component.handle_connection_lost();
  component.handle_connection_established();
  assert(pairing.state && component.warning && component.vehicle_->polls == 0);
  component.vehicle_->outcome = 0;
  component.state_manager_ = {};
  component.set_charging_limit(80);
  assert(component.state_manager_.publishes == 0 && component.state_manager_.republishes == 1);
  for (int outcome : {2, 1}) {
    component.vehicle_->outcome = outcome;
    component.start_pairing();
    assert(pairing.state && component.warning);
  }
  component.vehicle_->outcome = 0;
  component.start_pairing();
  assert(!pairing.state && !component.warning && !component.key_rejected_);
  assert(component.vehicle_->polls == 1);
  assert(last_command.state == "Pair → Success");
  for (int flap = 0; flap < 5; ++flap) {
    component.handle_connection_lost();
    component.handle_connection_established();
  }
  assert(component.vehicle_->polls == 6);
  component.handle_connection_lost();
  component.handle_connection_established();
  assert(component.vehicle_->polls == 7);
  assert(component.vehicle_->wake_capable_polls == 0);

  // Stale awake across reconnect never authorizes a probe. Fresh awake does,
  // but fresh sleep, disconnect and cooldown prevent an unbounded burst.
  TeslaBLEVehicle wake;
  wake.poll_policy_.update(1, false, false, false);
  clock_ms = 700000;
  wake.handle_connection_established();
  wake.update();
  assert(wake.vehicle_->infotainment_polls == 0);
  VCSEC_VehicleStatus awake;
  VCSEC_VehicleStatus asleep;
  asleep.vehicleSleepStatus = 2;
  wake.handle_vehicle_status(awake);
  wake.update();
  assert(wake.vehicle_->infotainment_polls == 1);
  for (int step = 0; step < 5; ++step) {
    clock_ms += 30000;
    wake.handle_vehicle_status(awake);
    wake.update();
  }
  assert(wake.vehicle_->infotainment_polls == 6);
  clock_ms += 30000;
  wake.handle_vehicle_status(awake);
  wake.update();
  assert(wake.vehicle_->infotainment_polls == 6);
  assert(wake.poll_policy_.update(clock_ms, false, false, false).interval_ms == 660000);
  wake.handle_vehicle_status(asleep);
  wake.handle_vehicle_status(awake);
  wake.update();
  assert(wake.vehicle_->infotainment_polls == 6);
  clock_ms = 1400000;
  wake.handle_vehicle_status(awake);
  wake.update();
  assert(wake.poll_policy_.probe_window_active(clock_ms));
  wake.handle_connection_lost();
  assert(!wake.poll_policy_.probe_window_active(clock_ms));
  wake.handle_connection_established();
  wake.handle_vehicle_status(asleep);
  wake.update();
  assert(wake.vehicle_->infotainment_polls == 7);
  assert(wake.vehicle_->wake_capable_polls == 0);

  // Reconnecting inside the cooldown cannot re-arm the one-shot awake poll.
  wake.handle_connection_lost();
  wake.handle_connection_established();
  wake.handle_vehicle_status(awake);
  wake.update();
  assert(wake.vehicle_->infotainment_polls == 7);
  assert(!wake.poll_policy_.probe_window_active(clock_ms));

  // Missing or unknown VCSEC status cancels even an unsent one-shot poll.
  for (bool unknown : {false, true}) {
    TeslaBLEVehicle stale;
    stale.handle_vehicle_status(awake);
    if (unknown) {
      VCSEC_VehicleStatus unavailable;
      unavailable.vehicleSleepStatus = 0;
      stale.handle_vehicle_status(unavailable);
    } else {
      clock_ms += 120001;
    }
    stale.update();
    assert(stale.vehicle_->infotainment_polls == 0);
    assert(!stale.poll_policy_.probe_window_active(clock_ms));
    assert(!stale.probe_poll_pending_);
  }

  // Charging and sentry observations expire independently of fresh VCSEC awake.
  for (bool sentry : {false, true}) {
    TeslaBLEVehicle active;
    active.state_manager_.charging = !sentry;
    active.state_manager_.sentry = sentry;
    if (sentry) active.last_closures_state_ = clock_ms;
    else active.last_charge_state_ = clock_ms;
    active.handle_vehicle_status(awake);
    active.update();
    clock_ms += 10000;
    active.handle_vehicle_status(awake);
    active.update();
    assert(active.vehicle_->infotainment_polls == 2);
    clock_ms += 120001;
    active.handle_vehicle_status(awake);
    active.update();
    assert(active.vehicle_->infotainment_polls == 3);
    clock_ms += 10000;
    active.handle_vehicle_status(awake);
    active.update();
    assert(active.vehicle_->infotainment_polls == 3);
    assert(active.vehicle_->wake_capable_polls == 0);
    active.handle_connection_lost();
    assert(!active.last_charge_state_ && !active.last_closures_state_);
  }

  // Late charging takes over cadence; sleep still suppresses it.
  TeslaBLEVehicle charge;
  charge.poll_policy_.update(1, false, false, false);
  charge.handle_vehicle_status(awake);
  charge.update();
  charge.state_manager_.charging = true;
  charge.last_charge_state_ = clock_ms;
  clock_ms += 10000;
  charge.handle_vehicle_status(awake);
  charge.update();
  assert(charge.vehicle_->infotainment_polls == 2);
  charge.handle_vehicle_status(asleep);
  assert(!charge.poll_policy_.probe_window_active(clock_ms));
  charge.update();
  assert(charge.vehicle_->infotainment_polls == 2);
  clock_ms += 700000;
  charge.handle_vehicle_status(awake);
  charge.update();
  assert(charge.poll_policy_.update(clock_ms, false, false, false).wake_policy == TeslaBLE::WakePolicy::NO_WAKE_SKIP);

  // Charge-port activity restarts the idle window on the open edge, only while
  // awake. The first status after boot/reconnect is just a baseline.
  VCSEC_VehicleStatus closed;
  closed.has_closureStatuses = true;
  VCSEC_VehicleStatus open = closed;
  open.closureStatuses.chargePort = VCSEC_ClosureState_E_CLOSURESTATE_OPEN;
  TeslaBLEVehicle plug;
  plug.poll_policy_.update(clock_ms - 660001, false, false, false);
  plug.state_manager_.asleep = false;
  plug.handle_vehicle_status(open);
  assert(plug.poll_policy_.update(clock_ms, false, false, false).wake_policy == TeslaBLE::WakePolicy::NO_WAKE_SKIP);
  plug.handle_vehicle_status(closed);
  plug.handle_vehicle_status(open);
  assert(plug.poll_policy_.update(clock_ms, false, false, false).wake_policy == TeslaBLE::WakePolicy::WAKE_IF_NEEDED);
  plug.handle_vehicle_status(open);
  clock_ms += 660001;
  plug.handle_vehicle_status(open);
  assert(plug.poll_policy_.update(clock_ms, false, false, false).wake_policy == TeslaBLE::WakePolicy::NO_WAKE_SKIP);
  plug.handle_vehicle_status(closed);
  open.vehicleSleepStatus = 2;
  plug.handle_vehicle_status(open);
  assert(plug.poll_policy_.update(clock_ms, false, false, false).wake_policy == TeslaBLE::WakePolicy::NO_WAKE_SKIP);
}
'''


def main():
    source = (COMPONENT / "tesla_ble_vehicle.cpp").read_text()
    names = (
        "handle_command_result", "handle_key_not_paired", "handle_connection_established", "handle_connection_lost", "track", "start_pairing",
        "set_charging_state", "set_charging_amps", "set_charging_limit",
        "set_steering_wheel_heat", "set_sentry_mode", "update", "handle_vehicle_status",
    )
    with tempfile.TemporaryDirectory() as directory:
        cpp = Path(directory) / "command_results.cpp"
        binary = Path(directory) / "command_results"
        cpp.write_text(HARNESS + "\n".join(method(source, name) for name in names) + CHECKS)
        subprocess.run([
            *shlex.split(os.environ.get("CXX", "c++")), "-std=c++17", "-Wall", "-Wextra",
            "-I", str(Path(os.environ.get("TESLA_BLE_DIR", COMPONENT.parents[2] / "tesla-ble")) / "include"),
            str(cpp), "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
    print("OK: component command outcomes, number rollback, warning expiry/disconnect, awake/plug-in polls")


if __name__ == "__main__":
    main()
