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
#include <esphome/components/select/select.h>
#include <map>
#include <string>
#include <cmath>
#include <type_traits>
#include <car_server.pb.h>
#include <vcsec.pb.h>
#if __has_include(<charging_power.h>)
#include <charging_power.h>
#endif

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
    
    // Const version for state queries
    const binary_sensor::BinarySensor* get_binary_sensor(const std::string& id) const;
    
    // ==========================================================================
    // Control setters (switches and numbers need special handling)
    // ==========================================================================
    void set_charging_switch(switch_::Switch* sw) { charging_switch_ = sw; }
    void set_sentry_mode_switch(switch_::Switch* sw) { sentry_mode_switch_ = sw; }
    void set_steering_wheel_heat_switch(switch_::Switch* sw) { steering_wheel_heat_switch_ = sw; }
    void set_charging_amps_number(number::Number* number) {
        charging_amps_number_ = number;
        if (number != nullptr && charging_amps_max_ > 0) {
            number->traits.set_max_value(static_cast<float>(charging_amps_max_));
        }
    }
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
    void set_sunroof_cover(cover::Cover* cvr) { sunroof_cover_ = cvr; }
    void set_climate(climate::Climate* clm) { climate_ = clm; }
    void set_seat_select(int seat, select::Select* sel);
    void set_cabin_protection_select(select::Select* sel) { cabin_protection_select_ = sel; }
    void set_cabin_temperature_select(select::Select* sel) { cabin_temperature_select_ = sel; }

    // ==========================================================================
    // Observed seat capabilities
    // ==========================================================================
    int seat_cool_level(int seat) const;
    bool seat_auto_supported(int seat) const;
    
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
#if __has_include(<charging_power.h>)
    void update_charging_power(const TeslaBLE::ChargingPower& power);
#endif
    void update_climate_state(const CarServer_ClimateState& climate_state);
    void update_drive_state(const CarServer_DriveState& drive_state);
    void update_tire_pressure_state(const CarServer_TirePressureState& tire_pressure_state);
    void update_closures_state(const CarServer_ClosuresState& closures_state);
    void update_software_update_state(const CarServer_SoftwareUpdateState& update_state,
                                      const std::string& version);
    
    // ==========================================================================
    // Direct state updates (for specific use cases)
    // ==========================================================================
    void update_asleep(bool asleep);
    void update_unlocked(bool unlocked);
    void update_user_present(bool present);
    void update_charge_flap_open(bool open);
    void update_charging_amps(float amps);
    void update_charging_limit(float limit);
    void update_charging_control_state(bool charging);
    void update_steering_wheel_heat(bool enabled);
    void update_sentry_mode(bool enabled);
    void republish_charging_amps();
    void republish_charging_limit();
    
    // ==========================================================================
    // State queries
    // ==========================================================================
    bool is_asleep() const;
    bool is_sentry_mode() const;
    bool is_charging() const { return is_charging_; }
    
    // ==========================================================================
    // Dynamic limits
    // ==========================================================================
    void update_charging_amps_max(int32_t new_max);
    int get_charging_amps_max() const { return charging_amps_max_; }
    void set_charging_amps_max(int max) {
        if (max <= 0) return;
        charging_amps_max_ = max;
        if (charging_amps_number_) {
            charging_amps_number_->traits.set_max_value(static_cast<float>(max));
        }
    }
    
    
private:
    TeslaBLEVehicle* parent_;

    // Climate sub-updates (called from update_climate_state)
    void update_seat_climate_state(const CarServer_ClimateState& climate_state);
    void update_cabin_protection_state(const CarServer_ClimateState& climate_state);
    void update_steering_wheel_heat_mode(const CarServer_ClimateState& climate_state);
    
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
    cover::Cover* sunroof_cover_{nullptr};
    climate::Climate* climate_{nullptr};
    // Seat climate selects indexed by TeslaBLE::SeatPosition value.
    select::Select* seat_selects_[16]{};
    select::Select* cabin_protection_select_{nullptr};
    select::Select* cabin_temperature_select_{nullptr};
    
    // ==========================================================================
    // Internal state tracking
    // ==========================================================================
    bool is_charging_{false};
    int charging_amps_max_{0};
    
    // Climate state tracking
    float current_inside_temp_{NAN};
    float target_temp_{21.0f};
    bool climate_on_{false};
    // Seat climate indexed by TeslaBLE::SeatPosition value (-1 = not reported).
    int seat_heat_levels_[16]{};
    int seat_cool_levels_[16]{};
    bool seat_auto_modes_[16]{};
    bool seat_auto_observed_[16]{};

    
    // ==========================================================================
    // Helper methods for publishing sensor state
    // ==========================================================================
    bool publish_binary_sensor(const std::string& id, bool state);
    bool publish_sensor(const std::string& id, float state);
    bool publish_text_sensor(const std::string& id, const std::string& state);

    template<typename T, typename V> static bool publish_sensor_state(T *entity, V state) {
      if (entity == nullptr) return false;
      if constexpr (std::is_floating_point<V>::value) {
        if (!entity->has_state() || std::isnan(entity->state) != std::isnan(state) ||
            std::abs(entity->state - state) > 0.001f) {
          entity->publish_state(state);
          return true;
        }
      } else {
        if (!entity->has_state() || entity->state != state) {
          entity->publish_state(state);
          return true;
        }
      }
      return false;
    }

    template<typename T> static void set_sensor_available(T *entity, bool available) {
      if (entity != nullptr) entity->set_has_state(available);
    }

};

} // namespace tesla_ble_vehicle
} // namespace esphome
