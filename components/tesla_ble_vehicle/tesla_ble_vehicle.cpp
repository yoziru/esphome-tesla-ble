#include "tesla_ble_vehicle.h"
#include "common.h"
#include <esphome/core/helpers.h>
#include <client.h>
#include <tb_utils.h>
#include <esp_log.h>
#include <cstring>

namespace esphome {
namespace tesla_ble_vehicle {

void tesla_ble_log_callback(TeslaBLE::LogLevel level, const char* tag, int line, const char* format, va_list args) {
    if (tag == nullptr) tag = "TeslaBLE";
    if (format == nullptr) return;
    
    int esphome_level;
    switch (level) {
        case TeslaBLE::LogLevel::ERROR:   esphome_level = ESPHOME_LOG_LEVEL_ERROR; break;
        case TeslaBLE::LogLevel::WARN:    esphome_level = ESPHOME_LOG_LEVEL_WARN; break;
        case TeslaBLE::LogLevel::INFO:    esphome_level = ESPHOME_LOG_LEVEL_INFO; break;
        case TeslaBLE::LogLevel::DEBUG:   esphome_level = ESPHOME_LOG_LEVEL_DEBUG; break;
        case TeslaBLE::LogLevel::VERBOSE: esphome_level = ESPHOME_LOG_LEVEL_VERBOSE; break;
        default: return;
    }
    esp_log_vprintf_(esphome_level, tag, line, format, args);
}

TeslaBLEVehicle::TeslaBLEVehicle() : vin_(""), role_("DRIVER") {
    ESP_LOGCONFIG(TAG, "Constructing Tesla BLE Vehicle component");
}

void TeslaBLEVehicle::setup() {
    ESP_LOGCONFIG(TAG, "Setting up TeslaBLEVehicle");
    
    initialize_ble_uuids();
    initialize_managers();
    configure_pending_sensors();
    
    if (!vin_.empty()) vehicle_->set_vin(vin_);
    
    setup_button_callbacks();
}

void TeslaBLEVehicle::initialize_managers() {
    ESP_LOGD(TAG, "Initializing components...");
    
    ble_adapter_ = std::make_shared<BleAdapterImpl>(this);
    storage_adapter_ = std::make_shared<StorageAdapterImpl>();
    
    if (!storage_adapter_->initialize()) {
        ESP_LOGE(TAG, "Failed to initialize storage adapter");
    }
    
    TeslaBLE::g_log_callback = tesla_ble_log_callback;
    vehicle_ = std::make_shared<TeslaBLE::Vehicle>(ble_adapter_, storage_adapter_);
    state_manager_ = std::make_unique<VehicleStateManager>(this);
    
    ESP_LOGD(TAG, "Wiring up callbacks...");
    
    vehicle_->set_raw_message_callback([this](const std::vector<uint8_t>& data) {
        std::string hex = TeslaBLE::format_hex(data.data(), data.size());
        if (hex != last_rx_hex_) {
            ESP_LOGD(TAG, "BLE RX: %s", hex.c_str());
            last_rx_hex_ = hex;
        }
    });
    
    vehicle_->set_vehicle_status_callback([this](const VCSEC_VehicleStatus& s) {
        if (state_manager_) state_manager_->update_vehicle_status(s);
    });
    
    vehicle_->set_charge_state_callback([this](const CarServer_ChargeState& s) {
        if (state_manager_) state_manager_->update_charge_state(s);
    });
    
    vehicle_->set_climate_state_callback([this](const CarServer_ClimateState& s) {
        if (state_manager_) state_manager_->update_climate_state(s);
    });
    
    vehicle_->set_drive_state_callback([this](const CarServer_DriveState& s) {
        if (state_manager_) state_manager_->update_drive_state(s);
    });
    
    vehicle_->set_tire_pressure_state_callback([this](const CarServer_TirePressureState& s) {
        if (state_manager_) state_manager_->update_tire_pressure_state(s);
    });
    
    vehicle_->set_closures_state_callback([this](const CarServer_ClosuresState& s) {
        if (state_manager_) state_manager_->update_closures_state(s);
    });
    
    ESP_LOGD(TAG, "All components initialized");
}

void TeslaBLEVehicle::initialize_ble_uuids() {
    service_uuid_ = espbt::ESPBTUUID::from_raw(SERVICE_UUID);
    read_uuid_ = espbt::ESPBTUUID::from_raw(READ_UUID);
    write_uuid_ = espbt::ESPBTUUID::from_raw(WRITE_UUID);
}

void TeslaBLEVehicle::setup_button_callbacks() {
    ESP_LOGD(TAG, "Button callbacks configured");
}

void TeslaBLEVehicle::configure_pending_sensors() {
    if (!state_manager_) {
        ESP_LOGE(TAG, "State manager not available");
        return;
    }
    
    for (const auto& pair : pending_binary_sensors_)
        state_manager_->set_binary_sensor(pair.first, pair.second);
    for (const auto& pair : pending_sensors_)
        state_manager_->set_sensor(pair.first, pair.second);
    for (const auto& pair : pending_text_sensors_)
        state_manager_->set_text_sensor(pair.first, pair.second);
    
    if (pending_charging_switch_) state_manager_->set_charging_switch(pending_charging_switch_);
    if (pending_sentry_mode_switch_) state_manager_->set_sentry_mode_switch(pending_sentry_mode_switch_);
    if (pending_steering_wheel_heat_switch_) state_manager_->set_steering_wheel_heat_switch(pending_steering_wheel_heat_switch_);
    if (pending_charging_amps_number_) state_manager_->set_charging_amps_number(pending_charging_amps_number_);
    if (pending_charging_limit_number_) state_manager_->set_charging_limit_number(pending_charging_limit_number_);
    if (pending_doors_lock_) state_manager_->set_doors_lock(pending_doors_lock_);
    if (pending_charge_port_latch_lock_) state_manager_->set_charge_port_latch_lock(pending_charge_port_latch_lock_);
    if (pending_trunk_cover_) state_manager_->set_trunk_cover(pending_trunk_cover_);
    if (pending_frunk_cover_) state_manager_->set_frunk_cover(pending_frunk_cover_);
    if (pending_windows_cover_) state_manager_->set_windows_cover(pending_windows_cover_);
    if (pending_charge_port_door_cover_) state_manager_->set_charge_port_door_cover(pending_charge_port_door_cover_);
    if (pending_climate_) state_manager_->set_climate(pending_climate_);
    
    ESP_LOGD(TAG, "Configured %d binary, %d numeric, %d text sensors", 
             pending_binary_sensors_.size(), pending_sensors_.size(), pending_text_sensors_.size());
}

void TeslaBLEVehicle::loop() {
    if (vehicle_) vehicle_->loop();
    if (ble_adapter_) ble_adapter_->process_write_queue();
}

void TeslaBLEVehicle::update() {
    if (!is_connected() || !vehicle_) return;
    
    uint32_t now = millis();
    
    // VCSEC Polling
    if (now - last_vcsec_poll_ >= vcsec_poll_interval_) {
        ESP_LOGI(TAG, "Polling VCSEC");
        vehicle_->vcsec_poll();
        last_vcsec_poll_ = now;
    }
    
    // Infotainment Polling - use faster interval when vehicle is active
    uint32_t infotainment_interval = state_manager_->is_asleep() 
        ? infotainment_sleep_timeout_
        : (state_manager_->is_charging() || state_manager_->is_user_present() || state_manager_->is_unlocked())
            ? infotainment_poll_interval_active_
            : infotainment_poll_interval_awake_;
    
    if (now - last_infotainment_poll_ >= infotainment_interval) {
        ESP_LOGI(TAG, "Polling Infotainment");
        vehicle_->infotainment_poll();
        last_infotainment_poll_ = now;
    }
}

void TeslaBLEVehicle::dump_config() {
    ESP_LOGCONFIG(TAG, "Tesla BLE Vehicle:");
    ESP_LOGCONFIG(TAG, "  VIN: %s", vin_.empty() ? "Not set" : vin_.c_str());
    ESP_LOGCONFIG(TAG, "  Role: %s", role_.c_str());
    ESP_LOGCONFIG(TAG, "  Max Charging Amps: %d", state_manager_ ? state_manager_->get_charging_amps_max() : 32);
    ESP_LOGCONFIG(TAG, "  Polling: VCSEC=%ums, Awake=%ums, Active=%ums",
                  vcsec_poll_interval_, infotainment_poll_interval_awake_, infotainment_poll_interval_active_);
    ESP_LOGCONFIG(TAG, "  Sensors: %d binary, %d numeric, %d text", 
                  pending_binary_sensors_.size(), pending_sensors_.size(), pending_text_sensors_.size());
}

// =============================================================================
// Configuration setters
// =============================================================================

void TeslaBLEVehicle::set_vin(const char *vin) {
    if (vin == nullptr) {
        ESP_LOGW(TAG, "Attempted to set null VIN - ignoring");
        return;
    }
    
    vin_ = std::string(vin);
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
    
    if (state_manager_) {
        state_manager_->set_charging_amps_max(amps_max);
    }
}

void TeslaBLEVehicle::set_vcsec_poll_interval(uint32_t interval_ms) {
    ESP_LOGD(TAG, "Setting VCSEC poll interval: %u ms", interval_ms);
    vcsec_poll_interval_ = interval_ms;
}

void TeslaBLEVehicle::set_infotainment_poll_interval_awake(uint32_t interval_ms) {
    ESP_LOGD(TAG, "Setting infotainment poll interval awake: %u ms", interval_ms);
    infotainment_poll_interval_awake_ = interval_ms;
}

void TeslaBLEVehicle::set_infotainment_poll_interval_active(uint32_t interval_ms) {
    ESP_LOGD(TAG, "Setting infotainment poll interval active: %u ms", interval_ms);
    infotainment_poll_interval_active_ = interval_ms;
}

void TeslaBLEVehicle::set_infotainment_sleep_timeout(uint32_t interval_ms) {
    ESP_LOGD(TAG, "Setting infotainment sleep timeout: %u ms", interval_ms);
    infotainment_sleep_timeout_ = interval_ms;
}

// =============================================================================
// Generic sensor setters
// =============================================================================

void TeslaBLEVehicle::set_binary_sensor(const std::string& id, binary_sensor::BinarySensor* sensor) {
    pending_binary_sensors_[id] = sensor;
    if (state_manager_) state_manager_->set_binary_sensor(id, sensor);
}

void TeslaBLEVehicle::set_sensor(const std::string& id, sensor::Sensor* sensor) {
    pending_sensors_[id] = sensor;
    if (state_manager_) state_manager_->set_sensor(id, sensor);
}

void TeslaBLEVehicle::set_text_sensor(const std::string& id, text_sensor::TextSensor* sensor) {
    pending_text_sensors_[id] = sensor;
    if (state_manager_) state_manager_->set_text_sensor(id, sensor);
}

// =============================================================================
// Control setters
// =============================================================================

void TeslaBLEVehicle::set_charging_switch(switch_::Switch *sw) {
    pending_charging_switch_ = sw;
    if (state_manager_) state_manager_->set_charging_switch(sw);
}

void TeslaBLEVehicle::set_steering_wheel_heat_switch(switch_::Switch *sw) {
    pending_steering_wheel_heat_switch_ = sw;
    if (state_manager_) state_manager_->set_steering_wheel_heat_switch(sw);
}

void TeslaBLEVehicle::set_sentry_mode_switch(switch_::Switch *sw) {
    pending_sentry_mode_switch_ = sw;
    if (state_manager_) state_manager_->set_sentry_mode_switch(sw);
}

void TeslaBLEVehicle::set_charging_amps_number(number::Number *number) {
    pending_charging_amps_number_ = number;
    if (state_manager_) state_manager_->set_charging_amps_number(number);
}

void TeslaBLEVehicle::set_charging_limit_number(number::Number *number) {
    pending_charging_limit_number_ = number;
    if (state_manager_) state_manager_->set_charging_limit_number(number);
}

// =============================================================================
// Lock, Cover, and Climate setters
// =============================================================================

void TeslaBLEVehicle::set_doors_lock(lock::Lock *lck) {
    pending_doors_lock_ = lck;
    if (state_manager_) state_manager_->set_doors_lock(lck);
}

void TeslaBLEVehicle::set_charge_port_latch_lock(lock::Lock *lck) {
    pending_charge_port_latch_lock_ = lck;
    if (state_manager_) state_manager_->set_charge_port_latch_lock(lck);
}

void TeslaBLEVehicle::set_trunk_cover(cover::Cover *cvr) {
    pending_trunk_cover_ = cvr;
    if (state_manager_) state_manager_->set_trunk_cover(cvr);
}

void TeslaBLEVehicle::set_frunk_cover(cover::Cover *cvr) {
    pending_frunk_cover_ = cvr;
    if (state_manager_) state_manager_->set_frunk_cover(cvr);
}

void TeslaBLEVehicle::set_windows_cover(cover::Cover *cvr) {
    pending_windows_cover_ = cvr;
    if (state_manager_) state_manager_->set_windows_cover(cvr);
}

void TeslaBLEVehicle::set_charge_port_door_cover(cover::Cover *cvr) {
    pending_charge_port_door_cover_ = cvr;
    if (state_manager_) state_manager_->set_charge_port_door_cover(cvr);
}

void TeslaBLEVehicle::set_climate(climate::Climate *clm) {
    pending_climate_ = clm;
    if (state_manager_) state_manager_->set_climate(clm);
}

// =============================================================================
// Button setters
// =============================================================================

void TeslaBLEVehicle::set_wake_button(button::Button *button) {
    TeslaWakeButton* wake_button = static_cast<TeslaWakeButton*>(button);
    if (wake_button) wake_button->set_parent(this);
}

void TeslaBLEVehicle::set_pair_button(button::Button *button) {
    TeslaPairButton* pair_button = static_cast<TeslaPairButton*>(button);
    if (pair_button) pair_button->set_parent(this);
}

void TeslaBLEVehicle::set_regenerate_key_button(button::Button *button) {
    TeslaRegenerateKeyButton* regen_button = static_cast<TeslaRegenerateKeyButton*>(button);
    if (regen_button) regen_button->set_parent(this);
}

void TeslaBLEVehicle::set_force_update_button(button::Button *button) {
    TeslaForceUpdateButton* update_button = static_cast<TeslaForceUpdateButton*>(button);
    if (update_button) update_button->set_parent(this);
}

// =============================================================================
// Public vehicle actions
// =============================================================================

int TeslaBLEVehicle::wake_vehicle() {
    ESP_LOGD(TAG, "Wake vehicle requested");
    
    if (!vehicle_) return -1;
    
    if (state_manager_ && !state_manager_->is_asleep()) {
        ESP_LOGI(TAG, "Vehicle already awake - sending VCSEC poll instead");
        vehicle_->vcsec_poll();
        return 0;
    }
    
    ESP_LOGI(TAG, "Sending wake command");
    vehicle_->wake();
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
    ESP_LOGI(TAG, "Force update requested");
    
    if (vehicle_) {
         vehicle_->wake();
         vehicle_->vcsec_poll();
         vehicle_->infotainment_poll();
    }
}

int TeslaBLEVehicle::set_charging_state(bool charging) {
    ESP_LOGI(TAG, "Set charging state: %s", charging ? "ON" : "OFF");
    
    if (state_manager_) state_manager_->track_command_issued();
    
    if (!vehicle_) {
        ESP_LOGE(TAG, "Vehicle instance not available");
        return -1;
    }
    
    vehicle_->set_charging_state(charging);
    return 0;
}

int TeslaBLEVehicle::set_charging_amps(int amps) {
    ESP_LOGI(TAG, "Set charging amps: %d", amps);
    
    if (amps < 0) {
        ESP_LOGW(TAG, "Invalid charging amps: %d", amps);
        return -1;
    }
    
    int max_amps = state_manager_->get_charging_amps_max();
    if (amps > max_amps) {
        ESP_LOGW(TAG, "Requested amps (%d) exceeds maximum (%d), clamping", amps, max_amps);
        amps = max_amps;
    }
    
    if (state_manager_) state_manager_->track_command_issued();
    
    if (!vehicle_) {
        ESP_LOGE(TAG, "Vehicle instance not available");
        return -1;
    }
    
    vehicle_->set_charging_amps(amps);
    return 0;
}

int TeslaBLEVehicle::set_charging_limit(int limit) {
    ESP_LOGI(TAG, "Set charging limit: %d%%", limit);
    
    if (limit < MIN_CHARGING_LIMIT || limit > MAX_CHARGING_LIMIT) {
        ESP_LOGW(TAG, "Invalid charging limit: %d%%", limit);
        return -1;
    }
    
    if (state_manager_) state_manager_->track_command_issued();
    
    if (!vehicle_) {
        ESP_LOGE(TAG, "Vehicle instance not available");
        return -1;
    }
    
    vehicle_->set_charging_limit(limit);
    return 0;
}

// =============================================================================
// Closure controls (VCSEC)
// =============================================================================

void TeslaBLEVehicle::lock_vehicle() {
    ESP_LOGI(TAG, "Lock vehicle requested");
    if (state_manager_) state_manager_->track_command_issued();
    if (vehicle_) vehicle_->lock();
}

void TeslaBLEVehicle::unlock_vehicle() {
    ESP_LOGI(TAG, "Unlock vehicle requested");
    if (state_manager_) state_manager_->track_command_issued();
    if (vehicle_) vehicle_->unlock();
}

void TeslaBLEVehicle::open_trunk() {
    ESP_LOGI(TAG, "Open trunk requested");
    if (state_manager_) state_manager_->track_command_issued();
    if (vehicle_) vehicle_->open_trunk();
}

void TeslaBLEVehicle::close_trunk() {
    ESP_LOGI(TAG, "Close trunk requested");
    if (state_manager_) state_manager_->track_command_issued();
    if (vehicle_) vehicle_->close_trunk();
}

void TeslaBLEVehicle::open_frunk() {
    ESP_LOGI(TAG, "Open frunk requested");
    if (state_manager_) state_manager_->track_command_issued();
    if (vehicle_) vehicle_->open_frunk();
}

void TeslaBLEVehicle::open_charge_port() {
    ESP_LOGI(TAG, "Open charge port requested");
    if (state_manager_) state_manager_->track_command_issued();
    if (vehicle_) vehicle_->open_charge_port();
}

void TeslaBLEVehicle::close_charge_port() {
    ESP_LOGI(TAG, "Close charge port requested");
    if (state_manager_) state_manager_->track_command_issued();
    if (vehicle_) vehicle_->close_charge_port();
}

void TeslaBLEVehicle::unlock_charge_port() {
    ESP_LOGI(TAG, "Unlock charge port latch requested");
    if (state_manager_) state_manager_->track_command_issued();
    if (vehicle_) vehicle_->unlock_charge_port();
}

void TeslaBLEVehicle::unlatch_driver_door() {
    ESP_LOGI(TAG, "Unlatch driver door requested");
    if (state_manager_) state_manager_->track_command_issued();
    if (vehicle_) vehicle_->unlatch_driver_door();
}

// =============================================================================
// HVAC controls (Infotainment)
// =============================================================================

void TeslaBLEVehicle::set_climate_on(bool enable) {
    ESP_LOGI(TAG, "Climate %s requested", enable ? "ON" : "OFF");
    if (state_manager_) state_manager_->track_command_issued();
    if (vehicle_) vehicle_->set_climate(enable);
}

void TeslaBLEVehicle::set_climate_temp(float temp) {
    ESP_LOGI(TAG, "Climate temperature %.1f°C requested", temp);
    if (state_manager_) state_manager_->track_command_issued();
    if (vehicle_) vehicle_->set_climate_temp(temp);
}

void TeslaBLEVehicle::set_climate_keeper(int mode) {
    const char* mode_names[] = {"Off", "On", "Dog", "Camp"};
    ESP_LOGI(TAG, "Climate keeper %s requested", (mode >= 0 && mode <= 3) ? mode_names[mode] : "Unknown");
    if (state_manager_) state_manager_->track_command_issued();
    if (vehicle_) vehicle_->set_climate_keeper(mode);
}

void TeslaBLEVehicle::set_bioweapon_mode(bool enable) {
    ESP_LOGI(TAG, "Bioweapon mode %s requested", enable ? "ON" : "OFF");
    if (state_manager_) state_manager_->track_command_issued();
    if (vehicle_) vehicle_->set_bioweapon_mode(enable);
}

void TeslaBLEVehicle::set_preconditioning_max(bool enable) {
    ESP_LOGI(TAG, "Preconditioning max (defrost) %s requested", enable ? "ON" : "OFF");
    if (state_manager_) state_manager_->track_command_issued();
    if (vehicle_) vehicle_->set_preconditioning_max(enable);
}

void TeslaBLEVehicle::set_steering_wheel_heat(bool enable) {
    ESP_LOGI(TAG, "Steering wheel heat %s requested", enable ? "ON" : "OFF");
    if (state_manager_) state_manager_->track_command_issued();
    if (vehicle_) vehicle_->set_steering_wheel_heat(enable);
}

// =============================================================================
// Vehicle controls (Infotainment)
// =============================================================================

void TeslaBLEVehicle::flash_lights() {
    ESP_LOGI(TAG, "Flash lights requested");
    if (state_manager_) state_manager_->track_command_issued();
    if (vehicle_) vehicle_->flash_lights();
}

void TeslaBLEVehicle::honk_horn() {
    ESP_LOGI(TAG, "Honk horn requested");
    if (state_manager_) state_manager_->track_command_issued();
    if (vehicle_) vehicle_->honk_horn();
}

void TeslaBLEVehicle::set_sentry_mode(bool enable) {
    ESP_LOGI(TAG, "Sentry mode %s requested", enable ? "ON" : "OFF");
    if (state_manager_) state_manager_->track_command_issued();
    if (vehicle_) vehicle_->set_sentry_mode(enable);
}

void TeslaBLEVehicle::vent_windows() {
    ESP_LOGI(TAG, "Vent windows requested");
    if (state_manager_) state_manager_->track_command_issued();
    if (vehicle_) vehicle_->vent_windows();
}

void TeslaBLEVehicle::close_windows() {
    ESP_LOGI(TAG, "Close windows requested");
    if (state_manager_) state_manager_->track_command_issued();
    if (vehicle_) vehicle_->close_windows();
}

void TeslaBLEVehicle::update_charging_amps_max_value(int32_t new_max) {
    if (pending_charging_amps_number_) {
        auto* tesla_amps = static_cast<TeslaChargingAmpsNumber*>(pending_charging_amps_number_);
        tesla_amps->update_max_value(new_max);
        ESP_LOGD(TAG, "Updated charging amps max value to %d A", new_max);
    }
}

// =============================================================================
// BLE event handling
// =============================================================================

void TeslaBLEVehicle::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
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
            auto *readChar = this->parent()->get_characteristic(this->service_uuid_, this->read_uuid_);
            if (readChar == nullptr) {
                ESP_LOGE(TAG, "Read characteristic not found");
                break;
            }
            this->read_handle_ = readChar->handle;
            
            auto reg_status = esp_ble_gattc_register_for_notify(
                this->parent()->get_gattc_if(), 
                this->parent()->get_remote_bda(), 
                readChar->handle);
            if (reg_status) {
                ESP_LOGE(TAG, "Failed to register for notifications: %d", reg_status);
            }
            
            auto *writeChar = this->parent()->get_characteristic(this->service_uuid_, this->write_uuid_);
            if (writeChar == nullptr) {
                ESP_LOGE(TAG, "Write characteristic not found");
                break;
            }
            this->write_handle_ = writeChar->handle;
            break;
        }
            
        case ESP_GATTC_REG_FOR_NOTIFY_EVT:
            if (param->reg_for_notify.status != ESP_GATT_OK) {
                ESP_LOGE(TAG, "Failed to register for notifications");
                break;
            }
            
            this->node_state = espbt::ClientState::ESTABLISHED;
            ESP_LOGI(TAG, "BLE connection fully established");
            handle_connection_established();
            break;
            
        case ESP_GATTC_NOTIFY_EVT: {
            if (param->notify.conn_id != this->parent()->get_conn_id()) break;
            
            std::vector<unsigned char> data(
                param->notify.value, 
                param->notify.value + param->notify.value_len);
            
            if (vehicle_) vehicle_->on_rx_data(data);
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
        vehicle_->infotainment_poll(true);
        last_vcsec_poll_ = millis();
        last_infotainment_poll_ = millis();
    }
    
    if (state_manager_) state_manager_->set_sensors_available(true);
    this->status_clear_warning();
}

void TeslaBLEVehicle::handle_connection_lost() {
    if (vehicle_) vehicle_->set_connected(false);
    if (ble_adapter_) ble_adapter_->clear_queues();
    
    last_infotainment_poll_ = 0;
    last_vcsec_poll_ = 0;
    this->status_set_warning("BLE connection lost");
}

// =============================================================================
// Button and Switch implementations
// =============================================================================
// Note: All button and switch implementations are now generated by macros
// in tesla_ble_vehicle.h (DEFINE_TESLA_BUTTON and DEFINE_TESLA_SWITCH)
// Only Number controls need explicit implementation due to validation logic

void TeslaChargingAmpsNumber::control(float value) {
    if (!parent_) return;
    
    float min_val = this->traits.get_min_value();
    float max_val = this->traits.get_max_value();
    
    if (value < min_val || value > max_val) {
        ESP_LOGW(TAG, "Charging amps value %.1f out of bounds", value);
        return;
    }
    
    parent_->set_charging_amps(static_cast<int>(value));
    publish_state(value);
}

void TeslaChargingAmpsNumber::update_max_value(int32_t new_max) {
    if (new_max <= 0) return;
    
    auto old_max = this->traits.get_max_value();
    
    if (std::abs(old_max - new_max) > 0.1f) {
        ESP_LOGD(TAG, "Updating charging amps max from %.0f to %d A", old_max, new_max);
        this->traits.set_max_value(new_max);
        
        if (this->has_state() && this->state > new_max) {
            this->publish_state(new_max);
        }
        
        if (this->has_state()) {
            this->publish_state(this->state);
        }
    }
}

void TeslaChargingLimitNumber::control(float value) {
    if (!parent_) return;
    
    float min_val = this->traits.get_min_value();
    float max_val = this->traits.get_max_value();
    
    if (value < min_val || value > max_val) {
        ESP_LOGW(TAG, "Charging limit value %.1f out of bounds", value);
        return;
    }
    
    parent_->set_charging_limit(static_cast<int>(value));
    publish_state(value);
}

// =============================================================================
// Lock implementations
// =============================================================================

void TeslaDoorsLock::control(const lock::LockCall &call) {
    if (!parent_) return;
    
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
    if (!parent_) return;
    
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
    if (!parent_) return;
    
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
    if (!parent_) return;
    
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
    if (!parent_) return;
    
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
    if (!parent_) return;
    
    if (call.get_position().has_value()) {
        float pos = call.get_position().value();
        if (pos == cover::COVER_OPEN) {
            parent_->open_charge_port();
        } else if (pos == cover::COVER_CLOSED) {
            parent_->close_charge_port();
        }
    }
}

// =============================================================================
// Climate implementation
// =============================================================================

climate::ClimateTraits TeslaClimate::traits() {
    auto traits = climate::ClimateTraits();
    traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_HEAT_COOL});
    // Custom presets with clear text descriptions
    traits.set_supported_custom_presets({"Normal", "Defrost", "Keep On", "Dog Mode", "Camp Mode"});
    // Custom fan modes with clear text descriptions
    traits.set_supported_custom_fan_modes({"Normal", "Bioweapon Mode"});
    // Use feature flags for current temperature support
    traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
    traits.set_visual_min_temperature(15.0f);
    traits.set_visual_max_temperature(28.0f);
    traits.set_visual_temperature_step(0.5f);
    return traits;
}

void TeslaClimate::control(const climate::ClimateCall &call) {
    if (!parent_) return;
    
    if (call.get_mode().has_value()) {
        auto mode = call.get_mode().value();
        if (mode == climate::CLIMATE_MODE_OFF) {
            parent_->set_climate_on(false);
        } else if (mode == climate::CLIMATE_MODE_HEAT_COOL) {
            parent_->set_climate_on(true);
        }
    }
    
    // Handle custom presets
    const char* custom = call.get_custom_preset();
    if (custom != nullptr) {
        if (strcmp(custom, "Normal") == 0) {
            parent_->set_preconditioning_max(false);
            parent_->set_climate_keeper(0);
        } else if (strcmp(custom, "Defrost") == 0) {
            parent_->set_preconditioning_max(true);
        } else if (strcmp(custom, "Keep On") == 0) {
            parent_->set_preconditioning_max(false);
            parent_->set_climate_keeper(1);
        } else if (strcmp(custom, "Dog Mode") == 0) {
            parent_->set_preconditioning_max(false);
            parent_->set_climate_keeper(2);
        } else if (strcmp(custom, "Camp Mode") == 0) {
            parent_->set_preconditioning_max(false);
            parent_->set_climate_keeper(3);
        }
    }
    
    // Handle custom fan modes
    const char* fan = call.get_custom_fan_mode();
    if (fan != nullptr) {
        if (strcmp(fan, "Normal") == 0) {
            parent_->set_bioweapon_mode(false);
        } else if (strcmp(fan, "Bioweapon Mode") == 0) {
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

void TeslaClimate::update_state(bool is_on, float current_temp, float target_temp) {
    this->mode = is_on ? climate::CLIMATE_MODE_HEAT_COOL : climate::CLIMATE_MODE_OFF;
    
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
