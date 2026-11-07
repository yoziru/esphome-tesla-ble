#include "test_helper.h"
#include "tesla_ble_vehicle.h"

using namespace esphome;
using namespace esphome::tesla_ble_vehicle;
using TeslaBLE::OperationResult;
using TeslaBLE::OperationOutcome;
using TeslaBLE::WakePolicy;

static OperationResult result(OperationOutcome outcome) {
  if (outcome == OperationOutcome::SUCCESS) return OperationResult::success();
  if (outcome == OperationOutcome::SKIPPED)
    return OperationResult::skipped(TeslaBLE::OperationTerminalReason::VEHICLE_ASLEEP);
  return OperationResult::failure(TeslaBLE::CommandError::build_failed("rejected"));
}

struct Fixture {
  TeslaBLEVehicle component;
  binary_sensor::BinarySensor asleep, pairing;
  text_sensor::TextSensor last;
  TeslaChargingSwitch charging;
  TeslaSteeringWheelHeatSwitch heat;
  TeslaSentryModeSwitch sentry;
  TeslaChargingAmpsNumber amps;
  TeslaChargingLimitNumber limit;
  int vcsec_polls = 0, infotainment_polls = 0, wake_capable_polls = 0;

  Fixture() {
    component.set_vin("5YJ30123456789ABC");
    component.set_charging_amps_max(32);
    component.set_binary_sensor("asleep", &asleep);
    component.set_pairing_required_binary_sensor(&pairing);
    component.set_last_command_text_sensor(&last);
    component.set_charging_switch(&charging);
    component.set_steering_wheel_heat_switch(&heat);
    component.set_sentry_mode_switch(&sentry);
    component.set_charging_amps_number(&amps);
    component.set_charging_limit_number(&limit);
    component.setup();
    connect();
    drain();
    vcsec_polls = infotainment_polls = wake_capable_polls = 0;
  }

  auto &queue() { return component.vehicle_->get_command_queue_for_testing(); }
  void connect() {
    esp_ble_gattc_cb_param_t event{};
    component.gattc_event_handler(ESP_GATTC_REG_FOR_NOTIFY_EVT, 0, &event);
  }
  void disconnect() {
    esp_ble_gattc_cb_param_t event{};
    component.gattc_event_handler(ESP_GATTC_DISCONNECT_EVT, 0, &event);
    component.gattc_event_handler(ESP_GATTC_CLOSE_EVT, 0, &event);
  }
  void complete(OperationOutcome outcome) {
    CHECK(!queue().empty());
    if (queue().empty()) return;
    auto command = queue().front();
    queue().pop();
    CHECK(static_cast<bool>(command->on_complete));
    if (command->on_complete) command->on_complete(result(outcome));
  }
  void drain() {
    while (!queue().empty()) {
      const auto &command = queue().front();
      if (command->name == "VCSEC Poll") ++vcsec_polls;
      if (command->name == "Charge State Poll") ++infotainment_polls;
      if (command->wake_policy == WakePolicy::WAKE_IF_NEEDED) ++wake_capable_polls;
      queue().pop();
    }
  }
  void update() { component.update(); drain(); }
  void status(bool sleeping = false) {
    VCSEC_VehicleStatus s = VCSEC_VehicleStatus_init_default;
    s.vehicleSleepStatus = sleeping ? VCSEC_VehicleSleepStatus_E_VEHICLE_SLEEP_STATUS_ASLEEP
                                   : VCSEC_VehicleSleepStatus_E_VEHICLE_SLEEP_STATUS_AWAKE;
    component.vehicle_->vehicle_status_callback_(s);
  }
  void charge() {
    CarServer_ChargeState s = CarServer_ChargeState_init_default;
    s.has_charging_state = true;
    s.charging_state.which_type = CarServer_ChargeState_ChargingState_Charging_tag;
    component.vehicle_->charge_state_callback_(s);
  }
  void expire() { host_millis += 5001; component.run_timeouts(); }
};

static void command_outcomes() {
  for (auto outcome : {OperationOutcome::SUCCESS, OperationOutcome::SKIPPED, OperationOutcome::FAILED}) {
    Fixture f;
    f.amps.publish_state(16);
    f.limit.publish_state(70);
    f.charging.publish_state(false);
    f.heat.publish_state(false);
    f.sentry.publish_state(false);
    f.component.set_charging_state(true);
    f.complete(outcome);
    CHECK(f.component.set_charging_amps(50) == 32);
    f.complete(outcome);
    f.component.set_charging_limit(80);
    f.complete(outcome);
    f.component.set_steering_wheel_heat(true);
    f.complete(outcome);
    f.component.set_sentry_mode(true);
    f.complete(outcome);
    const bool success = outcome == OperationOutcome::SUCCESS;
    CHECK(f.charging.state == success);
    CHECK(f.heat.state == success);
    CHECK(f.sentry.state == success);
    CHECK(f.charging.published.size() == (success ? 2 : 1));
    CHECK(f.heat.published.size() == (success ? 2 : 1));
    CHECK(f.sentry.published.size() == (success ? 2 : 1));
    CHECK(f.amps.state == (success ? 32 : 16));
    CHECK(f.limit.state == (success ? 80 : 70));
    CHECK(f.amps.published.size() == 2);
    CHECK(f.limit.published.size() == 2);
    CHECK(f.last.state.find(success ? "Success" : outcome == OperationOutcome::SKIPPED ? "Skipped" : "Failed") != std::string::npos);
  }
}

static void warnings_and_pairing() {
  Fixture f;
  f.component.set_charging_limit(80);
  f.complete(OperationOutcome::FAILED);
  CHECK(f.component.status_has_warning());
  f.expire();
  CHECK(!f.component.status_has_warning());
  for (auto healthy : {OperationOutcome::SUCCESS, OperationOutcome::SKIPPED}) {
    f.component.set_charging_limit(80);
    f.complete(OperationOutcome::FAILED);
    f.component.set_charging_limit(80);
    f.complete(healthy);
    CHECK(!f.component.status_has_warning());
    f.component.status_set_warning("persistent");
    f.expire();
    CHECK(f.component.status_has_warning());
  }
  f.component.set_charging_limit(80);
  // The real library delivers failure for queued commands on disconnect.
  f.disconnect();
  CHECK(f.last.state.find("Outcome unknown") != std::string::npos);
  f.expire();
  CHECK(f.component.status_has_warning());
  f.component.vehicle_->key_not_paired_callback_();
  CHECK(f.pairing.state && f.component.status_has_warning());
  CHECK(f.last.state.find("tap a key card") != std::string::npos);
  f.expire();
  f.component.handle_command_result("poll", OperationResult::success());
  f.disconnect();
  f.connect();
  CHECK(f.pairing.state && f.component.status_has_warning());
  CHECK(f.queue().empty());
  f.limit.publish_state(70);
  f.limit.published.clear();
  f.component.set_charging_limit(80);
  CHECK(f.limit.state == 70 && f.limit.published.size() == 1);
  CHECK(f.queue().empty());
  for (auto outcome : {OperationOutcome::FAILED, OperationOutcome::SKIPPED, OperationOutcome::SUCCESS}) {
    f.component.start_pairing();
    f.complete(outcome);
    CHECK(f.pairing.state == (outcome != OperationOutcome::SUCCESS));
    CHECK(f.component.status_has_warning() == (outcome != OperationOutcome::SUCCESS));
  }
  CHECK(f.last.state == "Pair → Success");
  f.drain();
  CHECK(f.vcsec_polls == 1);
  for (int flap = 0; flap < 6; ++flap) {
    f.disconnect();
    f.connect();
    f.drain();
  }
  CHECK(f.vcsec_polls == 7);
  CHECK(f.wake_capable_polls == 0);
}

static void awake_probe_lifecycle() {
  host_millis = 700000;
  Fixture f;
  f.update();
  CHECK(f.infotainment_polls == 0);
  f.status();
  f.update();
  CHECK(f.infotainment_polls == 1);
  for (int step = 0; step < 5; ++step) {
    host_millis += 30000;
    f.status();
    f.update();
  }
  CHECK(f.infotainment_polls == 6);
  host_millis += 30000;
  f.status();
  f.update();
  CHECK(f.infotainment_polls == 6);
  f.status(true);
  f.status();
  f.update();
  CHECK(f.infotainment_polls == 6);
  host_millis = 1400000;
  f.status();
  f.update();
  CHECK(f.infotainment_polls == 7);
  f.disconnect();
  f.connect();
  f.update();
  CHECK(f.infotainment_polls == 7);
  f.status(true);
  f.update();
  CHECK(f.infotainment_polls == 7);
  f.disconnect();
  f.connect();
  f.status();
  f.update();
  CHECK(f.infotainment_polls == 7);
  CHECK(f.wake_capable_polls == 0);

  for (bool unknown : {false, true}) {
    host_millis = 1000;
    Fixture stale;
    stale.status();
    if (unknown) {
      VCSEC_VehicleStatus unavailable = VCSEC_VehicleStatus_init_default;
      stale.component.vehicle_->vehicle_status_callback_(unavailable);
    } else host_millis += 120001;
    stale.update();
    CHECK(stale.infotainment_polls == 0);
    // Cancellation must also remove the unsent one-shot: a new awake inside
    // the cooldown cannot accidentally send it later.
    stale.status();
    stale.update();
    CHECK(stale.infotainment_polls == 0);
  }
}

static void active_observations_expire() {
  for (bool sentry : {false, true}) {
    Fixture f;
    if (sentry) {
      CarServer_ClosuresState s = CarServer_ClosuresState_init_default;
      s.has_sentry_mode_state = true;
      s.sentry_mode_state.which_type = CarServer_ClosuresState_SentryModeState_Armed_tag;
      f.component.vehicle_->closures_state_callback_(s);
    } else f.charge();
    f.status();
    f.update();
    host_millis += 10000;
    f.status();
    f.update();
    CHECK(f.infotainment_polls == 2);
    host_millis += 120001;
    f.status();
    f.update();
    CHECK(f.infotainment_polls == 3);
    host_millis += 10000;
    f.status();
    f.update();
    CHECK(f.infotainment_polls == 3);
    CHECK(f.wake_capable_polls == 0);
    f.disconnect();
    CHECK(!f.component.last_charge_state_ && !f.component.last_closures_state_);
  }

  Fixture f;
  f.status();
  f.update();
  f.charge();
  host_millis += 10000;
  f.status();
  f.update();
  CHECK(f.infotainment_polls == 2);
  f.status(true);
  f.update();
  CHECK(f.infotainment_polls == 2);
  host_millis += 700000;
  f.status();
  f.update();
  CHECK(f.wake_capable_polls == 0);
}

static void charge_port_edges() {
  Fixture f;
  VCSEC_VehicleStatus closed = VCSEC_VehicleStatus_init_default;
  closed.vehicleSleepStatus = VCSEC_VehicleSleepStatus_E_VEHICLE_SLEEP_STATUS_AWAKE;
  closed.has_closureStatuses = true;
  auto open = closed;
  open.closureStatuses.chargePort = VCSEC_ClosureState_E_CLOSURESTATE_OPEN;
  auto policy = [&]() { return f.component.poll_policy_.update(host_millis, false, false, false).wake_policy; };
  f.component.handle_vehicle_status(open);
  CHECK(policy() == WakePolicy::NO_WAKE_SKIP);
  f.component.handle_vehicle_status(closed);
  f.component.handle_vehicle_status(open);
  CHECK(policy() == WakePolicy::WAKE_IF_NEEDED);
  host_millis += 660001;
  f.component.handle_vehicle_status(open);
  CHECK(policy() == WakePolicy::NO_WAKE_SKIP);
  f.component.handle_vehicle_status(closed);
  open.vehicleSleepStatus = VCSEC_VehicleSleepStatus_E_VEHICLE_SLEEP_STATUS_ASLEEP;
  f.component.handle_vehicle_status(open);
  CHECK(policy() == WakePolicy::NO_WAKE_SKIP);
  f.disconnect();
  f.connect();
  open.vehicleSleepStatus = VCSEC_VehicleSleepStatus_E_VEHICLE_SLEEP_STATUS_AWAKE;
  f.component.handle_vehicle_status(open);
  CHECK(policy() == WakePolicy::NO_WAKE_SKIP);
}

int main() {
  command_outcomes();
  warnings_and_pairing();
  awake_probe_lifecycle();
  active_observations_expire();
  charge_port_edges();
  return test_summary();
}
