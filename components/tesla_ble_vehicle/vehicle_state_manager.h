#pragma once

#include <esphome/core/log.h>
#include <esphome/components/binary_sensor/binary_sensor.h>
#include <esphome/components/sensor/sensor.h>
#include <esphome/components/text_sensor/text_sensor.h>
#include <esphome/components/switch/switch.h>
#include <esphome/components/number/number.h>
#include <car_server.pb.h>
#include <vcsec.pb.h>

namespace esphome {
namespace tesla_ble_vehicle {

static const char *const STATE_MANAGER_TAG = "tesla_state_manager";

// Forward declarations
class TeslaBLEVehicle;

/**
 * @brief Vehicle state manager
 * 
 * This class manages the vehicle's state including sensors, switches, and numbers.
 * It handles updates from vehicle data and provides a centralized interface for
 * state management.
 */
class VehicleStateManager {
public:
    explicit VehicleStateManager(TeslaBLEVehicle* parent);
    
    // Sensor setters
    void set_asleep_sensor(binary_sensor::BinarySensor* sensor) { asleep_sensor_ = sensor; }
    void set_unlocked_sensor(binary_sensor::BinarySensor* sensor) { unlocked_sensor_ = sensor; }
    void set_user_present_sensor(binary_sensor::BinarySensor* sensor) { user_present_sensor_ = sensor; }
    void set_charge_flap_sensor(binary_sensor::BinarySensor* sensor) { charge_flap_sensor_ = sensor; }
    void set_charger_sensor(binary_sensor::BinarySensor* sensor) { charger_sensor_ = sensor; }
    void set_battery_level_sensor(sensor::Sensor* sensor) { battery_level_sensor_ = sensor; }
    void set_charger_power_sensor(sensor::Sensor* sensor) { charger_power_sensor_ = sensor; }
    void set_charging_rate_sensor(sensor::Sensor* sensor) { charging_rate_sensor_ = sensor; }
    void set_charging_state_sensor(text_sensor::TextSensor* sensor) { charging_state_sensor_ = sensor; }
    
    // Control setters
    void set_charging_switch(switch_::Switch* sw) { charging_switch_ = sw; }
    void set_charging_amps_number(number::Number* number) { charging_amps_number_ = number; }
    void set_charging_limit_number(number::Number* number) { charging_limit_number_ = number; }
    
    // State updates from VCSEC
    void update_vehicle_status(const VCSEC_VehicleStatus& status);
    void update_sleep_status(VCSEC_VehicleSleepStatus_E status);
    void update_lock_status(VCSEC_VehicleLockState_E status);
    void update_user_presence(VCSEC_UserPresence_E presence);
    
    // State updates from CarServer
    void update_charge_state(const CarServer_ChargeState& charge_state);
    void update_climate_state(const CarServer_ClimateState& climate_state);
    void update_drive_state(const CarServer_DriveState& drive_state);
    
    // Direct state updates
    void update_asleep(bool asleep);
    void update_unlocked(bool unlocked);
    void update_user_present(bool present);
    void update_charge_flap_open(bool open);
    void update_charging_amps(float amps);
    void update_charger_connected(bool connected);
    
    // Connection state management
    void set_sensors_available(bool available);
    void reset_all_states();
    
    // State queries
    bool is_asleep() const;
    bool is_unlocked() const;
    bool is_user_present() const;
    bool is_charge_flap_open() const;
    bool is_charging() const { return is_charging_; }
    float get_charging_amps() const;
    
    // Dynamic limits
    void update_charging_amps_max(int32_t new_max);
    int get_charging_amps_max() const { return charging_amps_max_; }
    void set_charging_amps_max(int max) { charging_amps_max_ = max; }
    
    // Command tracking for INFOTAINMENT request delay
    void track_command_issued();
    bool should_delay_infotainment_request() const;
    
private:
    TeslaBLEVehicle* parent_;
    
    // Binary sensors
    binary_sensor::BinarySensor* asleep_sensor_{nullptr};
    binary_sensor::BinarySensor* unlocked_sensor_{nullptr};
    binary_sensor::BinarySensor* user_present_sensor_{nullptr};
    binary_sensor::BinarySensor* charge_flap_sensor_{nullptr};
    binary_sensor::BinarySensor* charger_sensor_{nullptr};
    
    // Sensors
    sensor::Sensor* battery_level_sensor_{nullptr};
    sensor::Sensor* charger_power_sensor_{nullptr};
    sensor::Sensor* charging_rate_sensor_{nullptr};
    
    // Text sensors
    text_sensor::TextSensor* charging_state_sensor_{nullptr};
    
    // Controls
    switch_::Switch* charging_switch_{nullptr};
    number::Number* charging_amps_number_{nullptr};
    number::Number* charging_limit_number_{nullptr};
    
    // Internal state tracking
    bool is_charging_{false};
    int charging_amps_max_{32};
    
    // Command delay tracking - prevents stale data from overwriting fresh user commands
    // by delaying INFOTAINMENT requests (polling, force updates, etc.) after commands
    uint32_t last_command_time_{0};
    static const uint32_t COMMAND_DELAY_TIME = 3000; // 3 seconds delay after command
    
    // Helper methods
    void publish_sensor_state(binary_sensor::BinarySensor* sensor, bool state);
    void publish_sensor_state(sensor::Sensor* sensor, float state);
    void publish_sensor_state(switch_::Switch* switch_comp, bool state);
    void publish_sensor_state(number::Number* number_comp, float state);
    void publish_sensor_state(text_sensor::TextSensor* sensor, const std::string& state);
    void set_sensor_available(binary_sensor::BinarySensor* sensor, bool available);
    void set_sensor_available(sensor::Sensor* sensor, bool available);
    
    // State conversion helpers
    bool convert_sleep_status(VCSEC_VehicleSleepStatus_E status);
    bool convert_lock_status(VCSEC_VehicleLockState_E status);
    bool convert_user_presence(VCSEC_UserPresence_E presence);
    std::string get_charging_state_text(const CarServer_ChargeState_ChargingState& state);
    bool is_charger_connected_from_state(const CarServer_ChargeState_ChargingState& state);
};

} // namespace tesla_ble_vehicle
} // namespace esphome
