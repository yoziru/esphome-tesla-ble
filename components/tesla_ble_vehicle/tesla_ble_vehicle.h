#pragma once

#include <memory>
#include <esphome/components/ble_client/ble_client.h>
#include <esphome/components/esp32_ble_tracker/esp32_ble_tracker.h>
#include <esphome/components/binary_sensor/binary_sensor.h>
#include <esphome/components/sensor/sensor.h>
#include <esphome/components/text_sensor/text_sensor.h>
#include <esphome/components/button/button.h>
#include <esphome/components/switch/switch.h>
#include <esphome/components/number/number.h>
#include <esphome/core/component.h>
#include <esphome/core/automation.h>

#include "ble_adapter_impl.h"
#include "storage_adapter_impl.h"
#include <vehicle.h>
#include "vehicle_state_manager.h"
#include <memory>

namespace esphome {
namespace tesla_ble_vehicle {

namespace espbt = esphome::esp32_ble_tracker;

static const char *const TAG = "tesla_ble_vehicle";

// Tesla BLE service UUIDs
static const char *const SERVICE_UUID = "00000211-b2d1-43f0-9b88-960cebf8b91e";
static const char *const READ_UUID = "00000213-b2d1-43f0-9b88-960cebf8b91e";
static const char *const WRITE_UUID = "00000212-b2d1-43f0-9b88-960cebf8b91e";

/**
 * @brief Main Tesla BLE Vehicle component
 * 
 * This is the main component that coordinates all Tesla BLE operations.
 * It uses specialized managers for different aspects of the communication.
 */
class TeslaBLEVehicle : public PollingComponent, public ble_client::BLEClientNode {
public:
    TeslaBLEVehicle();
    ~TeslaBLEVehicle() = default;

    // ESPHome component lifecycle
    void setup() override;
    void loop() override;
    void update() override;
    void dump_config() override;

    // BLE event handling
    void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;

    // Configuration setters
    void set_vin(const char *vin);
    void set_role(const std::string &role);
    void set_charging_amps_max(int amps_max);
    
    // Polling interval setters
    void set_vcsec_poll_interval(uint32_t interval_ms);
    void set_infotainment_poll_interval_awake(uint32_t interval_ms);
    void set_infotainment_poll_interval_active(uint32_t interval_ms);
    void set_infotainment_sleep_timeout(uint32_t interval_ms);

    // Sensor setters (delegate to state manager)
    void set_binary_sensor_is_asleep(binary_sensor::BinarySensor *s);
    void set_binary_sensor_is_unlocked(binary_sensor::BinarySensor *s);
    void set_binary_sensor_is_user_present(binary_sensor::BinarySensor *s);
    void set_binary_sensor_is_charge_flap_open(binary_sensor::BinarySensor *s);
    void set_binary_sensor_is_charger_connected(binary_sensor::BinarySensor *s);
    void set_battery_level_sensor(sensor::Sensor *sensor);
    void set_charger_power_sensor(sensor::Sensor *sensor);
    void set_charger_voltage_sensor(sensor::Sensor *sensor);
    void set_charger_current_sensor(sensor::Sensor *sensor);
    void set_charging_rate_sensor(sensor::Sensor *sensor);
    void set_charging_state_sensor(text_sensor::TextSensor *sensor);
    void set_iec61851_state_sensor(text_sensor::TextSensor *sensor);

    // Control setters (delegate to state manager)
    void set_charging_switch(switch_::Switch *sw);
    void set_charging_amps_number(number::Number *number);
    void set_charging_limit_number(number::Number *number);
    void unlock_charge_port();

    // Button setters
    void set_wake_button(button::Button *button);
    void set_pair_button(button::Button *button);
    void set_regenerate_key_button(button::Button *button);
    void set_force_update_button(button::Button *button);

    // Public vehicle actions
    int wake_vehicle();
    int start_pairing();
    int regenerate_key();
    void force_update();

    // Vehicle control actions
    int set_charging_state(bool charging);
    int set_charging_amps(int amps);
    int set_charging_limit(int limit);

    // Data request actions
    void request_vehicle_data();
    void request_charging_data();
    
    // Internal helper methods for state manager
    void update_charging_amps_max_value(int32_t new_max);

    // Manager accessors
    VehicleStateManager* get_state_manager() const { return state_manager_.get(); }
    
    // BLE connection state
    bool is_connected() const { return node_state == espbt::ClientState::ESTABLISHED; }
    uint16_t get_read_handle() const { return read_handle_; }
    uint16_t get_write_handle() const { return write_handle_; }

private:
    // Initialization helpers
    void initialize_managers();
    void initialize_ble_uuids();
    void configure_pending_sensors();
    void setup_button_callbacks();

    // Connection handlers
    void handle_connection_established();
    void handle_connection_lost();

    // Adapters & Managers
    std::shared_ptr<BleAdapterImpl> ble_adapter_;
    std::shared_ptr<StorageAdapterImpl> storage_adapter_;
    std::shared_ptr<::TeslaBLE::Vehicle> vehicle_;
    std::unique_ptr<VehicleStateManager> state_manager_;

    // Configuration
    std::string vin_;
    std::string role_;
    
    // Polling intervals
    uint32_t vcsec_poll_interval_{10000};
    uint32_t infotainment_poll_interval_awake_{30000};
    uint32_t infotainment_poll_interval_active_{10000};
    uint32_t infotainment_sleep_timeout_{660000};
    
    // Polling state
    uint32_t last_vcsec_poll_{0};
    uint32_t last_infotainment_poll_{0};

    // BLE state
    espbt::ESPBTUUID service_uuid_;
    espbt::ESPBTUUID read_uuid_;
    espbt::ESPBTUUID write_uuid_;
    uint16_t read_handle_{0};
    uint16_t write_handle_{0};

    // Pending sensors (waiting for state manager init)
    binary_sensor::BinarySensor *pending_asleep_sensor_{nullptr};
    binary_sensor::BinarySensor *pending_unlocked_sensor_{nullptr};
    binary_sensor::BinarySensor *pending_user_present_sensor_{nullptr};
    binary_sensor::BinarySensor *pending_charge_flap_sensor_{nullptr};
    binary_sensor::BinarySensor *pending_charger_sensor_{nullptr};
    
    sensor::Sensor *pending_battery_level_sensor_{nullptr};
    sensor::Sensor *pending_charger_power_sensor_{nullptr};
    sensor::Sensor *pending_charger_voltage_sensor_{nullptr};
    sensor::Sensor *pending_charger_current_sensor_{nullptr};
    sensor::Sensor *pending_charging_rate_sensor_{nullptr};
    
    text_sensor::TextSensor *pending_charging_state_sensor_{nullptr};
    text_sensor::TextSensor *pending_iec61851_state_sensor_{nullptr};
    
    switch_::Switch *pending_charging_switch_{nullptr};
    number::Number *pending_charging_amps_number_{nullptr};
    number::Number *pending_charging_limit_number_{nullptr};

    // Friends
    friend class VehicleStateManager;
};

// Custom button classes (simplified)
class TeslaWakeButton : public button::Button {
public:
    void set_parent(TeslaBLEVehicle *parent) { parent_ = parent; }
protected:
    void press_action() override;
    TeslaBLEVehicle *parent_{nullptr};
};

class TeslaPairButton : public button::Button {
public:
    void set_parent(TeslaBLEVehicle *parent) { parent_ = parent; }
protected:
    void press_action() override;
    TeslaBLEVehicle *parent_{nullptr};
};

class TeslaRegenerateKeyButton : public button::Button {
public:
    void set_parent(TeslaBLEVehicle *parent) { parent_ = parent; }
protected:
    void press_action() override;
    TeslaBLEVehicle *parent_{nullptr};
};

class TeslaForceUpdateButton : public button::Button {
public:
    void set_parent(TeslaBLEVehicle *parent) { parent_ = parent; }
protected:
    void press_action() override;
    TeslaBLEVehicle *parent_{nullptr};
};

class TeslaUnlockChargePortButton : public button::Button {
public:
    void set_parent(TeslaBLEVehicle *parent) { parent_ = parent; }
protected:
    void press_action() override;
    TeslaBLEVehicle *parent_{nullptr};
};

class TeslaChargingSwitch : public switch_::Switch {
public:
    void set_parent(TeslaBLEVehicle *parent) { parent_ = parent; }
protected:
    void write_state(bool state) override;
    TeslaBLEVehicle *parent_{nullptr};
};

class TeslaChargingAmpsNumber : public number::Number {
public:
    void set_parent(TeslaBLEVehicle *parent) { parent_ = parent; }
    void update_max_value(int32_t new_max);
protected:
    void control(float value) override;
    TeslaBLEVehicle *parent_{nullptr};
};

class TeslaChargingLimitNumber : public number::Number {
public:
    void set_parent(TeslaBLEVehicle *parent) { parent_ = parent; }
protected:
    void control(float value) override;
    TeslaBLEVehicle *parent_{nullptr};
};

// Action classes for automation (unchanged for compatibility)
template<typename... Ts> class WakeAction : public Action<Ts...> {
public:
    WakeAction(TeslaBLEVehicle *parent) : parent_(parent) {}
    void play(Ts... x) override { parent_->wake_vehicle(); }
protected:
    TeslaBLEVehicle *parent_;
};

template<typename... Ts> class PairAction : public Action<Ts...> {
public:
    PairAction(TeslaBLEVehicle *parent) : parent_(parent) {}
    void play(Ts... x) override { parent_->start_pairing(); }
protected:
    TeslaBLEVehicle *parent_;
};

template<typename... Ts> class RegenerateKeyAction : public Action<Ts...> {
public:
    RegenerateKeyAction(TeslaBLEVehicle *parent) : parent_(parent) {}
    void play(Ts... x) override { parent_->regenerate_key(); }
protected:
    TeslaBLEVehicle *parent_;
};

template<typename... Ts> class ForceUpdateAction : public Action<Ts...> {
public:
    ForceUpdateAction(TeslaBLEVehicle *parent) : parent_(parent) {}
    void play(Ts... x) override { parent_->force_update(); }
protected:
    TeslaBLEVehicle *parent_;
};

template<typename... Ts> class SetChargingAction : public Action<Ts...> {
public:
    SetChargingAction(TeslaBLEVehicle *parent) : parent_(parent) {}
    void set_state(esphome::TemplatableValue<bool, Ts...> state) { state_ = state; }
    void play(Ts... x) override {
        bool state = state_.value(x...);
        parent_->set_charging_state(state);
    }
protected:
    TeslaBLEVehicle *parent_;
    esphome::TemplatableValue<bool, Ts...> state_;
};

template<typename... Ts> class SetChargingAmpsAction : public Action<Ts...> {
public:
    SetChargingAmpsAction(TeslaBLEVehicle *parent) : parent_(parent) {}
    void set_amps(esphome::TemplatableValue<int, Ts...> amps) { amps_ = amps; }
    void play(Ts... x) override {
        int amps = amps_.value(x...);
        parent_->set_charging_amps(amps);
    }
protected:
    TeslaBLEVehicle *parent_;
    esphome::TemplatableValue<int, Ts...> amps_;
};

template<typename... Ts> class SetChargingLimitAction : public Action<Ts...> {
public:
    SetChargingLimitAction(TeslaBLEVehicle *parent) : parent_(parent) {}
    void set_limit(esphome::TemplatableValue<int, Ts...> limit) { limit_ = limit; }
    void play(Ts... x) override {
        int limit = limit_.value(x...);
        parent_->set_charging_limit(limit);
    }
protected:
    TeslaBLEVehicle *parent_;
    esphome::TemplatableValue<int, Ts...> limit_;
};

} // namespace tesla_ble_vehicle
} // namespace esphome
