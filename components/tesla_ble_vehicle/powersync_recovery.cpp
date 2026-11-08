// SPDX-License-Identifier: AGPL-3.0-only
// Optional PowerSync operational recovery adapter for the yoziru component.
#include "tesla_ble_vehicle.h"
#include <esphome/core/application.h>
#include <esphome/components/json/json_util.h>
#include <esp_random.h>
#include <nvs.h>

namespace esphome { namespace tesla_ble_vehicle {

void TeslaBLEVehicle::setup_stop_recovery_() {
  if (recovery_proof_ == nullptr) return;
  char boot[17];
  snprintf(boot, sizeof(boot), "%08x%08x", unsigned(esp_random()), unsigned(esp_random()));
  StopRecoveryStore stored;
  size_t size = sizeof(stored);
  nvs_handle_t handle;
  if (nvs_open("powersync_r2", NVS_READWRITE, &handle) != ESP_OK) {
    stored.magic = 0;
  } else {
    auto result = nvs_get_blob(handle, "recovery", &stored, &size);
    if (result != ESP_ERR_NVS_NOT_FOUND && (result != ESP_OK || size != sizeof(stored))) stored.magic = 0;
    nvs_close(handle);
  }
  stop_recovery_.load(stored, boot);
  if (stop_recovery_.locked() && vin_ != stop_recovery_.saved.vin) stop_recovery_.held();
  publish_stop_recovery_();
}

bool TeslaBLEVehicle::persist_stop_recovery_() {
  nvs_handle_t handle;
  if (nvs_open("powersync_r2", NVS_READWRITE, &handle) != ESP_OK) return false;
  bool saved = nvs_set_blob(handle, "recovery", &stop_recovery_.saved, sizeof(stop_recovery_.saved)) == ESP_OK
      && nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return saved;
}

void TeslaBLEVehicle::publish_stop_recovery_() {
  if (recovery_proof_ == nullptr) return;
  const auto &p = stop_recovery_;
  auto value = json::build_json([&](JsonObject root) {
    root["v"] = 2; root["b"] = p.boot; root["o"] = p.saved.previous_boot;
    root["n"] = p.saved.nonce; root["g"] = p.saved.generation;
    root["m"] = std::string(1, p.phase); root["i"] = vin_;
    root["s"] = p.sequence; root["t"] = p.sample_ms;
    if (p.zero_samples == 2 && (p.phase == 'v' || p.phase == 'r')) {
      root["a"] = 0; root["w"] = 0; root["c"] = std::string(1, p.stopped_state);
    }
  });
  if (value.size() <= 255) recovery_proof_->publish_state(value);
}

void TeslaBLEVehicle::request_stop_recovery(const std::string &request) {
  if (recovery_proof_ == nullptr || vin_.size() != 17) return;
  auto colon = request.find(':');
  if (colon == std::string::npos) return;
  const auto action = request.substr(0, colon), nonce = request.substr(colon+1);
  if (!recovery_hex(nonce, 32)) return;
  if (action == "prepare") {
    if (!stop_recovery_.prepare(nonce)) return;
    vin_.copy(stop_recovery_.saved.vin, 17); stop_recovery_.saved.vin[17] = 0;
    if (!persist_stop_recovery_()) {
      stop_recovery_.held(); publish_stop_recovery_(); return;
    }
    publish_stop_recovery_();
    // No old library retries or fragment writes may progress after prepare.
    // The new process loads the durable read-only hold before controls run.
    this->set_timeout("ps_recovery_reboot", 100, []() { App.safe_reboot(); });
  } else if (nonce == stop_recovery_.saved.nonce && action == "commit"
             && stop_recovery_.can_release(nonce, millis())) {
    stop_recovery_.saved.locked = false;
    if (!persist_stop_recovery_()) {
      stop_recovery_.saved.locked = true;
      stop_recovery_.held(); publish_stop_recovery_(); return;
    }
    stop_recovery_.released(); publish_stop_recovery_();
  } else if (nonce == stop_recovery_.saved.nonce && action == "hold") {
    stop_recovery_.saved.locked = true;
    stop_recovery_.held(); persist_stop_recovery_(); publish_stop_recovery_();
  }
}

void TeslaBLEVehicle::poll_stop_recovery_() {
  if (!recovery_held() || !stop_recovery_.reading() || !vehicle_ || !is_connected()
      || key_rejected_ || vehicle_->pending_command_count() || millis()-recovery_last_poll_ < 2000) return;
  recovery_last_poll_ = millis();
  // This is the only new work allowed while locked. The library correlates
  // the native charge response to the actual request UUID before publishing
  // the separate recovery callback. Explicit operator recovery may wake.
  vehicle_->charge_state_poll(TeslaBLE::WakePolicy::WAKE_IF_NEEDED);
}

void TeslaBLEVehicle::read_stop_recovery_sample_(const CarServer_ChargeState &charge, const std::string &uuid) {
  if (!stop_recovery_.reading() || uuid == recovery_last_uuid_ || !stop_recovery_.expect(uuid)) return;
  recovery_last_uuid_ = uuid;
  bool complete = charge.has_charging_state && charge.which_optional_charger_actual_current
      && charge.which_optional_charger_power;
  char stopped = '?';
  float current = NAN, power = NAN;
  if (complete) {
    current = charge.optional_charger_actual_current.charger_actual_current;
    power = charge.optional_charger_power.charger_power;
    switch (charge.charging_state.which_type) {
      case CarServer_ChargeState_ChargingState_Stopped_tag: stopped = 'S'; break;
      case CarServer_ChargeState_ChargingState_Complete_tag: stopped = 'C'; break;
      case CarServer_ChargeState_ChargingState_Disconnected_tag: stopped = 'D'; break;
      case CarServer_ChargeState_ChargingState_NoPower_tag: stopped = 'N'; break;
      default: break;
    }
  }
  if (stop_recovery_.sample(uuid, millis(), complete, stopped, current, power)) publish_stop_recovery_();
}

}} // namespace esphome::tesla_ble_vehicle
