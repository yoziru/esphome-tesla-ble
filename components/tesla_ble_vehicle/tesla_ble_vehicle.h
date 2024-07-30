#pragma once

#include <algorithm>
#include <cstring>
#include <iterator>
#include <vector>
#include <mutex>

#include <esp_gattc_api.h>
#include <esphome/components/binary_sensor/binary_sensor.h>
#include <esphome/components/ble_client/ble_client.h>
#include <esphome/components/esp32_ble_tracker/esp32_ble_tracker.h>
#include <esphome/components/sensor/sensor.h>
#include <esphome/core/component.h>
#include <esphome/core/log.h>

#include <universal_message.pb.h>
#include <vcsec.pb.h>

namespace TeslaBLE
{
    class Client;
}

namespace esphome
{

    namespace tesla_ble_vehicle
    {
        namespace espbt = esphome::esp32_ble_tracker;

        static const char *const SERVICE_UUID = "00000211-b2d1-43f0-9b88-960cebf8b91e";
        static const char *const READ_UUID = "00000213-b2d1-43f0-9b88-960cebf8b91e";
        static const char *const WRITE_UUID = "00000212-b2d1-43f0-9b88-960cebf8b91e";

        static const int RX_TIMEOUT = 1 * 1000;  // Timeout interval between receiving chunks of a mesasge (1s)
        static const int MAX_LATENCY = 4 * 1000; // Max allowed error when syncing vehicle clock (4s)
        static const int BLOCK_LENGTH = 20;      // BLE MTU is 23 bytes, so we need to split the message into chunks (20 bytes as in vehicle_command)

        class TeslaBLEVehicle : public PollingComponent, public ble_client::BLEClientNode
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

            void regenerateKey();
            int startPair(void);
            int nvs_save_session_info(const Signatures_SessionInfo &session_info, const UniversalMessage_Domain domain);
            int nvs_load_session_info(Signatures_SessionInfo *session_info, const UniversalMessage_Domain domain);
            int nvs_initialize_private_key();

            int handleSessionInfoUpdate(UniversalMessage_RoutableMessage message, UniversalMessage_Domain domain);
            int handleVCSECVehicleStatus(VCSEC_VehicleStatus vehicleStatus);

            int wakeVehicle(void);
            int sendCommand(VCSEC_RKEAction_E action);
            int sendSessionInfoRequest(UniversalMessage_Domain domain);
            int sendInfoStatus(void);
            int setChargingAmps(int input_amps);
            int setChargingLimit(int input_percent);
            int setChargingSwitch(bool isOn);
            int vcsecPreflightCheck(void);
            int infotainmentPreflightCheck(void);

            int writeBLE(const unsigned char *message_buffer, size_t message_length,
                         esp_gatt_write_type_t write_type, esp_gatt_auth_req_t auth_req);

            // sensors
            // Sleep state (vehicleSleepStatus)
            void set_binary_sensor_is_asleep(binary_sensor::BinarySensor *s) { isAsleepSensor = s; }
            void updateIsAsleep(bool asleep)
            {
                if (isAsleepSensor != nullptr)
                {
                    isAsleepSensor->publish_state(asleep);
                }
            }
            // Door lock (vehicleLockState)
            void set_binary_sensor_is_unlocked(binary_sensor::BinarySensor *s) { isUnlockedSensor = s; }
            void updateisUnlocked(bool locked)
            {
                if (isUnlockedSensor != nullptr)
                {
                    isUnlockedSensor->publish_state(locked);
                }
            }
            // User presence (userPresence)
            void set_binary_sensor_is_user_present(binary_sensor::BinarySensor *s) { isUserPresentSensor = s; }
            void updateIsUserPresent(bool present)
            {
                if (isUserPresentSensor != nullptr)
                {
                    isUserPresentSensor->publish_state(present);
                }
            }

            // set sensors to unknown (e.g. when vehicle is disconnected)
            void setSensorsUnknown()
            {
                updateIsAsleep(NAN);
                updateisUnlocked(NAN);
                updateIsUserPresent(NAN);
            }

        protected:
            TeslaBLE::Client *tesla_ble_client_;
            uint32_t storage_handle_;
            uint16_t handle_;
            uint16_t read_handle_{0};
            uint16_t write_handle_{0};
            std::mutex write_mutex_;

            espbt::ESPBTUUID service_uuid_;
            espbt::ESPBTUUID read_uuid_;
            espbt::ESPBTUUID write_uuid_;

            // sensors
            binary_sensor::BinarySensor *isAsleepSensor;
            binary_sensor::BinarySensor *isUnlockedSensor;
            binary_sensor::BinarySensor *isUserPresentSensor;

            std::vector<unsigned char> read_buffer;
            size_t current_size = 0;

            void initializeFlash();
            void openNVSHandle();
            void initializePrivateKey();
            void loadSessionInfo();
            void loadDomainSessionInfo(UniversalMessage_Domain domain);
        };

    } // namespace tesla_ble_vehicle
} // namespace esphome
