#include "vehicle_state_manager.h"
#include "tesla_ble_vehicle.h"
#include <esphome/core/helpers.h>
#include <cmath>
#include <algorithm>

namespace esphome {
namespace tesla_ble_vehicle {

VehicleStateManager::VehicleStateManager(TeslaBLEVehicle* parent)
    : parent_(parent), is_charging_(false), charging_amps_max_(32) {}

void VehicleStateManager::update_vehicle_status(const VCSEC_VehicleStatus& status) {
    ESP_LOGD(STATE_MANAGER_TAG, "Updating vehicle status");
    
    update_sleep_status(status.vehicleSleepStatus);
    update_lock_status(status.vehicleLockState);
    update_user_presence(status.userPresence);
    
    // Update charge flap if present (from closure statuses)
    if (status.has_closureStatuses && charge_flap_sensor_) {
        bool flap_open = (status.closureStatuses.chargePort == VCSEC_ClosureState_E_CLOSURESTATE_OPEN);
        update_charge_flap_open(flap_open);
    }
}

void VehicleStateManager::update_sleep_status(VCSEC_VehicleSleepStatus_E status) {
    auto asleep = convert_sleep_status(status);
    if (asleep.has_value()) {
        update_asleep(asleep.value());
    } else {
        set_sensor_available(asleep_sensor_, false);
    }
}

void VehicleStateManager::update_lock_status(VCSEC_VehicleLockState_E status) {
    auto unlocked = convert_lock_status(status);
    if (unlocked.has_value()) {
        update_unlocked(unlocked.value());
    } else {
        set_sensor_available(unlocked_sensor_, false);
    }
}

void VehicleStateManager::update_user_presence(VCSEC_UserPresence_E presence) {
    auto present = convert_user_presence(presence);
    if (present.has_value()) {
        update_user_present(present.value());
    } else {
        set_sensor_available(user_present_sensor_, false);
    }
}

void VehicleStateManager::update_charge_state(const CarServer_ChargeState& charge_state) {
    ESP_LOGD(STATE_MANAGER_TAG, "Updating charge state");
    
    // Update charging status and charging state text
    if (charge_state.has_charging_state) {
        bool was_charging = is_charging_;
        
        // Determine if vehicle is actively charging based on state type
        // Starting state is considered charging since it's transitioning to charge
        bool new_charging_state = (
            charge_state.charging_state.which_type == CarServer_ChargeState_ChargingState_Charging_tag ||
            charge_state.charging_state.which_type == CarServer_ChargeState_ChargingState_Starting_tag
        );
        
        ESP_LOGD(STATE_MANAGER_TAG, "Charging state check: was=%s, new=%s, state_type=%d", 
                 was_charging ? "ON" : "OFF", 
                 new_charging_state ? "ON" : "OFF",
                 charge_state.charging_state.which_type);
        
        is_charging_ = new_charging_state;
        
        // Always sync charging switch with vehicle state, but respect command delay
        // This prevents race conditions where user commands are overwritten by stale vehicle data
        if (charging_switch_ && (!charging_switch_->has_state() || charging_switch_->state != is_charging_)) {
            if (should_delay_infotainment_request()) {
                ESP_LOGD(STATE_MANAGER_TAG, "Delaying charging switch sync due to recent command (vehicle: %s, switch: %s)", 
                         is_charging_ ? "ON" : "OFF", 
                         charging_switch_->state ? "ON" : "OFF");
            } else {
                ESP_LOGD(STATE_MANAGER_TAG, "Syncing charging switch to vehicle state: %s", is_charging_ ? "ON" : "OFF");
                publish_sensor_state(charging_switch_, is_charging_);
            }
        }
        
        if (was_charging != is_charging_) {
            ESP_LOGD(STATE_MANAGER_TAG, "Charging state changed: %s", is_charging_ ? "ON" : "OFF");
        } else {
            ESP_LOGV(STATE_MANAGER_TAG, "Charging state unchanged: %s", is_charging_ ? "ON" : "OFF");
        }
        
        // Update charging state text sensor
        if (charging_state_sensor_) {
            std::string state_text = get_charging_state_text(charge_state.charging_state);
            publish_sensor_state(charging_state_sensor_, state_text);
        }
        
        // Update charger connected binary sensor based on charging state
        if (charger_sensor_) {
            bool charger_connected = is_charger_connected_from_state(charge_state.charging_state);
            publish_sensor_state(charger_sensor_, charger_connected);
        }
    }
    
    // Update battery level with validation
    if (charge_state.which_optional_battery_level && battery_level_sensor_) {
        float battery_level = static_cast<float>(charge_state.optional_battery_level.battery_level);
        
        // Validate battery level is within reasonable bounds [0-100]
        if (battery_level >= 0.0f && battery_level <= 100.0f) {
            ESP_LOGD(STATE_MANAGER_TAG, "Updating battery level to %.1f%%", battery_level);
            publish_sensor_state(battery_level_sensor_, battery_level);
        } else {
            ESP_LOGW(STATE_MANAGER_TAG, "Invalid battery level received: %.1f%% (expected 0-100)", battery_level);
        }
    }
    
    // Calculate charger power using voltage × current for more precision, or fall back to direct power reading
    if (charger_power_sensor_) {
        float calculated_power_w = 0.0f;
        bool has_calculated_power = false;
        
        // Try to calculate power from voltage × current for better precision
        if (charge_state.which_optional_charger_voltage && charge_state.which_optional_charger_actual_current) {
            float voltage = static_cast<float>(charge_state.optional_charger_voltage.charger_voltage);
            float current = static_cast<float>(charge_state.optional_charger_actual_current.charger_actual_current);

            // Calculate power in watts (voltage * current)
            calculated_power_w = (voltage * current);
            has_calculated_power = true;

            ESP_LOGD(STATE_MANAGER_TAG, "Calculated charger power: %.1fV × %.1fA = %.0fW", voltage, current, calculated_power_w);
        }
        // Fall back to direct power reading if calculation not available
        else if (charge_state.which_optional_charger_power) {
            calculated_power_w = static_cast<float>(charge_state.optional_charger_power.charger_power) / 1000.0f;
            has_calculated_power = true;

            ESP_LOGD(STATE_MANAGER_TAG, "Using direct charger power reading: %.0fW", calculated_power_w);
        }
        
        // Update power sensor with validation
        if (has_calculated_power) {
            // Validate power is non-negative (Tesla max is ~250kW at Superchargers)
            if (calculated_power_w >= 0.0f && calculated_power_w <= 300000.0f) {
                ESP_LOGD(STATE_MANAGER_TAG, "Updating charger power to %.3fW", calculated_power_w);
                publish_sensor_state(charger_power_sensor_, calculated_power_w);
            } else {
                ESP_LOGW(STATE_MANAGER_TAG, "Invalid charger power calculated/received: %.3fW (expected 0-300000)", calculated_power_w);
            }
        }
    }
    
    // Update charger voltage with validation
    if (charge_state.which_optional_charger_voltage && charger_voltage_sensor_) {
        float voltage = static_cast<float>(charge_state.optional_charger_voltage.charger_voltage);
        
        // Validate voltage (typical range 100-500V for Tesla chargers)
        if (voltage >= 0.0f && voltage <= 600.0f) {
            ESP_LOGD(STATE_MANAGER_TAG, "Updating charger voltage to %.1fV", voltage);
            publish_sensor_state(charger_voltage_sensor_, voltage);
        } else {
            ESP_LOGW(STATE_MANAGER_TAG, "Invalid charger voltage received: %.1fV (expected 0-600)", voltage);
        }
    }
    
    // Update charger current with validation
    if (charge_state.which_optional_charger_actual_current && charger_current_sensor_) {
        float current = static_cast<float>(charge_state.optional_charger_actual_current.charger_actual_current);
        
        // Validate current (typical range 0-80A for Tesla chargers)
        if (current >= 0.0f && current <= 100.0f) {
            ESP_LOGD(STATE_MANAGER_TAG, "Updating charger current to %.1fA", current);
            publish_sensor_state(charger_current_sensor_, current);
        } else {
            ESP_LOGW(STATE_MANAGER_TAG, "Invalid charger current received: %.1fA (expected 0-100)", current);
        }
    }
    
    // Update charging rate
    if (charge_state.which_optional_charge_rate_mph && charging_rate_sensor_) {
        float rate_mph = static_cast<float>(charge_state.optional_charge_rate_mph.charge_rate_mph);
        publish_sensor_state(charging_rate_sensor_, rate_mph);
    }

    // Update charging amps (from charger actual current)
    if (charge_state.which_optional_charger_actual_current && charging_amps_number_) {
        float amps = static_cast<float>(charge_state.optional_charger_actual_current.charger_actual_current);
        update_charging_amps(amps);
    }
    
    // Update charge limit - update both sensor (read-only) and number (user-controllable)
    if (charge_state.which_optional_charge_limit_soc) {
        float limit = static_cast<float>(charge_state.optional_charge_limit_soc.charge_limit_soc);
        
        // Update the user-controllable number component (with command delay protection)
        if (charging_limit_number_) {
            // Check if we should delay this update due to recent user command
            if (should_delay_infotainment_request()) {
                ESP_LOGD(STATE_MANAGER_TAG, "Delaying charging limit update (%.0f%%) due to recent command", limit);
            } else {
                ESP_LOGD(STATE_MANAGER_TAG, "Updating charging limit number to %.0f%%", limit);
                publish_sensor_state(charging_limit_number_, limit);
            }
        }
    }
    
    // Update max charging amps if available
    if (charge_state.which_optional_charge_current_request_max) {
        int32_t new_max = charge_state.optional_charge_current_request_max.charge_current_request_max;
        ESP_LOGD(STATE_MANAGER_TAG, "Received max charging amps from vehicle: %d A (current stored: %d A)", new_max, charging_amps_max_);
        
        // Skip update if new_max is 0 or invalid - likely not ready or invalid value from vehicle
        if (new_max <= 0) {
            ESP_LOGV(STATE_MANAGER_TAG, "Skipping max charging amps update - invalid value from vehicle: %d A", new_max);
        } else if (new_max != charging_amps_max_) {
            update_charging_amps_max(new_max);
        } else {
            ESP_LOGV(STATE_MANAGER_TAG, "Max charging amps unchanged: %d A", new_max);
        }
    } else {
        ESP_LOGV(STATE_MANAGER_TAG, "No max charging amps data in charge state");
    }
    
    // Update charge flap status if available
    if (charge_state.which_optional_charge_port_door_open) {
        update_charge_flap_open(charge_state.optional_charge_port_door_open.charge_port_door_open);
    }
}

void VehicleStateManager::update_climate_state(const CarServer_ClimateState& climate_state) {
    ESP_LOGD(STATE_MANAGER_TAG, "Updating climate state");
    // Future implementation for climate sensors
}

void VehicleStateManager::update_drive_state(const CarServer_DriveState& drive_state) {
    ESP_LOGD(STATE_MANAGER_TAG, "Updating drive state");
    // Future implementation for drive sensors
}

// Direct state update methods
void VehicleStateManager::update_asleep(bool asleep) {
    ESP_LOGD(STATE_MANAGER_TAG, "Vehicle sleep state: %s", asleep ? "ASLEEP" : "AWAKE");
    publish_sensor_state(asleep_sensor_, asleep);
    
    // Notify polling manager of state change
    if (parent_->get_polling_manager()) {
        parent_->get_polling_manager()->update_vehicle_state(!asleep, is_charging_, is_unlocked(), is_user_present_);
    }
}

void VehicleStateManager::update_unlocked(bool unlocked) {
    ESP_LOGD(STATE_MANAGER_TAG, "Vehicle lock state: %s", unlocked ? "UNLOCKED" : "LOCKED");
    publish_sensor_state(unlocked_sensor_, unlocked);
    
    // Notify polling manager of state change
    if (parent_->get_polling_manager()) {
        parent_->get_polling_manager()->update_vehicle_state(!is_asleep(), is_charging_, unlocked, is_user_present_);
    }
}

void VehicleStateManager::update_user_present(bool present) {
    ESP_LOGD(STATE_MANAGER_TAG, "User presence: %s", present ? "PRESENT" : "NOT_PRESENT");
    publish_sensor_state(user_present_sensor_, present);
    
    is_user_present_ = present;
    
    // Notify polling manager of state change
    if (parent_->get_polling_manager()) {
        parent_->get_polling_manager()->update_vehicle_state(!is_asleep(), is_charging_, is_unlocked(), is_user_present_);
    }
}

void VehicleStateManager::update_charge_flap_open(bool open) {
    ESP_LOGV(STATE_MANAGER_TAG, "Charge flap: %s", open ? "OPEN" : "CLOSED");
    publish_sensor_state(charge_flap_sensor_, open);
}

void VehicleStateManager::update_charging_amps(float amps) {
    ESP_LOGV(STATE_MANAGER_TAG, "Charging amps from vehicle: %.1f A", amps);
    
    // Always update the number component (since we're using delay-based approach)
    publish_sensor_state(charging_amps_number_, amps);
}

// Connection state management
void VehicleStateManager::set_sensors_available(bool available) {
    ESP_LOGD(STATE_MANAGER_TAG, "Setting sensors available: %s", available ? "true" : "false");
    
    set_sensor_available(asleep_sensor_, available);
    set_sensor_available(unlocked_sensor_, available);
    set_sensor_available(user_present_sensor_, available);
    set_sensor_available(charge_flap_sensor_, available);
    
    // For controls, set availability but don't change state
    if (charging_switch_) {
        // Switch availability is managed differently
    }
    if (charging_amps_number_) {
        // Number availability is managed differently  
    }
    if (charging_limit_number_) {
        // Number availability is managed differently
    }
}

void VehicleStateManager::reset_all_states() {
    ESP_LOGD(STATE_MANAGER_TAG, "Resetting all vehicle states");
    
    is_charging_ = false;
    
    // Reset all sensors to unavailable
    set_sensors_available(false);
}

// State queries
bool VehicleStateManager::is_asleep() const {
    return asleep_sensor_ ? asleep_sensor_->state : true; // Default to asleep if unknown
}

bool VehicleStateManager::is_unlocked() const {
    return unlocked_sensor_ ? unlocked_sensor_->state : false; // Default to locked if unknown
}

bool VehicleStateManager::is_user_present() const {
    return user_present_sensor_ ? user_present_sensor_->state : false;
}

bool VehicleStateManager::is_charge_flap_open() const {
    return charge_flap_sensor_ ? charge_flap_sensor_->state : false;
}

float VehicleStateManager::get_charging_amps() const {
    return charging_amps_number_ ? charging_amps_number_->state : 0.0f;
}

// Dynamic limits
void VehicleStateManager::update_charging_amps_max(int32_t new_max) {
    // Additional safety check - should not happen since callers validate, but be defensive
    if (new_max <= 0) {
        ESP_LOGW(STATE_MANAGER_TAG, "Invalid max charging amps value: %d A - ignoring update", new_max);
        return;
    }
    
    int32_t old_max = charging_amps_max_;
    
    // Update stored value
    charging_amps_max_ = new_max;
    
    // Update the number component's maximum value via the parent (which knows about Tesla types)
    if (charging_amps_number_ && old_max != new_max) {
        auto old_trait_max = charging_amps_number_->traits.get_max_value();
        
        // Ask the parent to update the max value since it knows about Tesla-specific types
        if (parent_) {
            parent_->update_charging_amps_max_value(new_max);
            ESP_LOGD(STATE_MANAGER_TAG, "Updated max charging amps from %.0f to %d A via parent", old_trait_max, new_max);
        } else {
            ESP_LOGW(STATE_MANAGER_TAG, "Parent not available to update max charging amps");
        }
    } else {
        ESP_LOGD(STATE_MANAGER_TAG, "Max charging amps set to %d A (no component to update)", new_max);
    }
}

// Private helper methods
void VehicleStateManager::publish_sensor_state(binary_sensor::BinarySensor* sensor, bool state) {
    if (sensor != nullptr && (!sensor->has_state() || sensor->state != state)) {
        sensor->publish_state(state);
    }
}

void VehicleStateManager::publish_sensor_state(sensor::Sensor* sensor, float state) {
    if (sensor != nullptr && (!sensor->has_state() || std::abs(sensor->state - state) > 0.001f)) {
        sensor->publish_state(state);
    }
}

void VehicleStateManager::publish_sensor_state(switch_::Switch* switch_comp, bool state) {
    if (switch_comp != nullptr && (!switch_comp->has_state() || switch_comp->state != state)) {
        switch_comp->publish_state(state);
    }
}

void VehicleStateManager::publish_sensor_state(number::Number* number_comp, float state) {
    if (number_comp != nullptr && (!number_comp->has_state() || std::abs(number_comp->state - state) > 0.001f)) {
        number_comp->publish_state(state);
    }
}

void VehicleStateManager::set_sensor_available(binary_sensor::BinarySensor* sensor, bool available) {
    if (sensor != nullptr) {
        sensor->set_has_state(available);
    }
}

void VehicleStateManager::set_sensor_available(sensor::Sensor* sensor, bool available) {
    if (sensor != nullptr) {
        sensor->set_has_state(available);
    }
}

// State conversion helpers
std::optional<bool> VehicleStateManager::convert_sleep_status(VCSEC_VehicleSleepStatus_E status) {
    switch (status) {
        case VCSEC_VehicleSleepStatus_E_VEHICLE_SLEEP_STATUS_AWAKE:
            return false; // Not asleep
        case VCSEC_VehicleSleepStatus_E_VEHICLE_SLEEP_STATUS_ASLEEP:
            return true;  // Asleep
        case VCSEC_VehicleSleepStatus_E_VEHICLE_SLEEP_STATUS_UNKNOWN:
        default:
            return std::nullopt;   // Unknown state
    }
}

std::optional<bool> VehicleStateManager::convert_lock_status(VCSEC_VehicleLockState_E status) {
    switch (status) {
        case VCSEC_VehicleLockState_E_VEHICLELOCKSTATE_UNLOCKED:
        case VCSEC_VehicleLockState_E_VEHICLELOCKSTATE_SELECTIVE_UNLOCKED:
            return true;  // Unlocked
        case VCSEC_VehicleLockState_E_VEHICLELOCKSTATE_LOCKED:
        case VCSEC_VehicleLockState_E_VEHICLELOCKSTATE_INTERNAL_LOCKED:
            return false; // Locked
        default:
            return std::nullopt;   // Unknown state
    }
}

std::optional<bool> VehicleStateManager::convert_user_presence(VCSEC_UserPresence_E presence) {
    switch (presence) {
        case VCSEC_UserPresence_E_VEHICLE_USER_PRESENCE_PRESENT:
            return true;  // Present
        case VCSEC_UserPresence_E_VEHICLE_USER_PRESENCE_NOT_PRESENT:
            return false; // Not present
        case VCSEC_UserPresence_E_VEHICLE_USER_PRESENCE_UNKNOWN:
        default:
            return std::nullopt;   // Unknown state
    }
}

void VehicleStateManager::publish_sensor_state(text_sensor::TextSensor* sensor, const std::string& state) {
    if (sensor != nullptr && (!sensor->has_state() || sensor->state != state)) {
        sensor->publish_state(state);
    }
}

std::string VehicleStateManager::get_charging_state_text(const CarServer_ChargeState_ChargingState& state) {
    switch (state.which_type) {
        case CarServer_ChargeState_ChargingState_Unknown_tag:
            return "Unknown";
        case CarServer_ChargeState_ChargingState_Disconnected_tag:
            return "Disconnected";
        case CarServer_ChargeState_ChargingState_NoPower_tag:
            return "No Power";
        case CarServer_ChargeState_ChargingState_Starting_tag:
            return "Starting";
        case CarServer_ChargeState_ChargingState_Charging_tag:
            return "Charging";
        case CarServer_ChargeState_ChargingState_Complete_tag:
            return "Complete";
        case CarServer_ChargeState_ChargingState_Stopped_tag:
            return "Stopped";
        case CarServer_ChargeState_ChargingState_Calibrating_tag:
            return "Calibrating";
        default:
            return "Unknown";
    }
}

bool VehicleStateManager::is_charger_connected_from_state(const CarServer_ChargeState_ChargingState& state) {
    // Charger is considered connected for all states except Disconnected and Unknown
    switch (state.which_type) {
        case CarServer_ChargeState_ChargingState_Disconnected_tag:
        case CarServer_ChargeState_ChargingState_Unknown_tag:
            return false;
        case CarServer_ChargeState_ChargingState_NoPower_tag:
        case CarServer_ChargeState_ChargingState_Starting_tag:
        case CarServer_ChargeState_ChargingState_Charging_tag:
        case CarServer_ChargeState_ChargingState_Complete_tag:
        case CarServer_ChargeState_ChargingState_Stopped_tag:
        case CarServer_ChargeState_ChargingState_Calibrating_tag:
            return true;
        default:
            return false;
    }
}

void VehicleStateManager::update_charger_connected(bool connected) {
    publish_sensor_state(charger_sensor_, connected);
}

// Command tracking for INFOTAINMENT request delay
void VehicleStateManager::track_command_issued() {
    last_command_time_ = millis();
    ESP_LOGD(STATE_MANAGER_TAG, "Command issued - will delay INFOTAINMENT requests for %dms", COMMAND_DELAY_TIME);
}

bool VehicleStateManager::should_delay_infotainment_request() const {
    uint32_t now = millis();
    uint32_t time_since_command = now - last_command_time_;
    
    bool should_delay = time_since_command < COMMAND_DELAY_TIME;
    if (should_delay) {
        ESP_LOGV(STATE_MANAGER_TAG, "Delaying INFOTAINMENT request (%dms since last command)", time_since_command);
    }
    return should_delay;
}

} // namespace tesla_ble_vehicle
} // namespace esphome
