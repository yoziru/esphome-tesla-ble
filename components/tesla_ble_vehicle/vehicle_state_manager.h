#pragma once

#include <esphome/core/log.h>
#include <esphome/components/binary_sensor/binary_sensor.h>
#include <esphome/components/sensor/sensor.h>
#include <esphome/components/text_sensor/text_sensor.h>
#include <esphome/components/switch/switch.h>
#include <esphome/components/number/number.h>
#include <esphome/components/lock/lock.h>
#include <esphome/components/cover/cover.h>
#include <esphome/components/climate/climate.h>
#include <optional>
#include <map>
#include <string>
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
 * It uses a map-based approach for sensor storage, making it easy to add new sensors
 * without modifying C++ code - just add to the Python sensor definitions.
 * 
 * Sensors are stored by string ID and can be accessed via get_*() methods in update functions.
 */
class VehicleStateManager {
public:
    explicit VehicleStateManager(TeslaBLEVehicle* parent);
    
    // ==========================================================================
    // Generic sensor setters - use these from Python codegen
    // ==========================================================================
    void set_binary_sensor(const std::string& id, binary_sensor::BinarySensor* sensor);
    void set_sensor(const std::string& id, sensor::Sensor* sensor);
    void set_text_sensor(const std::string& id, text_sensor::TextSensor* sensor);
    
    // Generic sensor getters - use these in update methods
    binary_sensor::BinarySensor* get_binary_sensor(const std::string& id);
    sensor::Sensor* get_sensor(const std::string& id);
    text_sensor::TextSensor* get_text_sensor(const std::string& id);
    
    // Const versions for state queries
    const binary_sensor::BinarySensor* get_binary_sensor(const std::string& id) const;
    const sensor::Sensor* get_sensor(const std::string& id) const;
    const text_sensor::TextSensor* get_text_sensor(const std::string& id) const;
    
    // ==========================================================================
    // Control setters (switches and numbers need special handling)
    // ==========================================================================
    void set_charging_switch(switch_::Switch* sw) { charging_switch_ = sw; }
    void set_sentry_mode_switch(switch_::Switch* sw) { sentry_mode_switch_ = sw; }
    void set_steering_wheel_heat_switch(switch_::Switch* sw) { steering_wheel_heat_switch_ = sw; }
    void set_charging_amps_number(number::Number* number) { charging_amps_number_ = number; }
    void set_charging_limit_number(number::Number* number) { charging_limit_number_ = number; }
    
    // ==========================================================================
    // Lock, Cover, and Climate setters
    // ==========================================================================
    void set_doors_lock(lock::Lock* lck) { doors_lock_ = lck; }
    void set_charge_port_latch_lock(lock::Lock* lck) { charge_port_latch_lock_ = lck; }
    void set_trunk_cover(cover::Cover* cvr) { trunk_cover_ = cvr; }
    void set_frunk_cover(cover::Cover* cvr) { frunk_cover_ = cvr; }
    void set_windows_cover(cover::Cover* cvr) { windows_cover_ = cvr; }
    void set_charge_port_door_cover(cover::Cover* cvr) { charge_port_door_cover_ = cvr; }
    void set_climate(climate::Climate* clm) { climate_ = clm; }
    
    // Lock, Cover, Climate getters (for state manager access)
    lock::Lock* get_doors_lock() { return doors_lock_; }
    lock::Lock* get_charge_port_latch_lock() { return charge_port_latch_lock_; }
    cover::Cover* get_trunk_cover() { return trunk_cover_; }
    cover::Cover* get_frunk_cover() { return frunk_cover_; }
    cover::Cover* get_windows_cover() { return windows_cover_; }
    cover::Cover* get_charge_port_door_cover() { return charge_port_door_cover_; }
    climate::Climate* get_climate() { return climate_; }
    
    // ==========================================================================
    // State updates from VCSEC
    // ==========================================================================
    void update_vehicle_status(const VCSEC_VehicleStatus& status);
    void update_sleep_status(VCSEC_VehicleSleepStatus_E status);
    void update_lock_status(VCSEC_VehicleLockState_E status);
    void update_user_presence(VCSEC_UserPresence_E presence);
    
    // ==========================================================================
    // State updates from CarServer (Infotainment)
    // ==========================================================================
    void update_charge_state(const CarServer_ChargeState& charge_state);
    void update_climate_state(const CarServer_ClimateState& climate_state);
    void update_drive_state(const CarServer_DriveState& drive_state);
    void update_tire_pressure_state(const CarServer_TirePressureState& tire_pressure_state);
    void update_closures_state(const CarServer_ClosuresState& closures_state);
    
    // ==========================================================================
    // Direct state updates (for specific use cases)
    // ==========================================================================
    void update_asleep(bool asleep);
    void update_unlocked(bool unlocked);
    void update_user_present(bool present);
    void update_charge_flap_open(bool open);
    void update_charging_amps(float amps);
    void update_charger_connected(bool connected);
    
    // ==========================================================================
    // Connection state management
    // ==========================================================================
    void set_sensors_available(bool available);
    void reset_all_states();
    
    // ==========================================================================
    // State queries
    // ==========================================================================
    bool is_asleep() const;
    bool is_unlocked() const;
    bool is_user_present() const;
    bool is_charge_flap_open() const;
    bool is_charging() const { return is_charging_; }
    float get_charging_amps() const;
    
    // ==========================================================================
    // Dynamic limits
    // ==========================================================================
    void update_charging_amps_max(int32_t new_max);
    int get_charging_amps_max() const { return charging_amps_max_; }
    void set_charging_amps_max(int max) { charging_amps_max_ = max; }
    
    // ==========================================================================
    // ==========================================================================
    // Command tracking
    // ==========================================================================
    void track_command_issued();
    
private:
    TeslaBLEVehicle* parent_;
    
    // ==========================================================================
    // Sensor storage maps - sensors are stored by string ID
    // ==========================================================================
    std::map<std::string, binary_sensor::BinarySensor*> binary_sensors_;
    std::map<std::string, sensor::Sensor*> sensors_;
    std::map<std::string, text_sensor::TextSensor*> text_sensors_;
    
    // ==========================================================================
    // Controls (special handling needed, not in maps)
    // ==========================================================================
    switch_::Switch* charging_switch_{nullptr};
    switch_::Switch* sentry_mode_switch_{nullptr};
    switch_::Switch* steering_wheel_heat_switch_{nullptr};
    number::Number* charging_amps_number_{nullptr};
    number::Number* charging_limit_number_{nullptr};
    
    // ==========================================================================
    // Lock, Cover, and Climate entities
    // ==========================================================================
    lock::Lock* doors_lock_{nullptr};
    lock::Lock* charge_port_latch_lock_{nullptr};
    cover::Cover* trunk_cover_{nullptr};
    cover::Cover* frunk_cover_{nullptr};
    cover::Cover* windows_cover_{nullptr};
    cover::Cover* charge_port_door_cover_{nullptr};
    climate::Climate* climate_{nullptr};
    
    // ==========================================================================
    // Internal state tracking
    // ==========================================================================
    bool is_charging_{false};
    bool is_user_present_{false};
    int charging_amps_max_{32};
    
    // Climate state tracking
    float current_inside_temp_{NAN};
    float target_temp_{21.0f};
    bool climate_on_{false};
    

    
    // ==========================================================================
    // Helper methods for publishing sensor state
    // ==========================================================================
    bool publish_binary_sensor(const std::string& id, bool state);
    bool publish_sensor(const std::string& id, float state);
    bool publish_text_sensor(const std::string& id, const std::string& state);
    
    // Overloads for direct pointer access (used internally)
    bool publish_sensor_state(binary_sensor::BinarySensor* sensor, bool state);
    bool publish_sensor_state(sensor::Sensor* sensor, float state);
    bool publish_sensor_state(switch_::Switch* switch_comp, bool state);
    bool publish_sensor_state(number::Number* number_comp, float state);
    bool publish_sensor_state(text_sensor::TextSensor* sensor, const std::string& state);
    
    void set_sensor_available(binary_sensor::BinarySensor* sensor, bool available);
    void set_sensor_available(sensor::Sensor* sensor, bool available);
    
    // ==========================================================================
    // State conversion helpers
    // ==========================================================================
    std::optional<bool> convert_sleep_status(VCSEC_VehicleSleepStatus_E status);
    std::optional<bool> convert_lock_status(VCSEC_VehicleLockState_E status);
    std::optional<bool> convert_user_presence(VCSEC_UserPresence_E presence);
    std::string get_charging_state_text(const CarServer_ChargeState_ChargingState& state);
    bool is_charger_connected_from_state(const CarServer_ChargeState_ChargingState& state);
    std::string get_iec61851_state_text(const CarServer_ChargeState_ChargingState& state);
    std::string get_shift_state_text(const CarServer_ShiftState& state);
};

} // namespace tesla_ble_vehicle
} // namespace esphome
