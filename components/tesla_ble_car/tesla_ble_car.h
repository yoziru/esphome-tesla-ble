#pragma once

#include <esp_gattc_api.h>
#include <algorithm>
#include <iterator>
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"
#include "esphome/core/log.h"

#include "vcsec.pb.h"

namespace TeslaBLE {
    class Client;
}

namespace esphome {

namespace tesla_ble_car {

namespace espbt = esphome::esp32_ble_tracker;

static const char *const SERVICE_UUID = "00000211-b2d1-43f0-9b88-960cebf8b91e";
static const char *const READ_UUID = "00000213-b2d1-43f0-9b88-960cebf8b91e";
static const char *const WRITE_UUID = "00000212-b2d1-43f0-9b88-960cebf8b91e";

static const int MAX_BLE_MESSAGE_SIZE = 1024;
static const int RX_TIMEOUT = 1 * 1000; // Timeout interval between receiving chunks of a mesasge (1s)
static const int MAX_LATENCY = 4 * 1000; // Max allowed error when syncing vehicle clock (4s)

class TeslaBLECar : public ble_client::BLEClientNode {
 public:
  TeslaBLECar();
  void startPair(void);
  void wakeVehicle(void);
  void lockVehicle(void);
  void unlockVehicle(void);
  void sendCommand(VCSEC_RKEAction_E action);
  void sendEphemeralKeyRequest(void);

  // void update() override;

  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;

 protected:
  TeslaBLE::Client* m_pClient;
  uint32_t storage_handle_;
  uint16_t handle_;
  uint16_t read_handle_{0};
  uint16_t write_handle_{0};

  espbt::ESPBTUUID service_uuid_;
  espbt::ESPBTUUID read_uuid_;
  espbt::ESPBTUUID write_uuid_;
  bool isAuthenticated;

  // uint8_t responses_pending_{0};
  // void response_pending_();
  // void response_received_();
  // void set_response_timeout_();
};

template<typename... Ts>
class TeslaBLEPair : public Action<Ts...>, public Parented<TeslaBLECar> {
    public:
        void play(Ts... x) override { 
            this->parent_->startPair();
        }
};

template<typename... Ts>
class TeslaBLEWake : public Action<Ts...>, public Parented<TeslaBLECar> {
    public:
        void play(Ts... x) override { 
            this->parent_->wakeVehicle();
        }
};

}  // namespace tesla_ble_car
}  // namespace esphome
