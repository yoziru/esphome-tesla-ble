#pragma once

#include <algorithm>
#include <cstring>
#include <iterator>
#include <queue>
#include <vector>

#include <esp_gattc_api.h>
#include <esphome/components/binary_sensor/binary_sensor.h>
#include <esphome/components/ble_client/ble_client.h>
#include <esphome/components/esp32_ble_tracker/esp32_ble_tracker.h>
#include <esphome/components/number/number.h>
#include <esphome/components/sensor/sensor.h>
#include <esphome/components/text_sensor/text_sensor.h>
#include <esphome/core/component.h>
#include <esphome/core/log.h>

#include <errors.h>
#include <universal_message.pb.h>
#include <vcsec.pb.h>

namespace TeslaBLE {
class Client;
}

typedef enum BLE_CarServer_VehicleAction_E {
  SET_CHARGING_SWITCH,
  SET_CHARGING_AMPS,
  SET_CHARGING_LIMIT
} BLE_CarServer_VehicleAction;

namespace esphome {

namespace tesla_ble_vehicle {

// Forward declaration
class TeslaBLEVehicle;

// Charging Amps Number Component
class ChargingAmpsNumber : public number::Number, public Component {
public:
  void set_parent(TeslaBLEVehicle *parent) { this->parent_ = parent; }
  void setup() override;

protected:
  void control(float value) override;
  TeslaBLEVehicle *parent_;
};

// Charge Limit Number Component
class ChargeLimitNumber : public number::Number, public Component {
public:
  void set_parent(TeslaBLEVehicle *parent) { this->parent_ = parent; }
  void setup() override;

protected:
  void control(float value) override;
  TeslaBLEVehicle *parent_;
};
namespace espbt = esphome::esp32_ble_tracker;

static const char *const TAG = "tesla_ble_vehicle";
static const char *nvs_key_infotainment = "tk_infotainment";
static const char *nvs_key_vcsec = "tk_vcsec";

static const char *const SERVICE_UUID = "00000211-b2d1-43f0-9b88-960cebf8b91e";
static const char *const READ_UUID = "00000213-b2d1-43f0-9b88-960cebf8b91e";
static const char *const WRITE_UUID = "00000212-b2d1-43f0-9b88-960cebf8b91e";

static const int PRIVATE_KEY_SIZE = 228;
static const int PUBLIC_KEY_SIZE = 65;
static const int MAX_BLE_MESSAGE_SIZE = 1024; // Max size of a BLE message
static const int RX_TIMEOUT =
    1 * 1000; // Timeout interval between receiving chunks of a message (1s)
static const int MAX_LATENCY =
    4 * 1000; // Max allowed error when syncing vehicle clock (4s)
static const int BLOCK_LENGTH =
    20; // BLE MTU is 23 bytes, so we need to split the message into chunks (20
        // bytes as in vehicle_command)
static const int MAX_RETRIES = 5; // Max number of retries for a command
static const int COMMAND_TIMEOUT =
    30 * 1000; // Overall timeout for a command (30s)

enum class BLECommandState {
  IDLE,
  WAITING_FOR_VCSEC_AUTH,
  WAITING_FOR_VCSEC_AUTH_RESPONSE,
  WAITING_FOR_INFOTAINMENT_AUTH,
  WAITING_FOR_INFOTAINMENT_AUTH_RESPONSE,
  WAITING_FOR_WAKE,
  WAITING_FOR_WAKE_RESPONSE,
  READY,
  WAITING_FOR_RESPONSE,
};

struct BLECommand {
  UniversalMessage_Domain domain;
  std::function<int()> execute;
  std::string execute_name;
  BLECommandState state;
  uint32_t started_at = millis();
  uint32_t last_tx_at = 0;
  uint8_t retry_count = 0;

  BLECommand(UniversalMessage_Domain d, std::function<int()> e,
             std::string n = "")
      : domain(d), execute(e), execute_name(n), state(BLECommandState::IDLE) {}
};

struct BLETXChunk {
  std::vector<unsigned char> data;
  esp_gatt_write_type_t write_type;
  esp_gatt_auth_req_t auth_req;
  uint32_t sent_at = millis();
  uint8_t retry_count = 0;

  BLETXChunk(std::vector<unsigned char> d, esp_gatt_write_type_t wt,
             esp_gatt_auth_req_t ar)
      : data(d), write_type(wt), auth_req(ar) {}
};

struct BLERXChunk {
  std::vector<unsigned char> buffer;
  uint32_t received_at = millis();

  BLERXChunk(std::vector<unsigned char> b) : buffer(b) {}
};

struct BLEResponse {
  // universal message
  UniversalMessage_RoutableMessage message;
  uint32_t received_at = millis();

  BLEResponse(UniversalMessage_RoutableMessage m) : message(m) {}
};

class TeslaBLEVehicle : public PollingComponent,
                        public ble_client::BLEClientNode {
public:
  TeslaBLEVehicle();
  void setup() override;
  void loop() override;
  void update() override;
  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;
  void dump_config() override;
  void set_vin(const char *vin);
  void process_command_queue();
  void process_response_queue();
  void process_ble_read_queue();
  void process_ble_write_queue();
  void invalidateSession(UniversalMessage_Domain domain);

  void regenerateKey();
  int startPair(void);
  int nvs_save_session_info(const Signatures_SessionInfo &session_info,
                            const UniversalMessage_Domain domain);
  int nvs_load_session_info(Signatures_SessionInfo *session_info,
                            const UniversalMessage_Domain domain);
  int nvs_initialize_private_key();

  int handleSessionInfoUpdate(UniversalMessage_RoutableMessage message,
                              UniversalMessage_Domain domain);
  int handleVCSECVehicleStatus(VCSEC_VehicleStatus vehicleStatus);
  int handleCarServerVehicleData(const CarServer_VehicleData &vehicleData);

  int wakeVehicle(void);
  int sendVCSECActionMessage(VCSEC_RKEAction_E action);
  int sendCarServerGetVehicleData(void);
  int sendCarServerVehicleActionMessage(BLE_CarServer_VehicleAction action,
                                        int param);

  int sendSessionInfoRequest(UniversalMessage_Domain domain);
  int sendVCSECInformationRequest(void);
  void enqueueVCSECInformationRequest(bool force = false);

  int writeBLE(const unsigned char *message_buffer, size_t message_length,
               esp_gatt_write_type_t write_type, esp_gatt_auth_req_t auth_req);

  // sensors
  // Sleep state (vehicleSleepStatus)
  void set_binary_sensor_is_asleep(binary_sensor::BinarySensor *s) {
    this->isAsleepSensor = static_cast<binary_sensor::BinarySensor *>(s);
  }
  void updateIsAsleep(bool asleep) {
    this->isAsleepSensor->publish_state(asleep);
  }
  // Door lock (vehicleLockState)
  void set_binary_sensor_is_unlocked(binary_sensor::BinarySensor *s) {
    this->isUnlockedSensor = static_cast<binary_sensor::BinarySensor *>(s);
  }
  void updateisUnlocked(bool locked) {
    this->isUnlockedSensor->publish_state(locked);
  }
  // User presence (userPresence)
  void set_binary_sensor_is_user_present(binary_sensor::BinarySensor *s) {
    this->isUserPresentSensor = static_cast<binary_sensor::BinarySensor *>(s);
  }
  void updateIsUserPresent(bool present) {
    this->isUserPresentSensor->publish_state(present);
  }
  // Charge flap (chargeFlapStatus)
  void set_binary_sensor_is_charge_flap_open(binary_sensor::BinarySensor *s) {
    this->isChargeFlapOpenSensor =
        static_cast<binary_sensor::BinarySensor *>(s);
  }
  void updateIsChargeFlapOpen(bool open) {
    this->isChargeFlapOpenSensor->publish_state(open);
  }
  void setChargeFlapHasState(bool has_state) {
    this->isChargeFlapOpenSensor->set_has_state(has_state);
  }

  // Charge state sensors
  void set_sensor_battery_level(sensor::Sensor *s) {
    this->batteryLevelSensor = s;
  }
  void set_text_sensor_charging_state(text_sensor::TextSensor *s) {
    this->chargingStateSensor = s;
  }
  void set_sensor_charger_power(sensor::Sensor *s) {
    this->chargerPowerSensor = s;
  }
  void set_sensor_charge_rate(sensor::Sensor *s) { this->chargeRateSensor = s; }
  void set_sensor_max_charging_amps(sensor::Sensor *s) {
    this->maxChargingAmpsSensor = s;
  }

  void set_binary_sensor_charger_switch(binary_sensor::BinarySensor *s) {
    this->chargerSwitchSensor = s;
  }
  void set_charge_polling_interval(uint32_t interval) {
    this->charge_polling_interval_ = interval;
  }
  void set_awake_polling_interval(uint32_t interval) {
    this->awake_polling_interval_ = interval;
  }

  void updateBatteryLevel(float level) {
    if (this->batteryLevelSensor != nullptr) {
      this->batteryLevelSensor->publish_state(level);
    }
  }
  void updateChargingState(const std::string &state) {
    if (this->chargingStateSensor != nullptr) {
      this->chargingStateSensor->publish_state(state);
    }
  }
  void updateChargerPower(float power) {
    if (this->chargerPowerSensor != nullptr) {
      this->chargerPowerSensor->publish_state(power);
    }
  }
  void updateChargeRate(float rate) {
    if (this->chargeRateSensor != nullptr) {
      this->chargeRateSensor->publish_state(rate);
    }
  }
  void updateMaxChargingAmps(float amps) {
    if (this->maxChargingAmpsSensor != nullptr) {
      this->maxChargingAmpsSensor->publish_state(amps);
    }
  }

  void updateChargeLimit(float limit) { this->current_charge_limit_ = limit; }
  void updateChargingAmps(float amps) { this->current_charging_amps_ = amps; }

  // Getters for template numbers and switches
  float getCurrentChargeLimit() { return this->current_charge_limit_; }
  float getCurrentChargingAmps() { return this->current_charging_amps_; }
  float getCurrentMaxChargingAmps() { return this->current_max_charging_amps_; }
  bool getCurrentChargerSwitchState() {
    return this->current_charger_switch_state_;
  }

  // Method to sync template numbers with current car state
  void syncTemplateNumbers();

  // Set number components
  void set_charging_amps_number(ChargingAmpsNumber *number) {
    this->charging_amps_number_ = number;
  }
  void set_charge_limit_number(ChargeLimitNumber *number) {
    this->charge_limit_number_ = number;
  }

  void updateChargerSwitch(bool enabled) {
    this->current_charger_switch_state_ = enabled;
    if (this->chargerSwitchSensor != nullptr) {
      this->chargerSwitchSensor->publish_state(enabled);
    }
  }

  // set sensors to unknown (e.g. when vehicle is disconnected)
  void setSensors(bool has_state) {
    this->isAsleepSensor->set_has_state(has_state);
    this->isUnlockedSensor->set_has_state(has_state);
    this->isUserPresentSensor->set_has_state(has_state);
    if (this->batteryLevelSensor != nullptr)
      this->batteryLevelSensor->set_has_state(has_state);
    if (this->chargingStateSensor != nullptr)
      this->chargingStateSensor->set_has_state(has_state);
    if (this->chargerPowerSensor != nullptr)
      this->chargerPowerSensor->set_has_state(has_state);
    if (this->chargeRateSensor != nullptr)
      this->chargeRateSensor->set_has_state(has_state);
    if (this->maxChargingAmpsSensor != nullptr)
      this->maxChargingAmpsSensor->set_has_state(has_state);

    if (this->chargerSwitchSensor != nullptr)
      this->chargerSwitchSensor->set_has_state(has_state);
  }

protected:
  std::queue<BLERXChunk> ble_read_queue_;
  std::queue<BLEResponse> response_queue_;
  std::queue<BLETXChunk> ble_write_queue_;
  std::queue<BLECommand> command_queue_;

  TeslaBLE::Client *tesla_ble_client_;
  uint32_t storage_handle_;
  uint16_t handle_;
  uint16_t read_handle_{0};
  uint16_t write_handle_{0};

  espbt::ESPBTUUID service_uuid_;
  espbt::ESPBTUUID read_uuid_;
  espbt::ESPBTUUID write_uuid_;

  // sensors
  binary_sensor::BinarySensor *isAsleepSensor;
  binary_sensor::BinarySensor *isUnlockedSensor;
  binary_sensor::BinarySensor *isUserPresentSensor;
  binary_sensor::BinarySensor *isChargeFlapOpenSensor;

  // charge state sensors
  sensor::Sensor *batteryLevelSensor = nullptr;
  text_sensor::TextSensor *chargingStateSensor = nullptr;
  sensor::Sensor *chargerPowerSensor = nullptr;
  sensor::Sensor *chargeRateSensor = nullptr;
  sensor::Sensor *maxChargingAmpsSensor = nullptr;

  binary_sensor::BinarySensor *chargerSwitchSensor = nullptr;

  // polling configuration
  uint32_t charge_polling_interval_ = 10000; // 10 seconds default
  uint32_t awake_polling_interval_ = 300000; // 5 minutes default
  uint32_t last_charge_poll_ = 0;
  uint32_t last_awake_poll_ = 0;
  bool is_charging_ = false;
  bool was_asleep_ =
      true; // Track previous sleep state to detect wake transitions

  // Store current values for template number and switch access
  float current_charge_limit_ = NAN;
  float current_charging_amps_ = NAN;
  float current_max_charging_amps_ = NAN;
  bool current_charger_switch_state_ = false;

  // References to number components
  ChargingAmpsNumber *charging_amps_number_ = nullptr;
  ChargeLimitNumber *charge_limit_number_ = nullptr;

  std::vector<unsigned char> ble_read_buffer_;

  void initializeFlash();
  void openNVSHandle();
  void initializePrivateKey();
  void loadSessionInfo();
  void loadDomainSessionInfo(UniversalMessage_Domain domain);
};

} // namespace tesla_ble_vehicle
} // namespace esphome
