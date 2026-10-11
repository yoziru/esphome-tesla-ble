#include "tesla_ble_vehicle.h"
#include <client.h>
#include <cinttypes>
#include <cstring>
#include <defs.h>
#include <esp_log.h>
#include <esphome/core/helpers.h>
#include <tb_utils.h>

namespace esphome {
namespace tesla_ble_vehicle {

void tesla_ble_log_callback(TeslaBLE::LogLevel level, const char *tag, int line,
                            const char *format, va_list args) {
  if (tag == nullptr)
    tag = "TeslaBLE";
  if (format == nullptr)
    return;

  int esphome_level;
  switch (level) {
  case TeslaBLE::LogLevel::ERROR:
    esphome_level = ESPHOME_LOG_LEVEL_ERROR;
    break;
  case TeslaBLE::LogLevel::WARN:
    esphome_level = ESPHOME_LOG_LEVEL_WARN;
    break;
  case TeslaBLE::LogLevel::INFO:
    esphome_level = ESPHOME_LOG_LEVEL_INFO;
    break;
  case TeslaBLE::LogLevel::DEBUG:
    esphome_level = ESPHOME_LOG_LEVEL_DEBUG;
    break;
  case TeslaBLE::LogLevel::VERBOSE:
    esphome_level = ESPHOME_LOG_LEVEL_VERBOSE;
    break;
  default:
    return;
  }
  esp_log_vprintf_(esphome_level, tag, line, format, args);
}

TeslaBLEVehicle::TeslaBLEVehicle() : vin_(""), role_("DRIVER") {
  ESP_LOGCONFIG(TAG, "Constructing Tesla BLE Vehicle component");
}

void TeslaBLEVehicle::setup() {
  ESP_LOGCONFIG(TAG, "Setting up TeslaBLEVehicle");
  service_uuid_ = espbt::ESPBTUUID::from_raw(SERVICE_UUID);
  read_uuid_ = espbt::ESPBTUUID::from_raw(READ_UUID);
  write_uuid_ = espbt::ESPBTUUID::from_raw(WRITE_UUID);
  initialize_managers();
  restore_charging_amps_max_();

  if (vin_.empty()) {
    ESP_LOGE(TAG, "VIN not configured - component will not function properly");
    this->status_set_warning("VIN not configured");
    return;
  }

  vehicle_->set_vin(vin_);
}

void TeslaBLEVehicle::initialize_managers() {
  ESP_LOGD(TAG, "Initializing components...");

  ble_adapter_ = std::make_shared<BleAdapterImpl>(this);
  storage_adapter_ = std::make_shared<StorageAdapterImpl>();

  if (!storage_adapter_->initialize()) {
    ESP_LOGE(TAG, "Failed to initialize storage adapter");
  }

  TeslaBLE::set_log_callback(tesla_ble_log_callback);
  vehicle_ =
      std::make_shared<TeslaBLE::Vehicle>(ble_adapter_, storage_adapter_);

  ESP_LOGD(TAG, "Wiring up callbacks...");

  vehicle_->set_raw_message_callback([this](const std::vector<uint8_t> &data) {
    std::string hex = TeslaBLE::format_hex(data.data(), data.size());
    if (hex != last_rx_hex_) {
      ESP_LOGV(TAG, "BLE RX: %s", hex.c_str());
      last_rx_hex_ = hex;
    }
  });

  vehicle_->set_vehicle_status_callback([this](const VCSEC_VehicleStatus &s) {
    state_manager_.update_vehicle_status(s);
  });

  vehicle_->set_charge_state_callback([this](const CarServer_ChargeState &s) {
    state_manager_.update_charge_state(s);
  });

  vehicle_->set_climate_state_callback([this](const CarServer_ClimateState &s) {
    state_manager_.update_climate_state(s);
  });

  vehicle_->set_drive_state_callback([this](const CarServer_DriveState &s) {
    state_manager_.update_drive_state(s);
  });

  vehicle_->set_tire_pressure_state_callback(
      [this](const CarServer_TirePressureState &s) {
        state_manager_.update_tire_pressure_state(s);
      });

  vehicle_->set_closures_state_callback(
      [this](const CarServer_ClosuresState &s) {
        state_manager_.update_closures_state(s);
      });

  vehicle_->set_software_update_state_callback(
      [this](const CarServer_SoftwareUpdateState &s, const std::string &version) {
        state_manager_.update_software_update_state(s, version);
      });

  ESP_LOGD(TAG, "All components initialized");
}

void TeslaBLEVehicle::loop() {
  if (vehicle_)
    vehicle_->loop();
  if (ble_adapter_)
    ble_adapter_->process_write_queue();

  // Detect wedged links. Two flavours:
  //  - GATT established but the Vehicle reports disconnected (e.g. after the
  //    library's auth-stuck watchdog reset session state)
  //  - setup stalled in CONNECTED: service discovery or notify registration
  //    failed (gattc_event_handler logs-and-breaks), so ESTABLISHED is never
  //    reached and nothing else ever retries
  // Only a fresh connect cycle re-runs discovery / notify registration, so
  // force one instead of waiting for a reboot.
  const bool gatt_established = is_connected();
  const bool stalled_setup = this->node_state == espbt::ClientState::CONNECTED;
  const bool vehicle_connected = vehicle_ != nullptr && vehicle_->is_connected();
  if (connection_reset_policy_.should_force_reconnect(millis(), gatt_established || stalled_setup,
                                                      vehicle_connected)) {
    ESP_LOGW(TAG, "GATT connection up but vehicle is disconnected - forcing reconnect");
    connection_reset_policy_.on_force_reconnect(millis());
    this->parent()->disconnect();
  }
}

void TeslaBLEVehicle::update() {
  if (!is_connected() || !vehicle_)
    return;

  uint32_t now = millis();

  // VCSEC Polling
  if (now - last_vcsec_poll_ >= vcsec_poll_interval_) {
    ESP_LOGI(TAG, "Polling VCSEC");
    vehicle_->vcsec_poll();
    last_vcsec_poll_ = now;
  }

  // Infotainment Polling - use faster interval when vehicle is active
  const bool is_asleep = state_manager_.is_asleep();
  // Only sentry mode and active charging warrant keeping the car awake. A car
  // that is merely left unlocked, or reports user presence because a phone is
  // in range, can still fall asleep on its own and must be allowed to do so
  // (issues #201/#202). To add faster polling for unlocked/user-present later,
  // extend the decision inputs here (and gate it behind an opt-in config).
  TeslaBLE::InfotainmentPollDecision decision =
      poll_policy_.update(now, is_asleep, state_manager_.is_charging(),
                          state_manager_.is_sentry_mode());

  if (poll_policy_.should_poll(now, decision.interval_ms)) {
    ESP_LOGI(TAG, "Polling Infotainment (%s)",
             decision.wake_policy == TeslaBLE::WakePolicy::NO_WAKE_SKIP
                 ? "sleeping - NO_WAKE_SKIP"
                 : "active - WAKE_IF_NEEDED");
    vehicle_->infotainment_poll(decision.wake_policy);
    poll_policy_.on_poll(now);
  }
}

void TeslaBLEVehicle::dump_config() {
  ESP_LOGCONFIG(TAG, "Tesla BLE Vehicle:");
  ESP_LOGCONFIG(TAG, "  VIN: %s", vin_.empty() ? "Not set" : vin_.c_str());
  ESP_LOGCONFIG(TAG, "  Role: %s", role_.c_str());
  ESP_LOGCONFIG(TAG, "  Max Charging Amps: %d",
                state_manager_.get_charging_amps_max());
  ESP_LOGCONFIG(TAG, "  Polling: VCSEC=%" PRIu32 "ms, Awake=%" PRIu32
                     "ms, Active=%" PRIu32 "ms",
                 vcsec_poll_interval_, poll_policy_.awake_interval_ms(),
                 poll_policy_.active_interval_ms());
}

// =============================================================================
// Configuration setters
// =============================================================================

void TeslaBLEVehicle::set_vin(const char *vin) {
  if (vin == nullptr) {
    ESP_LOGW(TAG, "Attempted to set null VIN - ignoring");
    return;
  }

  vin_ = vin;
  ESP_LOGD(TAG, "VIN set to: %s", vin_.c_str());

  if (vehicle_) {
    vehicle_->set_vin(vin_);
  }
}

void TeslaBLEVehicle::set_role(const std::string &role) {
  ESP_LOGD(TAG, "Setting role: %s", role.c_str());
  role_ = role;
}

void TeslaBLEVehicle::set_charging_amps_max(int amps_max) {
  ESP_LOGD(TAG, "Setting charging amps max: %d", amps_max);

  if (amps_max <= 0) {
    ESP_LOGW(TAG, "Invalid charging amps max value: %d - ignoring", amps_max);
    return;
  }

  configured_charging_amps_max_ = amps_max;

  state_manager_.set_charging_amps_max(amps_max);
}

void TeslaBLEVehicle::restore_charging_amps_max_() {
  auto pref = global_preferences->make_preference<int32_t>(charging_amps_max_pref_hash_());
  int32_t stored = 0;
  if (pref.load(&stored) && stored > 0 && stored <= 80) {
    ESP_LOGI(TAG, "Restored charging amps max from NVS: %" PRId32 " A", stored);
    state_manager_.set_charging_amps_max(stored);
    return;
  }
  state_manager_.set_charging_amps_max(configured_charging_amps_max_);
}

uint32_t TeslaBLEVehicle::charging_amps_max_pref_hash_() const {
  return fnv1_hash_extend(fnv1_hash("tesla_ble_vehicle.charging_amps_max"), vin_);
}

void TeslaBLEVehicle::save_charging_amps_max_(int max) {
  if (max <= 0 || max > 80) return;
  auto pref = global_preferences->make_preference<int32_t>(charging_amps_max_pref_hash_());
  const int32_t value = max;
  if (pref.save(&value)) {
    ESP_LOGD(TAG, "Persisted charging amps max %d A", max);
  }
}

void TeslaBLEVehicle::set_vcsec_poll_interval(uint32_t interval_ms) {
  ESP_LOGD(TAG, "Setting VCSEC poll interval: %" PRIu32 " ms", interval_ms);
  vcsec_poll_interval_ = interval_ms;
}

void TeslaBLEVehicle::set_infotainment_poll_interval_awake(
    uint32_t interval_ms) {
  ESP_LOGD(TAG, "Setting infotainment poll interval awake: %" PRIu32 " ms",
           interval_ms);
  poll_policy_.set_awake_interval_ms(interval_ms);
}

void TeslaBLEVehicle::set_infotainment_poll_interval_active(
    uint32_t interval_ms) {
  ESP_LOGD(TAG, "Setting infotainment poll interval active: %" PRIu32 " ms",
            interval_ms);
  poll_policy_.set_active_interval_ms(interval_ms);
}

void TeslaBLEVehicle::set_infotainment_sleep_timeout(uint32_t interval_ms) {
  ESP_LOGD(TAG, "Setting infotainment sleep timeout: %" PRIu32 " ms",
           interval_ms);
  poll_policy_.set_sleep_timeout_ms(interval_ms);
}

// =============================================================================
// Generic sensor setters
// =============================================================================

void TeslaBLEVehicle::set_binary_sensor(const std::string &id,
                                        binary_sensor::BinarySensor *sensor) {
  state_manager_.set_binary_sensor(id, sensor);
}

void TeslaBLEVehicle::set_sensor(const std::string &id,
                                 sensor::Sensor *sensor) {
  state_manager_.set_sensor(id, sensor);
}

void TeslaBLEVehicle::set_text_sensor(const std::string &id,
                                      text_sensor::TextSensor *sensor) {
  state_manager_.set_text_sensor(id, sensor);
}

// =============================================================================
// Control setters
// =============================================================================

void TeslaBLEVehicle::set_charging_switch(switch_::Switch *sw) {
  state_manager_.set_charging_switch(sw);
}

void TeslaBLEVehicle::set_steering_wheel_heat_switch(switch_::Switch *sw) {
  state_manager_.set_steering_wheel_heat_switch(sw);
}

void TeslaBLEVehicle::set_sentry_mode_switch(switch_::Switch *sw) {
  state_manager_.set_sentry_mode_switch(sw);
}

void TeslaBLEVehicle::set_charging_amps_number(number::Number *number) {
  state_manager_.set_charging_amps_number(number);
}

void TeslaBLEVehicle::set_charging_limit_number(number::Number *number) {
  state_manager_.set_charging_limit_number(number);
}

// =============================================================================
// Lock, Cover, and Climate setters
// =============================================================================

void TeslaBLEVehicle::set_doors_lock(lock::Lock *lck) {
  state_manager_.set_doors_lock(lck);
}

void TeslaBLEVehicle::set_charge_port_latch_lock(lock::Lock *lck) {
  state_manager_.set_charge_port_latch_lock(lck);
}

void TeslaBLEVehicle::set_trunk_cover(cover::Cover *cvr) {
  state_manager_.set_trunk_cover(cvr);
}

void TeslaBLEVehicle::set_frunk_cover(cover::Cover *cvr) {
  state_manager_.set_frunk_cover(cvr);
}

void TeslaBLEVehicle::set_windows_cover(cover::Cover *cvr) {
  state_manager_.set_windows_cover(cvr);
}

void TeslaBLEVehicle::set_charge_port_door_cover(cover::Cover *cvr) {
  state_manager_.set_charge_port_door_cover(cvr);
}

void TeslaBLEVehicle::set_sunroof_cover(cover::Cover *cvr) {
  state_manager_.set_sunroof_cover(cvr);
}

void TeslaBLEVehicle::set_seat_select(int seat, select::Select *sel) {
  if (seat < 0 || seat >= 16) {
    ESP_LOGW(TAG, "Ignoring seat select for invalid seat %d", seat);
    return;
  }
  state_manager_.set_seat_select(seat, sel);
}

void TeslaBLEVehicle::set_cabin_protection_select(select::Select *sel) {
  state_manager_.set_cabin_protection_select(sel);
}

void TeslaBLEVehicle::set_cabin_temperature_select(select::Select *sel) {
  state_manager_.set_cabin_temperature_select(sel);
}

void TeslaBLEVehicle::set_climate(climate::Climate *clm) {
  state_manager_.set_climate(clm);
}

// =============================================================================
// Button setters
// =============================================================================

void TeslaBLEVehicle::set_wake_button(button::Button *button) {
  TeslaWakeButton *wake_button = static_cast<TeslaWakeButton *>(button);
  if (wake_button)
    wake_button->set_parent(this);
}

void TeslaBLEVehicle::set_pair_button(button::Button *button) {
  TeslaPairButton *pair_button = static_cast<TeslaPairButton *>(button);
  if (pair_button)
    pair_button->set_parent(this);
}

void TeslaBLEVehicle::set_regenerate_key_button(button::Button *button) {
  TeslaRegenerateKeyButton *regen_button =
      static_cast<TeslaRegenerateKeyButton *>(button);
  if (regen_button)
    regen_button->set_parent(this);
}

void TeslaBLEVehicle::set_force_update_button(button::Button *button) {
  TeslaForceUpdateButton *update_button =
      static_cast<TeslaForceUpdateButton *>(button);
  if (update_button)
    update_button->set_parent(this);
}

// =============================================================================
// Command tracking (v5.1.0 OperationResult + phase callbacks)
// =============================================================================

void TeslaBLEVehicle::handle_command_result(const std::string &name,
                                            TeslaBLE::OperationResult result) {
  std::string value = name;

  if (result.is_success()) {
    value += " → Success";
  } else if (result.is_skipped()) {
    value += " → Skipped";
  } else {
    value += result.error() && result.error()->may_have_succeeded() ? " → Outcome unknown" : " → Failed";
    if (result.error()) {
      value += ": ";
      value += result.error()->message();
    }
    ESP_LOGW(TAG, "Command failed: %s", value.c_str());
  }
  if (result.is_failure()) {
    this->status_momentary_warning("command-failed-warning");
  } else {
    this->cancel_timeout("command-failed-warning");
    this->status_clear_warning();
  }

  if (last_command_sensor_)
    last_command_sensor_->publish_state(value);
}

void TeslaBLEVehicle::track(const std::string &name,
                            std::function<void(TeslaBLE::Command::OperationResultCallback)> send,
                            std::function<void(bool)> on_result) {
  if (!vehicle_) {
    ESP_LOGE(TAG, "Cannot send command '%s': vehicle not initialized", name.c_str());
    return;
  }

  send([this, name, on_result = std::move(on_result)](TeslaBLE::OperationResult result) {
    const bool succeeded = result.is_success();
    handle_command_result(name, std::move(result));
    if (on_result) on_result(succeeded);
  });
}

// =============================================================================
// Public vehicle actions
// =============================================================================

int TeslaBLEVehicle::wake_vehicle() {
  ESP_LOGD(TAG, "Wake vehicle requested");

  if (!vehicle_)
    return -1;

  if (!state_manager_.is_asleep()) {
    ESP_LOGI(TAG, "Vehicle already awake - sending VCSEC poll instead");
    track("VCSEC Poll", [this](auto cb) { vehicle_->vcsec_poll(TeslaBLE::WakePolicy::NO_WAKE_SKIP, std::move(cb)); });
    return 0;
  }

  ESP_LOGI(TAG, "Sending wake command");
  track("Wake", [this](auto cb) { vehicle_->wake(std::move(cb)); });
  return 0;
}

int TeslaBLEVehicle::start_pairing() {
  ESP_LOGI(TAG, "Pairing requested");

  if (!vehicle_) {
    ESP_LOGE(TAG, "Vehicle instance not available");
    return -1;
  }

  Keys_Role role_enum = Keys_Role_ROLE_OWNER;
  if (role_ == "DRIVER") {
    role_enum = Keys_Role_ROLE_DRIVER;
  } else if (role_ == "CHARGING_MANAGER") {
    role_enum = Keys_Role_ROLE_CHARGING_MANAGER;
  }

  vehicle_->pair(role_enum);
  return 0;
}

int TeslaBLEVehicle::regenerate_key() {
  ESP_LOGI(TAG, "Key regeneration requested");

  if (!vehicle_) {
    ESP_LOGE(TAG, "Vehicle instance not available");
    return -1;
  }

  vehicle_->regenerate_key();
  return 0;
}

void TeslaBLEVehicle::force_update() {
  uint32_t now = millis();
  if (!poll_policy_.should_poll(now, poll_policy_.active_interval_ms())) {
    ESP_LOGD(TAG, "Force update requested too soon (within active polling "
                  "interval) - ignoring");
    return;
  }

  ESP_LOGI(TAG, "Force update requested");
  poll_policy_.on_poll(now);

  if (vehicle_) {
    vehicle_->vcsec_poll();
    vehicle_->infotainment_poll(TeslaBLE::WakePolicy::WAKE_IF_NEEDED);
    vehicle_->software_update_poll(TeslaBLE::WakePolicy::WAKE_IF_NEEDED);
  }
}

int TeslaBLEVehicle::set_charging_state(bool charging) {
  ESP_LOGI(TAG, "Set charging state: %s", charging ? "ON" : "OFF");

  if (!vehicle_) {
    ESP_LOGE(TAG, "Vehicle instance not available");
    return -1;
  }

  track(
      charging ? "Start Charging" : "Stop Charging",
      [this, charging](auto cb) { vehicle_->set_charging_state(charging, std::move(cb)); },
      [this, charging](bool succeeded) {
        if (succeeded) state_manager_.update_charging_control_state(charging);
      });
  return 0;
}

int TeslaBLEVehicle::set_charging_amps(int amps) {
  ESP_LOGI(TAG, "Set charging amps: %d", amps);

  if (amps < 0) {
    ESP_LOGW(TAG, "Invalid charging amps: %d", amps);
    return 0;
  }

  int max_amps = state_manager_.get_charging_amps_max();
  if (amps > max_amps) {
    ESP_LOGW(TAG, "Requested amps (%d) exceeds maximum (%d), clamping", amps,
             max_amps);
    amps = max_amps;
  }

  const int clamped = amps;
  track("Set Charging Amps",
        [this, clamped](auto cb) { vehicle_->set_charging_amps(clamped, std::move(cb)); },
        [this, clamped](bool succeeded) {
          if (succeeded) state_manager_.update_charging_amps(static_cast<float>(clamped));
          else state_manager_.republish_charging_amps();
        });
  return clamped;
}

int TeslaBLEVehicle::set_charging_limit(int limit) {
  ESP_LOGI(TAG, "Set charging limit: %d%%", limit);

  if (limit < MIN_CHARGING_LIMIT || limit > MAX_CHARGING_LIMIT) {
    ESP_LOGW(TAG, "Invalid charging limit: %d%%", limit);
    return -1;
  }

  track("Set Charging Limit",
        [this, limit](auto cb) { vehicle_->set_charging_limit(limit, std::move(cb)); },
        [this, limit](bool succeeded) {
          if (succeeded) state_manager_.update_charging_limit(static_cast<float>(limit));
          else state_manager_.republish_charging_limit();
        });
  return 0;
}

// =============================================================================
// Closure controls (VCSEC)
// =============================================================================

void TeslaBLEVehicle::lock_vehicle() {
  ESP_LOGI(TAG, "Lock vehicle requested");
  track("Lock", [this](auto cb) { vehicle_->lock(std::move(cb)); });
}

void TeslaBLEVehicle::unlock_vehicle() {
  ESP_LOGI(TAG, "Unlock vehicle requested");
  track("Unlock", [this](auto cb) { vehicle_->unlock(std::move(cb)); });
}

void TeslaBLEVehicle::open_trunk() {
  ESP_LOGI(TAG, "Open trunk requested");
  track("Open Trunk", [this](auto cb) { vehicle_->open_trunk(std::move(cb)); });
}

void TeslaBLEVehicle::close_trunk() {
  ESP_LOGI(TAG, "Close trunk requested");
  track("Close Trunk", [this](auto cb) { vehicle_->close_trunk(std::move(cb)); });
}

void TeslaBLEVehicle::open_frunk() {
  ESP_LOGI(TAG, "Open frunk requested");
  track("Open Frunk", [this](auto cb) { vehicle_->open_frunk(std::move(cb)); });
}

void TeslaBLEVehicle::open_charge_port() {
  ESP_LOGI(TAG, "Open charge port requested");
  track("Open Charge Port", [this](auto cb) { vehicle_->open_charge_port(std::move(cb)); });
}

void TeslaBLEVehicle::close_charge_port() {
  ESP_LOGI(TAG, "Close charge port requested");
  track("Close Charge Port", [this](auto cb) { vehicle_->close_charge_port(std::move(cb)); });
}

void TeslaBLEVehicle::unlock_charge_port() {
  ESP_LOGI(TAG, "Unlock charge port latch requested");
  track("Unlock Charge Port", [this](auto cb) { vehicle_->unlock_charge_port(std::move(cb)); });
}

void TeslaBLEVehicle::unlatch_driver_door() {
  ESP_LOGI(TAG, "Unlatch driver door requested");
  track("Unlatch Driver Door", [this](auto cb) { vehicle_->unlatch_driver_door(std::move(cb)); });
}

// =============================================================================
// HVAC controls (Infotainment)
// =============================================================================

void TeslaBLEVehicle::set_climate_on(bool enable) {
  ESP_LOGI(TAG, "Climate %s requested", enable ? "ON" : "OFF");
  track(enable ? "Climate On" : "Climate Off",
        [this, enable](auto cb) { vehicle_->set_climate(enable, std::move(cb)); });
}

void TeslaBLEVehicle::set_climate_temp(float temp) {
  ESP_LOGI(TAG, "Climate temperature %.1f°C requested", temp);
  track("Set Climate Temp", [this, temp](auto cb) { vehicle_->set_climate_temp(temp, std::move(cb)); });
}

void TeslaBLEVehicle::set_climate_keeper(int mode) {
  const char *mode_names[] = {"Off", "On", "Dog", "Camp"};
  ESP_LOGI(TAG, "Climate keeper %s requested",
           (mode >= 0 && mode <= 3) ? mode_names[mode] : "Unknown");
  std::string name = std::string("Climate Keeper ") +
                     ((mode >= 0 && mode <= 3) ? mode_names[mode] : "Unknown");
  track(name, [this, mode](auto cb) { vehicle_->set_climate_keeper(mode, std::move(cb)); });
}

void TeslaBLEVehicle::set_bioweapon_mode(bool enable) {
  ESP_LOGI(TAG, "Bioweapon mode %s requested", enable ? "ON" : "OFF");
  track(enable ? "Bioweapon On" : "Bioweapon Off",
        [this, enable](auto cb) { vehicle_->set_bioweapon_mode(enable, std::move(cb)); });
}

void TeslaBLEVehicle::set_preconditioning_max(bool enable) {
  ESP_LOGI(TAG, "Preconditioning max (defrost) %s requested",
           enable ? "ON" : "OFF");
  track(enable ? "Defrost On" : "Defrost Off",
        [this, enable](auto cb) { vehicle_->set_preconditioning_max(enable, std::move(cb)); });
}

void TeslaBLEVehicle::set_steering_wheel_heat(bool enable) {
  ESP_LOGI(TAG, "Steering wheel heat %s requested", enable ? "ON" : "OFF");
  track(enable ? "Steering Heat On" : "Steering Heat Off",
        [this, enable](auto cb) { vehicle_->set_steering_wheel_heat(enable, std::move(cb)); },
        [this, enable](bool succeeded) {
          if (succeeded) state_manager_.update_steering_wheel_heat(enable);
        });
}

void TeslaBLEVehicle::set_seat_climate(int seat, int level, bool cooling) {
  if (!vehicle_)
    return;

  const auto position = static_cast<TeslaBLE::SeatPosition>(seat);
  const bool cooling_supported = state_manager_.seat_cool_level(seat) >= 0;
  track("Set Seat Climate", [this, position, level, cooling, cooling_supported](auto cb) {
    vehicle_->set_seat_climate(position, level, cooling, cooling_supported, std::move(cb));
  });
}

void TeslaBLEVehicle::set_seat_auto(int seat, bool enable) {
  if (!vehicle_)
    return;
  if (!state_manager_.seat_auto_supported(seat)) {
    ESP_LOGW(TAG, "Seat Auto support has not been observed for seat %d", seat);
    return;
  }

  const auto position = static_cast<TeslaBLE::SeatPosition>(seat);
  ESP_LOGI(TAG, "Seat %d auto climate %s requested", seat, enable ? "ON" : "OFF");
  track(enable ? "Seat Auto On" : "Seat Auto Off",
        [this, position, enable](auto cb) { vehicle_->set_auto_seat_climate(position, enable, std::move(cb)); });
}

void TeslaBLEVehicle::set_cabin_overheat_protection(int mode) {
  if (!vehicle_)
    return;

  ESP_LOGI(TAG, "Cabin overheat protection mode %d requested", mode);
  track("Cabin Overheat Protection",
        [this, mode](auto cb) { vehicle_->set_cabin_overheat_protection(mode, std::move(cb)); });
}

void TeslaBLEVehicle::set_cabin_overheat_temperature(int level) {
  if (!vehicle_)
    return;

  ESP_LOGI(TAG, "Cabin overheat protection temperature level %d requested", level);
  track("Cabin Overheat Temp",
        [this, level](auto cb) { vehicle_->set_cabin_overheat_protection_temp(level, std::move(cb)); });
}

void TeslaBLEVehicle::set_sunroof_position(int percent) {
  if (!vehicle_)
    return;

  ESP_LOGI(TAG, "Sunroof position %d%% requested", percent);
  track("Set Sunroof Position",
        [this, percent](auto cb) { vehicle_->set_sunroof_position(percent, std::move(cb)); });
}

// =============================================================================
// Vehicle controls (Infotainment)
// =============================================================================

void TeslaBLEVehicle::flash_lights() {
  ESP_LOGI(TAG, "Flash lights requested");
  track("Flash Lights", [this](auto cb) { vehicle_->flash_lights(std::move(cb)); });
}

void TeslaBLEVehicle::honk_horn() {
  ESP_LOGI(TAG, "Honk horn requested");
  track("Honk Horn", [this](auto cb) { vehicle_->honk_horn(std::move(cb)); });
}

void TeslaBLEVehicle::install_software_update() {
  ESP_LOGI(TAG, "Install software update requested");
  track("Schedule Software Update", [this](auto cb) { vehicle_->schedule_software_update(0, std::move(cb)); });
}

void TeslaBLEVehicle::cancel_software_update() {
  ESP_LOGI(TAG, "Cancel software update requested");
  track("Cancel Software Update", [this](auto cb) { vehicle_->cancel_software_update(std::move(cb)); });
}

void TeslaBLEVehicle::set_sentry_mode(bool enable) {
  ESP_LOGI(TAG, "Sentry mode %s requested", enable ? "ON" : "OFF");
  track(enable ? "Sentry On" : "Sentry Off",
        [this, enable](auto cb) { vehicle_->set_sentry_mode(enable, std::move(cb)); },
        [this, enable](bool succeeded) {
          if (succeeded) state_manager_.update_sentry_mode(enable);
        });
}

void TeslaBLEVehicle::vent_windows() {
  ESP_LOGI(TAG, "Vent windows requested");
  track("Vent Windows", [this](auto cb) { vehicle_->vent_windows(std::move(cb)); });
}

void TeslaBLEVehicle::close_windows() {
  ESP_LOGI(TAG, "Close windows requested");
  track("Close Windows", [this](auto cb) { vehicle_->close_windows(std::move(cb)); });
}

// =============================================================================
// BLE event handling
// =============================================================================

void TeslaBLEVehicle::gattc_event_handler(esp_gattc_cb_event_t event,
                                          esp_gatt_if_t gattc_if,
                                          esp_ble_gattc_cb_param_t *param) {
  ESP_LOGV(TAG, "GATTC event %d", event);

  switch (event) {
  case ESP_GATTC_OPEN_EVT:
    if (param->open.status == ESP_GATT_OK) {
      ESP_LOGI(TAG, "BLE physical link established");
    }
    break;

  case ESP_GATTC_CLOSE_EVT:
    ESP_LOGW(TAG, "BLE connection closed");
    handle_connection_lost();
    break;

  case ESP_GATTC_DISCONNECT_EVT:
    ESP_LOGW(TAG, "BLE disconnected");
    this->read_handle_ = 0;
    this->write_handle_ = 0;
    this->node_state = espbt::ClientState::DISCONNECTING;
    break;

  case ESP_GATTC_SEARCH_CMPL_EVT: {
    auto *readChar = this->parent()->get_characteristic(this->service_uuid_,
                                                        this->read_uuid_);
    if (readChar == nullptr) {
      ESP_LOGE(TAG, "Read characteristic not found");
      break;
    }
    this->read_handle_ = readChar->handle;

    auto reg_status = esp_ble_gattc_register_for_notify(
        this->parent()->get_gattc_if(), this->parent()->get_remote_bda(),
        readChar->handle);
    if (reg_status) {
      ESP_LOGE(TAG, "Failed to register for notifications: %d", reg_status);
    }

    auto *writeChar = this->parent()->get_characteristic(this->service_uuid_,
                                                         this->write_uuid_);
    if (writeChar == nullptr) {
      ESP_LOGE(TAG, "Write characteristic not found");
      break;
    }
    this->write_handle_ = writeChar->handle;
    break;
  }

  case ESP_GATTC_REG_FOR_NOTIFY_EVT:
    if (param->reg_for_notify.handle != this->read_handle_)
      break;
    if (param->reg_for_notify.status != ESP_GATT_OK) {
      ESP_LOGE(TAG, "Failed to register for notifications");
      break;
    }

    this->node_state = espbt::ClientState::ESTABLISHED;
    ESP_LOGI(TAG, "BLE connection fully established");
    handle_connection_established();
    break;

  case ESP_GATTC_NOTIFY_EVT: {
    if (param->notify.conn_id != this->parent()->get_conn_id() ||
        param->notify.handle != this->read_handle_)
      break;

    std::vector<unsigned char> data(
        param->notify.value, param->notify.value + param->notify.value_len);

    if (vehicle_)
      vehicle_->on_rx_data(data);
    break;
  }

  case ESP_GATTC_WRITE_CHAR_EVT:
    if (param->write.status != ESP_GATT_OK) {
      ESP_LOGW(TAG, "BLE write failed: %d", param->write.status);
    }
    break;

  default:
    break;
  }
}

void TeslaBLEVehicle::handle_connection_established() {
  if (vehicle_) {
    vehicle_->set_connected(true);
    ESP_LOGI(TAG, "Connection established - triggering initial polls");
    vehicle_->vcsec_poll();
    vehicle_->infotainment_poll(TeslaBLE::WakePolicy::WAKE_IF_NEEDED);
    last_vcsec_poll_ = millis();
    poll_policy_.on_poll(millis());
    poll_policy_.reset();
  }

  this->status_clear_warning();
}

void TeslaBLEVehicle::handle_connection_lost() {
  if (vehicle_)
    vehicle_->set_connected(false);
  if (ble_adapter_)
    ble_adapter_->clear_queues();

  this->cancel_timeout("command-failed-warning");

  last_vcsec_poll_ = 0;
  poll_policy_.reset();
  this->status_set_warning("BLE connection lost");
}

// =============================================================================
// Button and Switch implementations
// =============================================================================
// Note: All button and switch implementations are now generated by macros
// in tesla_ble_vehicle.h (DEFINE_TESLA_BUTTON and DEFINE_TESLA_SWITCH)
// Only Number controls need explicit implementation due to validation logic

void TeslaChargingAmpsNumber::control(float value) {
  if (!parent_)
    return;

  float min_val = this->traits.get_min_value();
  float max_val = this->traits.get_max_value();

  if (value < min_val || value > max_val) {
    ESP_LOGW(TAG, "Charging amps value %.1f out of bounds", value);
    return;
  }

  parent_->set_charging_amps(static_cast<int>(value));
}

void TeslaChargingLimitNumber::control(float value) {
  if (!parent_)
    return;

  float min_val = this->traits.get_min_value();
  float max_val = this->traits.get_max_value();

  if (value < min_val || value > max_val) {
    ESP_LOGW(TAG, "Charging limit value %.1f out of bounds", value);
    return;
  }

  parent_->set_charging_limit(static_cast<int>(value));
}

// =============================================================================
// Lock implementations
// =============================================================================

void TeslaDoorsLock::control(const lock::LockCall &call) {
  if (!parent_)
    return;

  auto state = call.get_state();
  if (state.has_value()) {
    if (state.value() == lock::LOCK_STATE_LOCKED) {
      parent_->lock_vehicle();
      publish_state(lock::LOCK_STATE_LOCKING);
    } else if (state.value() == lock::LOCK_STATE_UNLOCKED) {
      parent_->unlock_vehicle();
      publish_state(lock::LOCK_STATE_UNLOCKING);
    }
  }
}

void TeslaChargePortLatchLock::control(const lock::LockCall &call) {
  if (!parent_)
    return;

  auto state = call.get_state();
  if (state.has_value()) {
    if (state.value() == lock::LOCK_STATE_LOCKED) {
      // Close charge port door (will also lock the latch)
      parent_->close_charge_port();
      publish_state(lock::LOCK_STATE_LOCKING);
    } else if (state.value() == lock::LOCK_STATE_UNLOCKED) {
      // Unlock the charge port latch (releases the cable)
      parent_->unlock_charge_port();
      publish_state(lock::LOCK_STATE_UNLOCKING);
    }
  }
}

// =============================================================================
// Cover implementations
// =============================================================================

cover::CoverTraits TeslaCoverBase::get_traits() {
  auto traits = cover::CoverTraits();
  traits.set_supports_position(false);
  traits.set_supports_tilt(false);
  traits.set_supports_stop(false);
  traits.set_is_assumed_state(false);
  return traits;
}

void TeslaTrunkCover::control(const cover::CoverCall &call) {
  if (!parent_)
    return;

  if (call.get_position().has_value()) {
    float pos = call.get_position().value();
    if (pos == cover::COVER_OPEN) {
      parent_->open_trunk();
    } else if (pos == cover::COVER_CLOSED) {
      parent_->close_trunk();
    }
  }
}

void TeslaFrunkCover::control(const cover::CoverCall &call) {
  if (!parent_)
    return;

  // Frunk can only be opened (no close command)
  if (call.get_position().has_value()) {
    float pos = call.get_position().value();
    if (pos == cover::COVER_OPEN) {
      parent_->open_frunk();
    }
    // Close is not supported for frunk
  }
}

void TeslaWindowsCover::control(const cover::CoverCall &call) {
  if (!parent_)
    return;

  if (call.get_position().has_value()) {
    float pos = call.get_position().value();
    if (pos == cover::COVER_OPEN) {
      parent_->vent_windows();
    } else if (pos == cover::COVER_CLOSED) {
      parent_->close_windows();
    }
  }
}

void TeslaChargePortDoorCover::control(const cover::CoverCall &call) {
  if (!parent_)
    return;

  if (call.get_position().has_value()) {
    float pos = call.get_position().value();
    if (pos == cover::COVER_OPEN) {
      parent_->open_charge_port();
    } else if (pos == cover::COVER_CLOSED) {
      parent_->close_charge_port();
    }
  }
}

cover::CoverTraits TeslaSunroofCover::get_traits() {
  auto traits = cover::CoverTraits();
  traits.set_supports_position(true);
  traits.set_supports_tilt(false);
  traits.set_supports_stop(false);
  traits.set_is_assumed_state(false);
  return traits;
}

void TeslaSunroofCover::control(const cover::CoverCall &call) {
  if (!parent_)
    return;

  if (call.get_position().has_value()) {
    const float pos = call.get_position().value();
    int percent = static_cast<int>(pos * 100.0f + 0.5f);
    if (percent < 0)
      percent = 0;
    if (percent > 100)
      percent = 100;
    parent_->set_sunroof_position(percent);
  }
}

// =============================================================================
// Select implementations
// =============================================================================

void TeslaSeatClimateSelect::control(size_t index) {
  if (!parent_)
    return;

  const auto selection = seat_climate_selection(index, cooling_supported_, auto_supported_);
  if (!selection.valid) {
    ESP_LOGW(TAG, "Ignoring unsupported seat climate option %u", (unsigned) index);
    return;
  }
  if (selection.auto_mode) {
    parent_->set_seat_auto(seat_, true);
  } else {
    parent_->set_seat_climate(seat_, selection.level, selection.cooling);
  }
}

void TeslaCabinProtectionSelect::control(size_t index) {
  if (!parent_ || index > 2)
    return;
  parent_->set_cabin_overheat_protection(static_cast<int>(index));
}

void TeslaCabinTemperatureSelect::control(size_t index) {
  if (!parent_ || index > 2)
    return;
  parent_->set_cabin_overheat_temperature(static_cast<int>(index) + 1);
}

// =============================================================================
// Climate implementation
// =============================================================================

TeslaClimate::TeslaClimate() {
  this->set_supported_custom_presets(
      {"Normal", "Defrost", "Keep On", "Dog Mode", "Camp Mode"});
  this->set_supported_custom_fan_modes({"Normal", "Bioweapon Mode"});
}

climate::ClimateTraits TeslaClimate::traits() {
  auto traits = climate::ClimateTraits();
  traits.set_supported_modes(
      {climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_HEAT_COOL});
  // Use feature flags for current temperature support
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
  traits.set_visual_min_temperature(15.0f);
  traits.set_visual_max_temperature(28.0f);
  traits.set_visual_temperature_step(0.5f);
  return traits;
}

void TeslaClimate::control(const climate::ClimateCall &call) {
  if (!parent_)
    return;

  if (call.get_mode().has_value()) {
    auto mode = call.get_mode().value();
    // ESPHome does not apply the call to the entity before control(): update
    // the published state too or HA reverts it until the next poll.
    this->mode = mode;
    if (mode == climate::CLIMATE_MODE_OFF) {
      parent_->set_climate_on(false);
    } else if (mode == climate::CLIMATE_MODE_HEAT_COOL) {
      parent_->set_climate_on(true);
    }
  }

  // Handle custom presets
  const auto custom = call.get_custom_preset();
  if (!custom.empty()) {
    // No telemetry reports the active preset, so keep the selection locally.
    this->set_custom_preset_(custom);
    if (custom == "Normal") {
      parent_->set_preconditioning_max(false);
      parent_->set_climate_keeper(0);
    } else if (custom == "Defrost") {
      parent_->set_preconditioning_max(true);
    } else if (custom == "Keep On") {
      parent_->set_preconditioning_max(false);
      parent_->set_climate_keeper(1);
    } else if (custom == "Dog Mode") {
      parent_->set_preconditioning_max(false);
      parent_->set_climate_keeper(2);
    } else if (custom == "Camp Mode") {
      parent_->set_preconditioning_max(false);
      parent_->set_climate_keeper(3);
    }
  }

  // Handle custom fan modes
  const auto fan = call.get_custom_fan_mode();
  if (!fan.empty()) {
    this->set_custom_fan_mode_(fan);
    if (fan == "Normal") {
      parent_->set_bioweapon_mode(false);
    } else if (fan == "Bioweapon Mode") {
      parent_->set_bioweapon_mode(true);
    }
  }

  if (call.get_target_temperature().has_value()) {
    float temp = call.get_target_temperature().value();
    parent_->set_climate_temp(temp);
    this->target_temperature = temp;
  }

  this->publish_state();
}

void TeslaClimate::update_state(bool is_on, float current_temp,
                                float target_temp) {
  this->mode =
      is_on ? climate::CLIMATE_MODE_HEAT_COOL : climate::CLIMATE_MODE_OFF;

  if (!std::isnan(current_temp)) {
    this->current_temperature = current_temp;
  }

  if (!std::isnan(target_temp)) {
    this->target_temperature = target_temp;
  }

  this->publish_state();
}

} // namespace tesla_ble_vehicle
} // namespace esphome
