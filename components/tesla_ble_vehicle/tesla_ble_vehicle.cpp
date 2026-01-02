#include "tesla_ble_vehicle.h"
#include "common.h"
#include <esphome/core/helpers.h>
#include <client.h>
#include <tb_utils.h>
#include <esp_log.h>

namespace esphome {
namespace tesla_ble_vehicle {

void tesla_ble_log_callback(TeslaBLE::LogLevel level, const char* tag, int line, const char* format, va_list args) {
    // Safety checks
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
    
    // Initialize BLE UUIDs
    initialize_ble_uuids();
    
    // Initialize all managers
    initialize_managers();
    
    // Configure any sensors that were set before managers were initialized
    configure_pending_sensors();
    
    // Initialize session logic usually happens implicitly in library via storage adapter
    // No explicit session_manager initialization call needed for Vehicle class other than passing adapter.
    
    // Set VIN if provided
    if (!vin_.empty()) {
        vehicle_->set_vin(vin_);
    }
    
    // Setup button callbacks
    setup_button_callbacks();
}

void TeslaBLEVehicle::initialize_managers() {
    ESP_LOGD(TAG, "Initializing components...");
    
    // Create adapters and vehicle
    ble_adapter_ = std::make_shared<BleAdapterImpl>(this);
    storage_adapter_ = std::make_shared<StorageAdapterImpl>();
    
    // Initialize storage
    if (!storage_adapter_->initialize()) {
        ESP_LOGE(TAG, "Failed to initialize storage adapter");
    }
    
    // Register logging bridge
    TeslaBLE::g_log_callback = tesla_ble_log_callback;
    
    // Create vehicle instance
    vehicle_ = std::make_shared<TeslaBLE::Vehicle>(ble_adapter_, storage_adapter_);
    
    // Create state manager (legacy sensor handler)
    state_manager_ = std::make_unique<VehicleStateManager>(this);
    
    ESP_LOGD(TAG, "Wiring up callbacks...");
    
    // Register raw message callback for reassembled logging with de-duplication
    vehicle_->set_raw_message_callback([this](const std::vector<uint8_t>& data) {
        std::string hex = TeslaBLE::format_hex(data.data(), data.size());
        if (hex != last_rx_hex_) {
            ESP_LOGD(TAG, "BLE RX: %s", hex.c_str());
            last_rx_hex_ = hex;
        }
    });
    
    // Wire Vehicle callbacks to State Manager
    vehicle_->set_vehicle_status_callback([this](const VCSEC_VehicleStatus& s) {
        if (state_manager_) {
            state_manager_->update_vehicle_status(s);
            
            // Trigger infotainment poll if vehicle is awake and we haven't polled yet (or if state just changed to awake)
            if (!state_manager_->is_asleep() && last_infotainment_poll_ == 0) {
                ESP_LOGI(TAG, "Vehicle is awake, triggering initial infotainment poll");
                vehicle_->infotainment_poll();
                last_infotainment_poll_ = millis();
            }
        }
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
    
    ESP_LOGD(TAG, "All components initialized");
}

void TeslaBLEVehicle::initialize_ble_uuids() {
    service_uuid_ = espbt::ESPBTUUID::from_raw(SERVICE_UUID);
    read_uuid_ = espbt::ESPBTUUID::from_raw(READ_UUID);
    write_uuid_ = espbt::ESPBTUUID::from_raw(WRITE_UUID);
}

void TeslaBLEVehicle::setup_button_callbacks() {
    // Note: Button callbacks are handled by the custom button classes
    // This is just for any additional setup if needed
    ESP_LOGD(TAG, "Button callbacks configured");
}

void TeslaBLEVehicle::configure_pending_sensors() {
    ESP_LOGD(TAG, "Configuring pending sensors with state manager");
    
    if (state_manager_) {
        // Configure binary sensors
        if (pending_asleep_sensor_) {
            ESP_LOGD(TAG, "Configuring asleep sensor");
            state_manager_->set_asleep_sensor(pending_asleep_sensor_);
        }
        if (pending_unlocked_sensor_) {
            ESP_LOGD(TAG, "Configuring unlocked sensor");
            state_manager_->set_unlocked_sensor(pending_unlocked_sensor_);
        }
        if (pending_user_present_sensor_) {
            ESP_LOGD(TAG, "Configuring user present sensor");
            state_manager_->set_user_present_sensor(pending_user_present_sensor_);
        }
        if (pending_charge_flap_sensor_) {
            ESP_LOGD(TAG, "Configuring charge flap sensor");
            state_manager_->set_charge_flap_sensor(pending_charge_flap_sensor_);
        }
        if (pending_charger_sensor_) {
            ESP_LOGD(TAG, "Configuring charger sensor");
            state_manager_->set_charger_sensor(pending_charger_sensor_);
        }
        
        // Configure regular sensors
        if (pending_battery_level_sensor_) {
            ESP_LOGD(TAG, "Configuring battery level sensor");
            state_manager_->set_battery_level_sensor(pending_battery_level_sensor_);
        }
        if (pending_charger_power_sensor_) {
            ESP_LOGD(TAG, "Configuring charger power sensor");
            state_manager_->set_charger_power_sensor(pending_charger_power_sensor_);
        }
        if (pending_charger_voltage_sensor_) {
            ESP_LOGD(TAG, "Configuring charger voltage sensor");
            state_manager_->set_charger_voltage_sensor(pending_charger_voltage_sensor_);
        }
        if (pending_charger_current_sensor_) {
            ESP_LOGD(TAG, "Configuring charger current sensor");
            state_manager_->set_charger_current_sensor(pending_charger_current_sensor_);
        }
        if (pending_charging_rate_sensor_) {
            ESP_LOGD(TAG, "Configuring charging rate sensor");
            state_manager_->set_charging_rate_sensor(pending_charging_rate_sensor_);
        }
        
        // Configure text sensors
        if (pending_charging_state_sensor_) {
            ESP_LOGD(TAG, "Configuring charging state sensor");
            state_manager_->set_charging_state_sensor(pending_charging_state_sensor_);
        }
        if (pending_iec61851_state_sensor_) {
            ESP_LOGD(TAG, "Configuring IEC 61851 state sensor");
            state_manager_->set_iec61851_state_sensor(pending_iec61851_state_sensor_);
        }
        
        // Configure controls
        if (pending_charging_switch_) {
            ESP_LOGD(TAG, "Configuring charging switch");
            state_manager_->set_charging_switch(pending_charging_switch_);
        }
        if (pending_charging_amps_number_) {
            ESP_LOGD(TAG, "Configuring charging amps number");
            state_manager_->set_charging_amps_number(pending_charging_amps_number_);
        }
        if (pending_charging_limit_number_) {
            ESP_LOGD(TAG, "Configuring charging limit number");
            state_manager_->set_charging_limit_number(pending_charging_limit_number_);
        }
        
        ESP_LOGD(TAG, "All pending sensors configured");
    } else {
        ESP_LOGE(TAG, "State manager not available for sensor configuration");
    }
}

void TeslaBLEVehicle::loop() {
    if (vehicle_) {
        vehicle_->loop();
    }
    
    if (ble_adapter_) {
        ble_adapter_->process_write_queue();
    }
}

void TeslaBLEVehicle::update() {
    if (!is_connected() || !vehicle_) {
        return;
    }
    
    uint32_t now = millis();
    
    // VCSEC Polling
    if (now - last_vcsec_poll_ >= vcsec_poll_interval_) {
        ESP_LOGI(TAG, "Polling VCSEC");
        vehicle_->vcsec_poll();
        last_vcsec_poll_ = now;
    }
    
    // Infotainment Polling logic
    // Determine interval based on state (asleep logic handled by state manager)
    uint32_t infotainment_interval = infotainment_poll_interval_awake_;
    if (state_manager_->is_asleep()) {
         // If asleep, we might want to poll less frequently or not at all unless forced via timeout/logic.
         // Original logic used sleep timeout. For simplicity here:
         infotainment_interval = infotainment_sleep_timeout_; 
    }
    
    // Check for "active" state to use faster polling
    if (!state_manager_->is_asleep()) {
        if (state_manager_->is_charging() || state_manager_->is_user_present() || state_manager_->is_unlocked()) {
            infotainment_interval = infotainment_poll_interval_active_;
            ESP_LOGD(TAG, "Vehicle is active, using active polling interval (%dms)", infotainment_interval);
        }
    }
    
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
    
    // Show polling intervals
    ESP_LOGCONFIG(TAG, "  Polling Intervals:");
    ESP_LOGCONFIG(TAG, "    VCSEC: %u ms", vcsec_poll_interval_);
    ESP_LOGCONFIG(TAG, "    Infotainment (awake): %u ms", infotainment_poll_interval_awake_);
    ESP_LOGCONFIG(TAG, "    Infotainment (active): %u ms", infotainment_poll_interval_active_);
    
    // Let state manager dump sensor config
    ESP_LOGCONFIG(TAG, "  Sensors configured:");
    // This would be implemented in state_manager_->dump_config()
}

// Configuration setters
void TeslaBLEVehicle::set_vin(const char *vin) {
    if (vin == nullptr) {
        ESP_LOGW(TAG, "Attempted to set null VIN - ignoring");
        return;
    }
    
    vin_ = std::string(vin);
    ESP_LOGD(TAG, "VIN set to: %s", vin_.c_str());
    
    // Only set in client if vehicle is initialized
    if (vehicle_) {
        vehicle_->set_vin(vin_);
        ESP_LOGD(TAG, "VIN configured in Tesla vehicle instance");
    } else {
        ESP_LOGD(TAG, "VIN stored for later configuration");
    }
}

void TeslaBLEVehicle::set_role(const std::string &role) {
    ESP_LOGD(TAG, "Setting role: %s", role.c_str());
    role_ = role;
}

void TeslaBLEVehicle::set_charging_amps_max(int amps_max) {
    ESP_LOGD(TAG, "Setting charging amps max: %d", amps_max);
    
    // Guard against invalid values - don't update if invalid
    if (amps_max <= 0) {
        ESP_LOGW(TAG, "Invalid charging amps max value: %d - ignoring update", amps_max);
        return;
    }
    
    if (state_manager_) {
        state_manager_->set_charging_amps_max(amps_max);
    }
}

// Polling interval setters
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

// Sensor setters (delegate to state manager)
void TeslaBLEVehicle::set_binary_sensor_is_asleep(binary_sensor::BinarySensor *s) {
    pending_asleep_sensor_ = s;
    if (state_manager_) state_manager_->set_asleep_sensor(s);
}

void TeslaBLEVehicle::set_binary_sensor_is_unlocked(binary_sensor::BinarySensor *s) {
    pending_unlocked_sensor_ = s;
    if (state_manager_) state_manager_->set_unlocked_sensor(s);
}

void TeslaBLEVehicle::set_binary_sensor_is_user_present(binary_sensor::BinarySensor *s) {
    pending_user_present_sensor_ = s;
    if (state_manager_) state_manager_->set_user_present_sensor(s);
}

void TeslaBLEVehicle::set_binary_sensor_is_charge_flap_open(binary_sensor::BinarySensor *s) {
    pending_charge_flap_sensor_ = s;
    if (state_manager_) state_manager_->set_charge_flap_sensor(s);
}

void TeslaBLEVehicle::set_binary_sensor_is_charger_connected(binary_sensor::BinarySensor *s) {
    pending_charger_sensor_ = s;
    if (state_manager_) state_manager_->set_charger_sensor(s);
}

void TeslaBLEVehicle::set_battery_level_sensor(sensor::Sensor *sensor) {
    pending_battery_level_sensor_ = sensor;
    if (state_manager_) state_manager_->set_battery_level_sensor(sensor);
}

void TeslaBLEVehicle::set_charger_power_sensor(sensor::Sensor *sensor) {
    pending_charger_power_sensor_ = sensor;
    if (state_manager_) state_manager_->set_charger_power_sensor(sensor);
}

void TeslaBLEVehicle::set_charger_voltage_sensor(sensor::Sensor *sensor) {
    pending_charger_voltage_sensor_ = sensor;
    if (state_manager_) state_manager_->set_charger_voltage_sensor(sensor);
}

void TeslaBLEVehicle::set_charger_current_sensor(sensor::Sensor *sensor) {
    pending_charger_current_sensor_ = sensor;
    if (state_manager_) state_manager_->set_charger_current_sensor(sensor);
}

void TeslaBLEVehicle::set_charging_rate_sensor(sensor::Sensor *sensor) {
    pending_charging_rate_sensor_ = sensor;
    if (state_manager_) state_manager_->set_charging_rate_sensor(sensor);
}

void TeslaBLEVehicle::set_charging_state_sensor(text_sensor::TextSensor *sensor) {
    pending_charging_state_sensor_ = sensor;
    if (state_manager_) state_manager_->set_charging_state_sensor(sensor);
}

void TeslaBLEVehicle::set_iec61851_state_sensor(text_sensor::TextSensor *sensor) {
    pending_iec61851_state_sensor_ = sensor;
    if (state_manager_) state_manager_->set_iec61851_state_sensor(sensor);
}

// Control setters (delegate to state manager)
void TeslaBLEVehicle::set_charging_switch(switch_::Switch *sw) {
    pending_charging_switch_ = sw;
    if (state_manager_) state_manager_->set_charging_switch(sw);
}

void TeslaBLEVehicle::set_charging_amps_number(number::Number *number) {
    pending_charging_amps_number_ = number;
    if (state_manager_) state_manager_->set_charging_amps_number(number);
}

void TeslaBLEVehicle::set_charging_limit_number(number::Number *number) {
    pending_charging_limit_number_ = number;
    if (state_manager_) state_manager_->set_charging_limit_number(number);
}

// Button setters
void TeslaBLEVehicle::set_wake_button(button::Button *button) {
    ESP_LOGD(TAG, "Setting wake button with parent pointer");
    // Cast to our custom button type and set parent
    TeslaWakeButton* wake_button = static_cast<TeslaWakeButton*>(button);
    if (wake_button) {
        wake_button->set_parent(this);
    }
}

void TeslaBLEVehicle::set_pair_button(button::Button *button) {
    ESP_LOGD(TAG, "Setting pair button with parent pointer");
    TeslaPairButton* pair_button = static_cast<TeslaPairButton*>(button);
    if (pair_button) {
        pair_button->set_parent(this);
    }
}

void TeslaBLEVehicle::set_regenerate_key_button(button::Button *button) {
    ESP_LOGD(TAG, "Setting regenerate key button with parent pointer");
    TeslaRegenerateKeyButton* regen_button = static_cast<TeslaRegenerateKeyButton*>(button);
    if (regen_button) {
        regen_button->set_parent(this);
    }
}

void TeslaBLEVehicle::set_force_update_button(button::Button *button) {
    ESP_LOGD(TAG, "Setting force update button with parent pointer");
    TeslaForceUpdateButton* update_button = static_cast<TeslaForceUpdateButton*>(button);
    if (update_button) {
        update_button->set_parent(this);
    }
}

// Public vehicle actions
int TeslaBLEVehicle::wake_vehicle() {
    ESP_LOGD(TAG, "Sending wake command");
    
    if (vehicle_) {
        vehicle_->wake();
        return 0;
    }
    return -1;
}

int TeslaBLEVehicle::start_pairing() {
    ESP_LOGI(TAG, "Pairing requested");
    
    if (!vehicle_) {
        ESP_LOGE(TAG, "Vehicle instance not available");
        return -1;
    }
    
    // Parse role e.g. "OWNER", "DRIVER"
    // The library expects Keys_Role enum from keys.pb.h
    Keys_Role role_enum = Keys_Role_ROLE_OWNER;
    if (role_ == "DRIVER") {
        role_enum = Keys_Role_ROLE_DRIVER;
    } else if (role_ != "OWNER") {
        ESP_LOGW(TAG, "Unknown role %s, defaulting to OWNER", role_.c_str());
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
         vehicle_->wake(); // Wake ensures recent data is fetched
         vehicle_->vcsec_poll();
         vehicle_->infotainment_poll();
    }
}

// Vehicle control actions
int TeslaBLEVehicle::set_charging_state(bool charging) {
    ESP_LOGI(TAG, "Set charging state: %s", charging ? "ON" : "OFF");
    
    // Track command to delay INFOTAINMENT requests
    if (state_manager_) {
        state_manager_->track_command_issued();
    }
    
    if (!vehicle_) {
        ESP_LOGE(TAG, "Vehicle instance not available");
        return -1;
    }
    
    vehicle_->set_charging_state(charging);
    return 0;
}

int TeslaBLEVehicle::set_charging_amps(int amps) {
    ESP_LOGI(TAG, "Set charging amps: %d", amps);
    
    // Basic validation
    if (amps < 0) {
        ESP_LOGW(TAG, "Invalid charging amps: %d (cannot be negative)", amps);
        return -1;
    }
    
    // Validate against max amps
    int max_amps = state_manager_->get_charging_amps_max();
    if (amps > max_amps) {
        ESP_LOGW(TAG, "Requested amps (%d) exceeds maximum (%d), clamping", amps, max_amps);
        amps = max_amps;
    }
    
    // Track this user change to prevent immediate overwrites from stale vehicle data
    if (state_manager_) {
        state_manager_->track_command_issued();
    }
    
    if (!vehicle_) {
        ESP_LOGE(TAG, "Vehicle instance not available");
        return -1;
    }
    
    vehicle_->set_charging_amps(amps);
    return 0;
}

int TeslaBLEVehicle::set_charging_limit(int limit) {
    ESP_LOGI(TAG, "Set charging limit: %d%%", limit);
    
    // Validate limit range
    if (limit < MIN_CHARGING_LIMIT || limit > MAX_CHARGING_LIMIT) {
        ESP_LOGW(TAG, "Invalid charging limit: %d%%, must be %d-%d%%", 
                 limit, MIN_CHARGING_LIMIT, MAX_CHARGING_LIMIT);
        return -1;
    }
    
    // Track this user change to prevent immediate overwrites from stale vehicle data
    if (state_manager_) {
        state_manager_->track_command_issued();
    }
    
    if (!vehicle_) {
        ESP_LOGE(TAG, "Vehicle instance not available");
        return -1;
    }
    
    vehicle_->set_charging_limit(limit);
    return 0;
}

// Data request actions
void TeslaBLEVehicle::request_vehicle_data() {
    ESP_LOGD(TAG, "Vehicle data requested");
    
    if (vehicle_) {
        vehicle_->infotainment_poll();
    }
}

void TeslaBLEVehicle::request_charging_data() {
    ESP_LOGD(TAG, "Requesting charging data from infotainment");
    
    if (vehicle_) {
        vehicle_->infotainment_poll();
    }
}

void TeslaBLEVehicle::unlock_charge_port() {
    ESP_LOGI(TAG, "Unlock/Open charge port requested");
    if (state_manager_) {
        state_manager_->track_command_issued();
    }
    if (vehicle_) {
        vehicle_->unlock_charge_port();
    }
}
void TeslaBLEVehicle::update_charging_amps_max_value(int32_t new_max) {
    // This method is called by VehicleStateManager when it needs to update max amps
    // but doesn't have access to the Tesla-specific types
    
    // Find the charging amps number component - we know it's our Tesla type
    if (pending_charging_amps_number_) {
        // Cast to our known type - this is safe since we control the creation
        auto* tesla_amps = static_cast<TeslaChargingAmpsNumber*>(pending_charging_amps_number_);
        tesla_amps->update_max_value(new_max);
        ESP_LOGD(TAG, "Updated charging amps max value to %d A", new_max);
    } else {
        ESP_LOGW(TAG, "Charging amps number component not available for max value update");
    }
}

// BLE event handling
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
            // conn_id is managed by BLEClient
            this->read_handle_ = 0;
            this->write_handle_ = 0;
            this->node_state = espbt::ClientState::DISCONNECTING;
            break;
            
        case ESP_GATTC_SEARCH_CMPL_EVT: {
            // Setup read characteristic
            auto *readChar = this->parent()->get_characteristic(this->service_uuid_, this->read_uuid_);
            if (readChar == nullptr) {
                ESP_LOGE(TAG, "Read characteristic not found");
                break;
            }
            this->read_handle_ = readChar->handle;
            
            // Register for notifications
            auto reg_status = esp_ble_gattc_register_for_notify(
                this->parent()->get_gattc_if(), 
                this->parent()->get_remote_bda(), 
                readChar->handle);
            if (reg_status) {
                ESP_LOGE(TAG, "Failed to register for notifications: %d", reg_status);
            }
            
            // Setup write characteristic
            auto *writeChar = this->parent()->get_characteristic(this->service_uuid_, this->write_uuid_);
            if (writeChar == nullptr) {
                ESP_LOGE(TAG, "Write characteristic not found");
                break;
            }
            this->write_handle_ = writeChar->handle;
            
            ESP_LOGD(TAG, "BLE characteristics configured");
            break;
        }
            
        case ESP_GATTC_REG_FOR_NOTIFY_EVT:
            if (param->reg_for_notify.status != ESP_GATT_OK) {
                ESP_LOGE(TAG, "Failed to register for notifications");
                break;
            }
            
            this->node_state = espbt::ClientState::ESTABLISHED;
            ESP_LOGI(TAG, "BLE connection fully established");
            
            // Now that characteristics are ready and notifications are enabled, trigger connection handling
            handle_connection_established();
            break;
            
        case ESP_GATTC_NOTIFY_EVT: {
            // Handle incoming data
            if (param->notify.conn_id != this->parent()->get_conn_id()) {
                break;
            }
            
            std::vector<unsigned char> data(
                param->notify.value, 
                param->notify.value + param->notify.value_len);
            
            if (vehicle_) {
                vehicle_->on_rx_data(data);
            }
            break;
        }
            
        case ESP_GATTC_WRITE_CHAR_EVT:
            if (param->write.status != ESP_GATT_OK) {
                ESP_LOGW(TAG, "BLE write failed: %d", param->write.status);
            }
            break;
            
        default:
            ESP_LOGV(TAG, "Unhandled GATTC event: %d", event);
            break;
    }
}

void TeslaBLEVehicle::handle_connection_established() {
    if (vehicle_) {
        vehicle_->set_connected(true);
        
        // Polling will happen in update() cycle
        // Set last_infotainment_poll_ to 0 to signal that an infotainment poll is needed as soon as vehicle is awake
        last_infotainment_poll_ = 0;
        
        // Trigger initial VCSEC poll for sleep status
        ESP_LOGI(TAG, "Connection established - triggering initial VCSEC poll");
        vehicle_->vcsec_poll();
        last_vcsec_poll_ = millis();
    }
    
    if (state_manager_) {
        state_manager_->set_sensors_available(true);
    }
    
    this->status_clear_warning();
}

void TeslaBLEVehicle::handle_connection_lost() {
    if (vehicle_) {
        vehicle_->set_connected(false);
    }
    
    if (ble_adapter_) {
        ble_adapter_->clear_queues();
    }
    
    // Reset initial poll times
    last_infotainment_poll_ = 0;
    last_vcsec_poll_ = 0;
    
    this->status_set_warning("BLE connection lost");
}

// Button implementations (simplified)
void TeslaWakeButton::press_action() {
    if (parent_) parent_->wake_vehicle();
}

void TeslaPairButton::press_action() {
    if (parent_) parent_->start_pairing();
}

void TeslaRegenerateKeyButton::press_action() {
    if (parent_) parent_->regenerate_key();
}

void TeslaForceUpdateButton::press_action() {
    if (parent_) parent_->force_update();
}

void TeslaUnlockChargePortButton::press_action() {
    if (parent_) parent_->unlock_charge_port();
}
void TeslaChargingSwitch::write_state(bool state) {
    if (parent_) {
        parent_->set_charging_state(state);
        publish_state(state);
    }
}

void TeslaChargingAmpsNumber::control(float value) {
    if (!parent_) {
        ESP_LOGW(TAG, "TeslaChargingAmpsNumber: parent not set");
        return;
    }
    
    // Additional bounds checking beyond what ESPHome provides
    float min_val = this->traits.get_min_value();
    float max_val = this->traits.get_max_value();
    
    if (value < min_val || value > max_val) {
        ESP_LOGW(TAG, "Charging amps value %.1f out of bounds [%.1f, %.1f]", value, min_val, max_val);
        return;
    }
    
    ESP_LOGD(TAG, "Setting charging amps to %.0f A", value);
    parent_->set_charging_amps(static_cast<int>(value));
    publish_state(value);
}

void TeslaChargingAmpsNumber::update_max_value(int32_t new_max) {
    // Skip update if new_max is 0 or invalid - likely not ready or invalid value
    if (new_max <= 0) {
        ESP_LOGV(TAG, "Skipping charging amps max update - invalid value: %d A", new_max);
        return;
    }
    
    auto old_max = this->traits.get_max_value();
    
    if (std::abs(old_max - new_max) > 0.1f) {
        ESP_LOGD(TAG, "Updating charging amps max from %.0f to %.0f A", old_max, new_max);
        
        // Update the traits
        this->traits.set_max_value(new_max);
        
        // Clamp current value if it exceeds new max
        if (this->has_state() && this->state > new_max) {
            ESP_LOGD(TAG, "Clamping current value from %.0f to %.0f A", this->state, new_max);
            this->publish_state(new_max);
        }
        
        ESP_LOGW(TAG, "Max charging amps updated to %.0f A - you may need to restart the ESPHome device or reload the ESPHome integration in Home Assistant to see the updated UI limit", new_max);
        
        // Republish current state to ensure it's visible
        if (this->has_state()) {
            this->publish_state(this->state);
        }
    }
}

void TeslaChargingLimitNumber::control(float value) {
    if (!parent_) {
        ESP_LOGW(TAG, "TeslaChargingLimitNumber: parent not set");
        return;
    }
    
    // Additional bounds checking beyond what ESPHome provides
    float min_val = this->traits.get_min_value();
    float max_val = this->traits.get_max_value();
    
    if (value < min_val || value > max_val) {
        ESP_LOGW(TAG, "Charging limit value %.1f out of bounds [%.1f, %.1f]", value, min_val, max_val);
        return;
    }
    
    ESP_LOGD(TAG, "Setting charging limit to %.0f%%", value);
    parent_->set_charging_limit(static_cast<int>(value));
    publish_state(value);
}

} // namespace tesla_ble_vehicle
} // namespace esphome
