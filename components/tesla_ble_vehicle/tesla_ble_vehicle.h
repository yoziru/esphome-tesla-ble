#pragma once

#include <algorithm>
#include <cstring>
#include <iterator>
#include <vector>
#include <queue>

#include <esp_gattc_api.h>
#include <esphome/components/binary_sensor/binary_sensor.h>
#include <esphome/components/ble_client/ble_client.h>
#include <esphome/components/esp32_ble_tracker/esp32_ble_tracker.h>
#include <esphome/components/sensor/sensor.h>
#include <esphome/core/component.h>
#include <esphome/core/log.h>

#include <universal_message.pb.h>
#include <vcsec.pb.h>
#include <errors.h>

#include "custom_binary_sensor.h"

namespace TeslaBLE
{
    class Client;
}

typedef enum BLE_CarServer_VehicleAction_E
{
    SET_CHARGING_SWITCH,
    SET_CHARGING_AMPS,
    SET_CHARGING_LIMIT,
    SET_SENTRY_SWITCH,
    SET_HVAC_SWITCH,
    SET_HVAC_STEERING_HEATER_SWITCH
} BLE_CarServer_VehicleAction;

namespace esphome
{

    namespace tesla_ble_vehicle
    {
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
        static const int RX_TIMEOUT = 1 * 1000;       // Timeout interval between receiving chunks of a message (1s)
        static const int MAX_LATENCY = 4 * 1000;      // Max allowed error when syncing vehicle clock (4s)
        static const int BLOCK_LENGTH = 20;           // BLE MTU is 23 bytes, so we need to split the message into chunks (20 bytes as in vehicle_command)
        static const int MAX_RETRIES = 5;             // Max number of retries for a command
        static const int COMMAND_TIMEOUT = 30 * 1000; // Overall timeout for a command (30s)

        enum class BLECommandState
        {
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

        struct BLECommand
        {
            UniversalMessage_Domain domain;
            std::function<int()> execute;
            std::string execute_name;
            BLECommandState state;
            uint32_t started_at = millis();
            uint32_t last_tx_at = 0;
            uint8_t retry_count = 0;

            BLECommand(UniversalMessage_Domain d, std::function<int()> e, std::string n = "")
                : domain(d), execute(e), execute_name(n), state(BLECommandState::IDLE) {}
        };

        struct BLETXChunk
        {
            std::vector<unsigned char> data;
            esp_gatt_write_type_t write_type;
            esp_gatt_auth_req_t auth_req;
            uint32_t sent_at = millis();
            uint8_t retry_count = 0;

            BLETXChunk(std::vector<unsigned char> d, esp_gatt_write_type_t wt, esp_gatt_auth_req_t ar)
                : data(d), write_type(wt), auth_req(ar) {}
        };

        struct BLERXChunk
        {
            std::vector<unsigned char> buffer;
            uint32_t received_at = millis();

            BLERXChunk(std::vector<unsigned char> b)
                : buffer(b) {}
        };

        struct BLEResponse
        {
            // universal message
            UniversalMessage_RoutableMessage message;
            uint32_t received_at = millis();

            BLEResponse(UniversalMessage_RoutableMessage m)
                : message(m) {}
        };

        class TeslaBLEVehicle : public PollingComponent,
                                public ble_client::BLEClientNode
        {
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
            int nvs_save_session_info(const Signatures_SessionInfo &session_info, const UniversalMessage_Domain domain);
            int nvs_load_session_info(Signatures_SessionInfo *session_info, const UniversalMessage_Domain domain);
            int nvs_initialize_private_key();

            int handleSessionInfoUpdate(UniversalMessage_RoutableMessage message, UniversalMessage_Domain domain);
            int handleVCSECVehicleStatus(VCSEC_VehicleStatus vehicleStatus);

            int wakeVehicle(void);
            int sendVCSECActionMessage(VCSEC_RKEAction_E action);
            int sendCarServerVehicleActionMessage(BLE_CarServer_VehicleAction action, int param);

            int sendSessionInfoRequest(UniversalMessage_Domain domain);
            int sendVCSECInformationRequest(void);
            void enqueueVCSECInformationRequest(bool force = false);

            int writeBLE(const unsigned char *message_buffer, size_t message_length,
                         esp_gatt_write_type_t write_type, esp_gatt_auth_req_t auth_req);

            // sensors
            // Sleep state (vehicleSleepStatus)
            void set_binary_sensor_is_asleep(binary_sensor::BinarySensor *s) { isAsleepSensor = static_cast<binary_sensor::CustomBinarySensor *>(s); }
            void updateIsAsleep(bool asleep)
            {
                isAsleepSensor->publish_state(asleep);
            }
            // Door lock (vehicleLockState)
            void set_binary_sensor_is_unlocked(binary_sensor::BinarySensor *s) { isUnlockedSensor = static_cast<binary_sensor::CustomBinarySensor *>(s); }
            void updateisUnlocked(bool locked)
            {
                isUnlockedSensor->publish_state(locked);
            }
            // User presence (userPresence)
            void set_binary_sensor_is_user_present(binary_sensor::BinarySensor *s) { isUserPresentSensor = static_cast<binary_sensor::CustomBinarySensor *>(s); }
            void updateIsUserPresent(bool present)
            {
                isUserPresentSensor->publish_state(present);
            }
            // Charge flap (chargeFlapStatus)
            void set_binary_sensor_is_charge_flap_open(binary_sensor::BinarySensor *s) { isChargeFlapOpenSensor = static_cast<binary_sensor::CustomBinarySensor *>(s); }
            void updateIsChargeFlapOpen(bool open)
            {
                isChargeFlapOpenSensor->publish_state(open);
            }
            void setChargeFlapHasState(bool has_state)
            {
                isChargeFlapOpenSensor->set_has_state(has_state);
            }

            // set sensors to unknown (e.g. when vehicle is disconnected)
            void setSensors(bool has_state)
            {
                isAsleepSensor->set_has_state(has_state);
                isUnlockedSensor->set_has_state(has_state);
                isUserPresentSensor->set_has_state(has_state);
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
            binary_sensor::CustomBinarySensor *isAsleepSensor;
            binary_sensor::CustomBinarySensor *isUnlockedSensor;
            binary_sensor::CustomBinarySensor *isUserPresentSensor;
            binary_sensor::CustomBinarySensor *isChargeFlapOpenSensor;

            std::vector<unsigned char> ble_read_buffer_;

            void initializeFlash();
            void openNVSHandle();
            void initializePrivateKey();
            void loadSessionInfo();
            void loadDomainSessionInfo(UniversalMessage_Domain domain);
        };

    } // namespace tesla_ble_vehicle
} // namespace esphome
